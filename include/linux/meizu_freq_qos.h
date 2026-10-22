/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_MEIZU_FREQ_QOS_H
#define _LINUX_MEIZU_FREQ_QOS_H
#include <linux/pm_qos.h>
/* Called synchronously with the real QoS lock held and IRQs disabled.
 * The visitor must not sleep, change QoS, or retain the request pointer.
 */
int meizu_freq_qos_walk(struct freq_constraints *qos,
		enum freq_qos_req_type type,
		void (*visit)(const struct freq_qos_request *, s32, void *),
		void *data);
#endif
