/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __QCOM_APSS_MINIDUMP_H__
#define __QCOM_APSS_MINIDUMP_H__

#include <linux/kconfig.h>
#include <linux/types.h>

struct device;

#if IS_REACHABLE(CONFIG_QCOM_APSS_MINIDUMP)
/*
 * Register a physically contiguous direct-map buffer for APSS crash collection.
 * Its contents must remain valid until the device's devres releases this entry.
 * Only data suitable for the firmware crash report should be registered.
 */
int qcom_devm_minidump_add(struct device *dev, const char *name,
			  const void *data, size_t size);
#else
static inline int qcom_devm_minidump_add(struct device *dev, const char *name,
					const void *data, size_t size)
{
	return 0;
}
#endif
#endif
