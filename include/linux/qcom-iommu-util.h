/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_QCOM_IOMMU_UTIL_H
#define _LINUX_QCOM_IOMMU_UTIL_H

struct device;
struct iommu_domain;

enum sid_switch_direction {
	SID_ACQUIRE,
	SID_RELEASE,
};

int qcom_iommu_sid_switch(struct device *dev, enum sid_switch_direction dir);
int qcom_iommu_enable_s1_translation(struct iommu_domain *domain);

#endif
