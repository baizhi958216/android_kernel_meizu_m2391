/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_QCOM_IOMMU_UTIL_H
#define _LINUX_QCOM_IOMMU_UTIL_H

#include <linux/bits.h>

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

int qcom_iommu_get_context_bank_nr(struct iommu_domain *domain);
int qcom_iommu_get_asid_nr(struct iommu_domain *domain);
int qcom_iommu_sid_switch(struct device *dev, enum sid_switch_direction dir);
int qcom_iommu_enable_s1_translation(struct iommu_domain *domain);

#endif
