// SPDX-License-Identifier: GPL-2.0-only
/* Kalama protected multimedia page tables are shared read-only with their VM. */
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/mempool.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <dt-bindings/firmware/qcom,scm.h>

#include "arm-smmu.h"

struct kalama_pt_pool {
	u32 vmid;
	mempool_t pages;
	spinlock_t lock;
	struct list_head retired;
	struct work_struct release;
};

static void *kalama_pt_new(gfp_t gfp, void *arg)
{
	struct kalama_pt_pool *pool = arg;
	struct qcom_scm_vmperm perms[] = {
		{ QCOM_SCM_VMID_HLOS, QCOM_SCM_PERM_RW },
		{ pool->vmid, QCOM_SCM_PERM_READ },
	};
	u64 src = BIT_ULL(QCOM_SCM_VMID_HLOS);
	struct page *page;
	int ret;

	if (!gfpflags_allow_blocking(gfp))
		return NULL;
	page = alloc_page(gfp | __GFP_ZERO);
	if (!page)
		return NULL;
	ret = qcom_scm_assign_mem(page_to_phys(page), PAGE_SIZE, &src, perms, ARRAY_SIZE(perms));
	if (ret) {
		/* Firmware errors can leave the owner indeterminate. */
		pr_err_ratelimited("arm-smmu: failed to share page table with VM %u: %d\n",
				   pool->vmid, ret);
		return NULL;
	}
	return page;
}

static void kalama_pt_reclaim(struct work_struct *work)
{
	struct kalama_pt_pool *pool = container_of(work, struct kalama_pt_pool, release);
	struct qcom_scm_vmperm perm = { QCOM_SCM_VMID_HLOS, QCOM_SCM_PERM_RWX };
	struct page *page, *next;
	unsigned long flags;
	LIST_HEAD(retired);

	spin_lock_irqsave(&pool->lock, flags);
	list_splice_init(&pool->retired, &retired);
	spin_unlock_irqrestore(&pool->lock, flags);
	list_for_each_entry_safe(page, next, &retired, lru) {
		u64 src = BIT_ULL(QCOM_SCM_VMID_HLOS) | BIT_ULL(pool->vmid);
		int ret;

		list_del(&page->lru);
		ret = qcom_scm_assign_mem(page_to_phys(page), PAGE_SIZE, &src, &perm, 1);
		if (!ret)
			__free_page(page);
		else
			pr_err_ratelimited("arm-smmu: failed to reclaim page table from VM %u: %d\n",
					   pool->vmid, ret);
	}
}

static void kalama_pt_retire(void *entry, void *arg)
{
	struct kalama_pt_pool *pool = arg;
	struct page *page = entry;
	unsigned long flags;

	spin_lock_irqsave(&pool->lock, flags);
	list_add_tail(&page->lru, &pool->retired);
	spin_unlock_irqrestore(&pool->lock, flags);
	schedule_work(&pool->release);
}

static void *kalama_pt_alloc(void *cookie, size_t size, gfp_t gfp)
{
	struct arm_smmu_domain *d = cookie;
	struct page *page;

	if (size > PAGE_SIZE)
		return NULL;
	page = kalama_pt_new(gfp, d->secure_pt);
	if (!page)
		page = mempool_alloc_preallocated(&d->secure_pt->pages);
	return page ? page_address(page) : NULL;
}

static void kalama_pt_free(void *cookie, void *addr, size_t size)
{
	struct arm_smmu_domain *d = cookie;

	clear_page(addr);
	mempool_free(virt_to_page(addr), &d->secure_pt->pages);
}

int kalama_pt_init(struct arm_smmu_domain *d, struct device *dev, struct io_pgtable_cfg *cfg)
{
	struct kalama_pt_pool *pool = d->secure_pt;
	u32 vmid;
	int ret;

	if (!of_property_present(dev->of_node, "qcom,iommu-vmid"))
		return pool ? -EINVAL : 0;
	ret = of_property_read_u32(dev->of_node, "qcom,iommu-vmid", &vmid);
	if (ret || (vmid != QCOM_SCM_VMID_CP_PIXEL && vmid != QCOM_SCM_VMID_CP_NON_PIXEL &&
		    vmid != QCOM_SCM_VMID_CP_BITSTREAM))
		return -EINVAL;
	if (pool && pool->vmid != vmid)
		return -EINVAL;
	if (!pool) {
		pool = kzalloc(sizeof(*pool), GFP_KERNEL);
		if (!pool)
			return -ENOMEM;
		pool->vmid = vmid;
		spin_lock_init(&pool->lock);
		INIT_LIST_HEAD(&pool->retired);
		INIT_WORK(&pool->release, kalama_pt_reclaim);
		/* A splitting unmap may need a table in atomic context. */
		ret = mempool_init(&pool->pages, 4, kalama_pt_new, kalama_pt_retire, pool);
		if (ret) {
			flush_work(&pool->release);
			kfree(pool);
			return ret;
		}
		d->secure_pt = pool;
	}
	cfg->alloc = kalama_pt_alloc;
	cfg->free = kalama_pt_free;
	return 0;
}

bool kalama_pt_matches(struct arm_smmu_domain *d, struct device *dev)
{
	u32 vmid;

	if (!of_property_present(dev->of_node, "qcom,iommu-vmid"))
		return !d->secure_pt;
	return !of_property_read_u32(dev->of_node, "qcom,iommu-vmid", &vmid) &&
	       d->secure_pt && d->secure_pt->vmid == vmid;
}

void kalama_pt_destroy(struct arm_smmu_domain *d)
{
	struct kalama_pt_pool *pool = d->secure_pt;

	if (!pool)
		return;
	mempool_exit(&pool->pages);
	flush_work(&pool->release);
	kfree(pool);
	d->secure_pt = NULL;
}
