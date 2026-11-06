/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_REMOTEPROC_QCOM_Q6V5_H
#define __LINUX_REMOTEPROC_QCOM_Q6V5_H

#include <linux/notifier.h>

/* Valid for the duration of the threaded watchdog or fatal error callback. */
struct qcom_q6v5_crash_data {
	const char *name;
	const char *message;
	size_t length;
};

/* The notification event is an enum rproc_crash_type. */
int qcom_q6v5_register_crash_notifier(struct notifier_block *nb);
int qcom_q6v5_unregister_crash_notifier(struct notifier_block *nb);

#endif
