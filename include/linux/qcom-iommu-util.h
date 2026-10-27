/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_QCOM_IOMMU_UTIL_H
#define _LINUX_QCOM_IOMMU_UTIL_H

#include <linux/bits.h>

/* DMA allocation with inner non-cacheable, outer system cacheable pages. */
#define DMA_ATTR_SYS_CACHE_ONLY		BIT(16)

/* Stage-1 outer system cache attributes for Qualcomm LLCC clients. */
#define IOMMU_SYS_CACHE			BIT(6)
#define IOMMU_SYS_CACHE_NWA		BIT(7)

#define IOMMU_FAULT_TRANSLATION		BIT(2)
#define IOMMU_FAULT_PERMISSION		BIT(3)
#define IOMMU_FAULT_EXTERNAL		BIT(4)
#define IOMMU_FAULT_TRANSACTION_STALLED	BIT(5)

struct device;
struct iommu_domain;

enum sid_switch_direction {
	SID_ACQUIRE,
	SID_RELEASE,
};

struct qcom_iommu_fault_ids {
	u32 bid;
	u32 pid;
	u32 mid;
};

int qcom_iommu_dma_enable_best_fit(struct device *dev);
int qcom_iommu_get_fault_ids(struct iommu_domain *domain, struct qcom_iommu_fault_ids *ids);
int qcom_iommu_get_context_bank_nr(struct iommu_domain *domain);
int qcom_iommu_get_asid_nr(struct iommu_domain *domain);
int qcom_iommu_sid_switch(struct device *dev, enum sid_switch_direction dir);
int qcom_iommu_enable_s1_translation(struct iommu_domain *domain);

#endif
