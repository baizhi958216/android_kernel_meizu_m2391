/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_SCMI_PLH_H
#define _LINUX_SCMI_PLH_H

#include <linux/types.h>

#define QCOM_SCMI_PLH 0x81
#define PLH_MAX_WORDS 50

enum plh_feature { PLH_SCROLL, PLH_LAUNCH };
enum plh_action { PLH_TABLE, PLH_START, PLH_STOP, PLH_SAMPLE, PLH_LOG };

struct scmi_protocol_handle;
struct plh_ops {
	int (*command)(const struct scmi_protocol_handle *ph, enum plh_feature feature,
		       enum plh_action action, const u16 *args, size_t count);
};

struct kobject;
struct kobject *msm_perf_kobject(void);

#endif
