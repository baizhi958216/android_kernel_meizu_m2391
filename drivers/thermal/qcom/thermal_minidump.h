/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __QCOM_THERMAL_MINIDUMP_H__
#define __QCOM_THERMAL_MINIDUMP_H__

#include <linux/kconfig.h>
#include <linux/types.h>

struct device;
struct thermal_minidump;

#if IS_REACHABLE(CONFIG_QTI_THERMAL_MINIDUMP)
struct thermal_minidump *devm_thermal_minidump_register(struct device *dev, const char *name);
void thermal_minidump_sample(struct thermal_minidump *md, const char *type, int temp);
#else
static inline struct thermal_minidump *
devm_thermal_minidump_register(struct device *dev, const char *name)
{
	return NULL;
}

static inline void thermal_minidump_sample(struct thermal_minidump *md, const char *type, int temp)
{
}
#endif
#endif
