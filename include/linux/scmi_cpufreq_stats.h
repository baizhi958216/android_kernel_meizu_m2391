/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_SCMI_CPUFREQ_STATS_H
#define _LINUX_SCMI_CPUFREQ_STATS_H

#include <linux/types.h>

#define QCOM_SCMI_CPUFREQ_STATS 0x84

struct scmi_protocol_handle;
struct cpufreq_stats_ops {
	int (*get_memory)(const struct scmi_protocol_handle *ph, phys_addr_t *base, u32 *size);
};

#endif
