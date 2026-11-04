/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_MEIZU_VIP_TASK_H
#define _LINUX_MEIZU_VIP_TASK_H
#include <linux/types.h>
struct rq;
struct task_struct;
#ifdef CONFIG_MEIZU_VIP_TASK
extern unsigned long global_vip_task_enabled;
extern int global_debug_enabled;
bool test_task_vip(struct task_struct *task);
void mz_check_preempt_wakeup(struct rq *rq, struct task_struct *task,
			   bool *preempt, bool *nopreempt);
void mz_replace_next_task_fair(struct rq *rq, struct task_struct **task);
#else
static inline void mz_check_preempt_wakeup(struct rq *rq, struct task_struct *task,
		bool *preempt, bool *nopreempt) {}
static inline void mz_replace_next_task_fair(struct rq *rq, struct task_struct **task) {}
#endif
#endif
