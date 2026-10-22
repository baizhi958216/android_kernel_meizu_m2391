/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_ENHANCE_QCOM_WDOG_H
#define _MEIZU_ENHANCE_QCOM_WDOG_H
#include <linux/cpumask.h>
#include <linux/sched.h>
void mz_get_cpu_ping_mask(cpumask_t *ping_mask, int *cpu_idle_pc_state);
void mz_dump_wdog_cpu(struct task_struct *w_task);
void mz_dump_cpu_online_smp_call(void);
#endif
