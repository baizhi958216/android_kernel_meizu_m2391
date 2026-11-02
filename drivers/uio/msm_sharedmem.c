// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm modem remote-storage shared window. */
#include <linux/device.h>
#include <linux/dma-direct.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/uio_driver.h>
#include <dt-bindings/firmware/qcom,scm.h>

#define MPSS_RMTS_CLIENT_ID 1

struct rmtfs_window {
	struct uio_info info;
	struct device *dev;
	struct kref refs;
	dma_addr_t dma;
	void *cpu_addr;
	size_t alloc_size;
	u64 owners;
};

static void rmtfs_release(struct kref *ref)
{
	struct rmtfs_window *win = container_of(ref, struct rmtfs_window, refs);
	const struct qcom_scm_vmperm host = { QCOM_SCM_VMID_HLOS, QCOM_SCM_PERM_RW };
	struct uio_mem *map = &win->info.mem[0];
	int ret = 0;

	if (win->owners != BIT_ULL(QCOM_SCM_VMID_HLOS))
		ret = qcom_scm_assign_mem(map->addr, map->size, &win->owners, &host, 1);
	if (ret)
		dev_err(win->dev, "cannot reclaim modem-owned storage window: %d\n", ret);
	else if (win->cpu_addr)
		dma_free_attrs(win->dev, win->alloc_size, win->cpu_addr, win->dma,
			       DMA_ATTR_FORCE_CONTIGUOUS);
	put_device(win->dev);
	kfree(win);
}

static void rmtfs_vma_open(struct vm_area_struct *vma)
{
	struct rmtfs_window *win = vma->vm_private_data;

	kref_get(&win->refs);
}

static void rmtfs_vma_close(struct vm_area_struct *vma)
{
	struct rmtfs_window *win = vma->vm_private_data;

	kref_put(&win->refs, rmtfs_release);
}

static const struct vm_operations_struct rmtfs_vm_ops = {
	.open = rmtfs_vma_open,
	.close = rmtfs_vma_close,
};

static int rmtfs_mmap(struct uio_info *info, struct vm_area_struct *vma)
{
	struct rmtfs_window *win = container_of(info, struct rmtfs_window, info);
	struct uio_mem *map = &info->mem[0];
	size_t len = vma->vm_end - vma->vm_start;
	int ret;

	if (vma->vm_pgoff || len > map->size)
		return -EINVAL;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
	ret = remap_pfn_range(vma, vma->vm_start, PHYS_PFN(map->addr),
			      len, vma->vm_page_prot);
	if (!ret) {
		vma->vm_private_data = win;
		vma->vm_ops = &rmtfs_vm_ops;
		rmtfs_vma_open(vma);
	}
	return ret;
}

static int rmtfs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct qcom_scm_vmperm perms[] = {
		{ QCOM_SCM_VMID_HLOS, QCOM_SCM_PERM_RW },
		{ QCOM_SCM_VMID_MSS_MSA, QCOM_SCM_PERM_RW },
		{ QCOM_SCM_VMID_NAV, QCOM_SCM_PERM_RW },
	};
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct rmtfs_window *win;
	struct uio_mem *map;
	resource_size_t padding, size;
	u32 client;
	int ret;

	if (!res || !PAGE_ALIGNED(res->start) || !PAGE_ALIGNED(resource_size(res)) ||
	    !resource_size(res))
		return -EINVAL;
	ret = of_property_read_u32(dev->of_node, "qcom,client-id", &client);
	if (ret)
		return ret;
	if (client == MPSS_RMTS_CLIENT_ID && !qcom_scm_is_available())
		return -EPROBE_DEFER;

	win = kzalloc(sizeof(*win), GFP_KERNEL);
	if (!win)
		return -ENOMEM;
	kref_init(&win->refs);
	win->dev = get_device(dev);
	win->owners = BIT_ULL(QCOM_SCM_VMID_HLOS);
	win->info.name = res->name;
	win->info.version = "1.0";
	win->info.mmap = rmtfs_mmap;
	map = &win->info.mem[0];
	map->memtype = UIO_MEM_PHYS;
	map->size = resource_size(res);
	map->addr = res->start;
	if (!map->addr) {
		/* The firmware receives a physical address, never a translated IOVA. */
		if (device_iommu_mapped(dev)) {
			ret = -EINVAL;
			goto release;
		}
		padding = of_property_read_bool(dev->of_node, "qcom,guard-memory") ? SZ_4K : 0;
		if (check_add_overflow(map->size, 2 * padding, &size) || size > SIZE_MAX) {
			ret = -EOVERFLOW;
			goto release;
		}
		win->alloc_size = size;
		win->cpu_addr = dma_alloc_attrs(dev, size, &win->dma, GFP_KERNEL,
					       DMA_ATTR_FORCE_CONTIGUOUS);
		if (!win->cpu_addr) {
			ret = -ENOMEM;
			goto release;
		}
		map->addr = dma_to_phys(dev, win->dma) + padding;
	}
	if (client == MPSS_RMTS_CLIENT_ID) {
		bool nav = of_property_read_bool(dev->of_node, "qcom,vm-nav-path");

		ret = qcom_scm_assign_mem(map->addr, map->size, &win->owners,
					  perms, nav ? 3 : 2);
		if (ret)
			goto release;
	}
	ret = uio_register_device(dev, &win->info);
	if (ret)
		goto release;
	platform_set_drvdata(pdev, win);
	return 0;

release:
	kref_put(&win->refs, rmtfs_release);
	return ret;
}

static void rmtfs_remove(struct platform_device *pdev)
{
	struct rmtfs_window *win = platform_get_drvdata(pdev);

	uio_unregister_device(&win->info);
	kref_put(&win->refs, rmtfs_release);
}

static const struct of_device_id rmtfs_match[] = {
	{ .compatible = "qcom,sharedmem-uio" },
	{}
};
MODULE_DEVICE_TABLE(of, rmtfs_match);

static struct platform_driver rmtfs_driver = {
	.probe = rmtfs_probe,
	.remove = rmtfs_remove,
	.driver = { .name = "msm_sharedmem", .of_match_table = rmtfs_match },
};
module_platform_driver(rmtfs_driver);

MODULE_DESCRIPTION("Qualcomm UIO shared memory for remote modem storage");
MODULE_LICENSE("GPL");
