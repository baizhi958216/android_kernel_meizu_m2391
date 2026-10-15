// SPDX-License-Identifier: GPL-2.0-only
/* Kalama QTEE shared buffers and VM-specific DMA buffer bridges. */
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/firmware/qcom/qcom_tzmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/qtee_shmbridge.h>
#include <linux/refcount.h>
#include <linux/sizes.h>
#include <linux/slab.h>

struct shared_region {
	struct list_head list;
	phys_addr_t start;
	size_t size;
	u64 vmids;
	u16 perms;
	u8 count;
	u8 tz_perm;
	u64 handle;
	refcount_t users;
};

static DEFINE_MUTEX(bridge_lock);
static LIST_HEAD(regions);
static struct qcom_tzmem_pool *shm_pool;
static size_t pool_size;

bool qtee_shmbridge_is_enabled(void)
{
	return !!smp_load_acquire(&shm_pool);
}
EXPORT_SYMBOL_GPL(qtee_shmbridge_is_enabled);

int qtee_shmbridge_allocate_shm(size_t size, struct qtee_shm *shm)
{
	struct qcom_tzmem_pool *pool = smp_load_acquire(&shm_pool);
	void *ptr;

	if (!pool)
		return -EPROBE_DEFER;
	if (!shm || !size || size > pool_size)
		return -EINVAL;
	size = PAGE_ALIGN(size);
	ptr = qcom_tzmem_alloc(pool, size, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;
	memset(ptr, 0, size);
	shm->vaddr = ptr;
	shm->paddr = qcom_tzmem_to_phys(ptr);
	shm->size = size;
	return 0;
}
EXPORT_SYMBOL_GPL(qtee_shmbridge_allocate_shm);

void qtee_shmbridge_free_shm(struct qtee_shm *shm)
{
	if (!shm || !shm->vaddr)
		return;
	memzero_explicit(shm->vaddr, shm->size);
	qcom_tzmem_free(shm->vaddr);
	*shm = (struct qtee_shm){ };
}
EXPORT_SYMBOL_GPL(qtee_shmbridge_free_shm);

/* TZMEM uses dma_alloc_coherent(). Order CPU accesses across the secure call. */
void qtee_shmbridge_flush_shm_buf(struct qtee_shm *shm)
{
	dma_wmb();
}
EXPORT_SYMBOL_GPL(qtee_shmbridge_flush_shm_buf);

void qtee_shmbridge_inv_shm_buf(struct qtee_shm *shm)
{
	dma_rmb();
}
EXPORT_SYMBOL_GPL(qtee_shmbridge_inv_shm_buf);

int qtee_shmbridge_query(phys_addr_t start)
{
	struct shared_region *r;

	guard(mutex)(&bridge_lock);
	list_for_each_entry(r, &regions, list)
		if (r->start == start)
			return -EEXIST;
	return 0;
}
EXPORT_SYMBOL_GPL(qtee_shmbridge_query);

int qtee_shmbridge_register(phys_addr_t start, size_t size, u32 *ids, u32 *perms,
			    u32 count, u32 tz_perm, u64 *handle)
{
	struct shared_region *r;
	phys_addr_t end;
	u64 vmids = 0;
	u16 permissions = 0;
	unsigned int i, j;
	int ret;

	if (!qtee_shmbridge_is_enabled())
		return -EPROBE_DEFER;
	if (!handle || !ids || !perms || !count || count > 4 || !size ||
	    !IS_ALIGNED(start, SZ_4K) || !IS_ALIGNED(size, SZ_4K) ||
	    !tz_perm || tz_perm > 7 || check_add_overflow(start, size, &end))
		return -EINVAL;
	for (i = 0; i < count; i++) {
		if (ids[i] > U16_MAX || !perms[i] || perms[i] > 7)
			return -EINVAL;
		for (j = 0; j < i; j++)
			if (ids[j] == ids[i])
				return -EINVAL;
		vmids = (vmids << 16) | ids[i];
		permissions = (permissions << 3) | perms[i];
	}

	guard(mutex)(&bridge_lock);
	list_for_each_entry(r, &regions, list) {
		if (end <= r->start || start >= r->start + r->size)
			continue;
		if (r->start != start || r->size != size || r->count != count ||
		    r->vmids != vmids || r->perms != permissions || r->tz_perm != tz_perm)
			return -EBUSY;
		refcount_inc(&r->users);
		*handle = r->handle;
		return 0;
	}

	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return -ENOMEM;
	ret = qcom_scm_shm_bridge_create(start | permissions, start | tz_perm,
					 size | ((u64)count << 9), vmids, &r->handle);
	/*
	 * Firmware returns 15 and a referenced handle when another secure
	 * client already registered the exact bridge. Pair it with delete.
	 */
	if (ret && !(ret == 15 && r->handle)) {
		kfree(r);
		return ret < 0 ? ret : -EACCES;
	}
	r->start = start;
	r->size = size;
	r->vmids = vmids;
	r->perms = permissions;
	r->tz_perm = tz_perm;
	r->count = count;
	refcount_set(&r->users, 1);
	list_add(&r->list, &regions);
	*handle = r->handle;
	return 0;
}
EXPORT_SYMBOL_GPL(qtee_shmbridge_register);

int qtee_shmbridge_deregister(u64 handle)
{
	struct shared_region *r;
	int ret;

	guard(mutex)(&bridge_lock);
	list_for_each_entry(r, &regions, list) {
		if (r->handle != handle)
			continue;
		if (refcount_read(&r->users) > 1) {
			refcount_dec(&r->users);
			return 0;
		}
		ret = qcom_scm_shm_bridge_delete(handle);
		if (ret)
			return ret;
		list_del(&r->list);
		kfree(r);
		return 0;
	}
	return -ENOENT;
}
EXPORT_SYMBOL_GPL(qtee_shmbridge_deregister);

static int kalama_bridge_probe(struct platform_device *pdev)
{
	struct qcom_tzmem_pool_config config = { .policy = QCOM_TZMEM_POLICY_STATIC };
	struct device_node *node = pdev->dev.of_node;
	struct qcom_tzmem_pool *pool;
	u32 pages = SZ_4M / SZ_4K;
	int ret;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;
	if (!qcom_tzmem_shm_bridge_enabled() ||
	    of_property_present(node, "qcom,support-hypervisor"))
		return -EOPNOTSUPP;
	if (of_property_present(node, "qcom,custom-bridge-size")) {
		ret = of_property_read_u32(node, "qcom,custom-bridge-size", &pages);
		if (ret)
			return ret;
	}
	if (!pages || check_mul_overflow((size_t)pages, (size_t)SZ_4K,
					&config.initial_size))
		return -EINVAL;
	guard(mutex)(&bridge_lock);
	if (shm_pool)
		return -EBUSY;
	pool = devm_qcom_tzmem_pool_new(&pdev->dev, &config);
	if (IS_ERR(pool))
		return PTR_ERR(pool);
	pool_size = config.initial_size;
	smp_store_release(&shm_pool, pool);
	return 0;
}

static void kalama_bridge_remove(struct platform_device *pdev)
{
	guard(mutex)(&bridge_lock);
	smp_store_release(&shm_pool, NULL);
}

static const struct of_device_id kalama_bridge_match[] = {
	{ .compatible = "qcom,tee-shared-memory-bridge" },
	{ }
};
MODULE_DEVICE_TABLE(of, kalama_bridge_match);

static struct platform_driver kalama_bridge_driver = {
	.probe = kalama_bridge_probe,
	.remove = kalama_bridge_remove,
	.driver = {
		.name = "kalama-shmbridge",
		.of_match_table = kalama_bridge_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(kalama_bridge_driver);
MODULE_DESCRIPTION("Qualcomm Kalama QTEE shared memory bridges");
MODULE_LICENSE("GPL");
