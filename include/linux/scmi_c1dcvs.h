/* SPDX-License-Identifier: GPL-2.0-only */
/* Qualcomm CPUCP C1 dynamic clock and voltage scaling protocol. */
#ifndef _LINUX_SCMI_C1DCVS_H
#define _LINUX_SCMI_C1DCVS_H

#include <linux/types.h>

#define QCOM_SCMI_C1DCVS 0x87
#define C1DCVS_MAX_VALUES 32

enum c1dcvs_setting {
	C1DCVS_ENABLE = 11,
	C1DCVS_TRACE = 13,
	C1DCVS_IPC = 15,
	C1DCVS_EFREQ = 17,
	C1DCVS_HYSTERESIS = 19,
	C1DCVS_OPT_MODE = 21,
};

struct scmi_protocol_handle;
struct c1dcvs_ops {
	int (*write)(const struct scmi_protocol_handle *ph, enum c1dcvs_setting setting,
		     const u32 *values, size_t count);
	int (*read)(const struct scmi_protocol_handle *ph, enum c1dcvs_setting setting,
		    u32 *values, size_t capacity);
};

int c1dcvs_enable(bool enable);
#endif
