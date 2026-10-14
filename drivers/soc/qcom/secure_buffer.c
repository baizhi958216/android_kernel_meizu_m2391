// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Google, Inc
 * Copyright (c) 2011-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <soc/qcom/secure_buffer.h>

struct secure_buffer_acl {
	u64 source;
	struct qcom_scm_vmperm dest[64];
	unsigned int count;
};

static bool secure_buffer_ready;
static bool vmid_cp_camera_preview_ro;

static int secure_buffer_acl_init(struct secure_buffer_acl *acl,
		const u32 *sources, int nr_sources, const int *dest,
		const int *perms, int nr_dest)
{
	u64 seen = 0;
	int i;

	if (!smp_load_acquire(&secure_buffer_ready))
		return -EPROBE_DEFER;
	if (!sources || !dest || !perms || nr_sources <= 0 || nr_sources > 64 ||
	    nr_dest <= 0 || nr_dest > ARRAY_SIZE(acl->dest))
		return -EINVAL;
	acl->source = 0;
	for (i = 0; i < nr_sources; i++) {
		if (sources[i] >= 64 || acl->source & BIT_ULL(sources[i]))
			return -EINVAL;
		acl->source |= BIT_ULL(sources[i]);
	}
	for (i = 0; i < nr_dest; i++) {
		if ((unsigned int)dest[i] >= 64 || seen & BIT_ULL(dest[i]) ||
		    !perms[i] || perms[i] & ~QCOM_SCM_PERM_RWX)
			return -EINVAL;
		seen |= BIT_ULL(dest[i]);
		acl->dest[i].vmid = dest[i];
		acl->dest[i].perm = perms[i];
	}
	acl->count = nr_dest;
	return 0;
}

int hyp_assign_phys(phys_addr_t addr, u64 size, u32 *sources, int nr_sources,
		    int *dest, int *perms, int nr_dest)
{
	struct qcom_scm_mem_region region = { addr, size };
	struct secure_buffer_acl acl;
	int ret;

	if (!size || !PAGE_ALIGNED(addr) || !PAGE_ALIGNED(size) ||
	    size > SIZE_MAX || addr > PHYS_ADDR_MAX - (size - 1))
		return -EINVAL;
	ret = secure_buffer_acl_init(&acl, sources, nr_sources, dest, perms,
				     nr_dest);
	if (ret)
		return ret;
	return qcom_scm_assign_mem_regions(&region, 1, acl.source, acl.dest,
					   acl.count);
}
EXPORT_SYMBOL_GPL(hyp_assign_phys);

int hyp_assign_table(struct sg_table *table, u32 *sources, int nr_sources,
		     int *dest, int *perms, int nr_dest)
{
	struct qcom_scm_mem_region regions[QCOM_SCM_MAX_MEM_REGIONS];
	struct secure_buffer_acl acl;
	unsigned int i, n = 0;
	struct scatterlist *sg;
	bool submitted = false;
	u64 bytes = 0;
	int ret;

	if (!table || !table->sgl || !table->orig_nents)
		return -EINVAL;
	ret = secure_buffer_acl_init(&acl, sources, nr_sources, dest, perms,
				     nr_dest);
	if (ret)
		return ret;

	/* Validate all extents before transferring any pages. */
	for_each_sgtable_sg(table, sg, i)
		if (!sg->length || !PAGE_ALIGNED(sg_phys(sg)) ||
		    !PAGE_ALIGNED(sg->length))
			return -EINVAL;

	for_each_sgtable_sg(table, sg, i) {
		if (n && (n == ARRAY_SIZE(regions) || bytes + sg->length > SZ_2M)) {
			ret = qcom_scm_assign_mem_regions(regions, n, acl.source,
							  acl.dest, acl.count);
			if (ret)
				return submitted ? -EADDRNOTAVAIL : ret;
			submitted = true;
			n = 0;
			bytes = 0;
		}
		regions[n].addr = sg_phys(sg);
		regions[n++].size = sg->length;
		bytes += sg->length;
	}
	ret = qcom_scm_assign_mem_regions(regions, n, acl.source, acl.dest, acl.count);
	return ret && submitted ? -EADDRNOTAVAIL : ret;
}
EXPORT_SYMBOL_GPL(hyp_assign_table);

const char *msm_secure_vmid_to_string(int secure_vmid)
{
	switch (secure_vmid) {
	case VMID_TZ:
		return "VMID_TZ";
	case VMID_SSC_Q6:
		return "VMID_SSC_Q6";
	case VMID_ADSP_Q6:
		return "VMID_ADSP_Q6";
	case VMID_HLOS:
		return "VMID_HLOS";
	case VMID_CP_TOUCH:
		return "VMID_CP_TOUCH";
	case VMID_CP_BITSTREAM:
		return "VMID_CP_BITSTREAM";
	case VMID_CP_PIXEL:
		return "VMID_CP_PIXEL";
	case VMID_CP_NON_PIXEL:
		return "VMID_CP_NON_PIXEL";
	case VMID_CP_CAMERA:
		return "VMID_CP_CAMERA";
	case VMID_HLOS_FREE:
		return "VMID_HLOS_FREE";
	case VMID_MSS_MSA:
		return "VMID_MSS_MSA";
	case VMID_MSS_NONMSA:
		return "VMID_MSS_NONMSA";
	case VMID_CP_SEC_DISPLAY:
		return "VMID_CP_SEC_DISPLAY";
	case VMID_CP_APP:
		return "VMID_CP_APP";
	case VMID_LPASS:
		return "VMID_LPASS";
	case VMID_WLAN:
		return "VMID_WLAN";
	case VMID_WLAN_CE:
		return "VMID_WLAN_CE";
	case VMID_CP_CAMERA_PREVIEW:
		return "VMID_CP_CAMERA_PREVIEW";
	case VMID_CP_SPSS_SP:
		return "VMID_CP_SPSS_SP";
	case VMID_CP_SPSS_SP_SHARED:
		return "VMID_CP_SPSS_SP_SHARED";
	case VMID_CP_SPSS_HLOS_SHARED:
		return "VMID_CP_SPSS_HLOS_SHARED";
	case VMID_ADSP_HEAP:
		return "VMID_ADSP_HEAP";
	case VMID_INVAL:
		return "VMID_INVAL";
	case VMID_CP_CDSP:
		return "VMID_CP_CDSP";
	case VMID_NAV:
		return "VMID_NAV";
	default:
		return "Unknown VMID";
	}
}
EXPORT_SYMBOL(msm_secure_vmid_to_string);

u32 msm_secure_get_vmid_perms(u32 vmid)
{
	if (vmid == VMID_CP_SEC_DISPLAY || (vmid == VMID_CP_CAMERA_PREVIEW &&
					    READ_ONCE(vmid_cp_camera_preview_ro)))
		return PERM_READ;
	else if (vmid == VMID_CP_CDSP)
		return PERM_READ | PERM_WRITE | PERM_EXEC;
	else
		return PERM_READ | PERM_WRITE;
}
EXPORT_SYMBOL(msm_secure_get_vmid_perms);

static int qcom_secure_buffer_probe(struct platform_device *pdev)
{
	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;
	WRITE_ONCE(vmid_cp_camera_preview_ro,
		of_property_read_bool(pdev->dev.of_node, "qcom,vmid-cp-camera-preview-ro"));
	smp_store_release(&secure_buffer_ready, true);
	return 0;
}

static void qcom_secure_buffer_remove(struct platform_device *pdev)
{
	smp_store_release(&secure_buffer_ready, false);
}

static const struct of_device_id qcom_secure_buffer_of_match[] = {
	{ .compatible = "qcom,secure-buffer" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_secure_buffer_of_match);

static struct platform_driver qcom_secure_buffer_driver = {
	.probe = qcom_secure_buffer_probe,
	.remove = qcom_secure_buffer_remove,
	.driver = {
		.name = "qcom_secure_buffer",
		.of_match_table = qcom_secure_buffer_of_match,
	},
};
module_platform_driver(qcom_secure_buffer_driver);
MODULE_DESCRIPTION("Qualcomm peripheral VM memory assignment");
MODULE_LICENSE("GPL v2");
