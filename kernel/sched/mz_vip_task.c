// SPDX-License-Identifier: GPL-2.0-only
/* M2391 VIP scheduling policy recovered from the original split BTF and code.
 * Restricted hooks retain callbacks permanently: this provider is built-in.
 */
#include "sched.h"
#include "../locking/mutex.h"
#include <linux/cgroup.h>
#include <linux/kernfs.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/prctl.h>
#include <linux/meizu_vip_task.h>
#include <trace/hooks/cgroup.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/sys.h>

extern void set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se);

struct mz_task_struct {
	struct list_head vip_entry;
	bool is_vip;
	bool is_static_vip;
	u64 sum_exec_baseline;
	u64 total_exec;
};
struct mz_rq { struct list_head vip_list; };
static_assert(sizeof(struct mz_task_struct) == 40);
static_assert(sizeof(struct mz_task_struct) <= sizeof_field(struct task_struct, android_oem_data1));
static_assert(sizeof(struct mz_rq) <= sizeof_field(struct rq, android_oem_data1));
static_assert(offsetof(struct task_struct, android_oem_data1) == 3392);
static_assert(offsetof(struct rq, android_oem_data1) == 4368);
static_assert(sizeof(struct task_struct) == 4608);
static_assert(sizeof(struct rq) == 4544);
static_assert(offsetof(struct task_struct, __state) == 40);
static_assert(offsetof(struct task_struct, cpu) == 88);
static_assert(offsetof(struct task_struct, on_rq) == 120);
static_assert(offsetof(struct task_struct, prio) == 124);
static_assert(offsetof(struct task_struct, se) == 192);
static_assert(offsetof(struct task_struct, se.exec_start) == 256);
static_assert(offsetof(struct task_struct, se.sum_exec_runtime) == 264);
static_assert(offsetof(struct task_struct, pid) == 1496);
static_assert(offsetof(struct task_struct, group_leader) == 1560);
static_assert(offsetof(struct task_struct, comm) == 1960);
static_assert(offsetof(struct rq, curr) == 2720);
static_assert(offsetof(struct rq, clock_task) == 2816);
static_assert(offsetof(struct rq, cpu) == 2984);
static_assert(offsetof(struct mutex_waiter, list) == 0);
static_assert(offsetof(struct mutex_waiter, task) == 16);
static_assert(offsetof(struct rwsem_waiter, list) == 0);
static_assert(offsetof(struct rwsem_waiter, task) == 16);

unsigned long global_vip_task_enabled;
int global_debug_enabled;
EXPORT_SYMBOL(global_vip_task_enabled);
EXPORT_SYMBOL(global_debug_enabled);

static struct mz_task_struct *vip(struct task_struct *p)
{
	return (struct mz_task_struct *)p->android_oem_data1;
}
static struct list_head *vip_list(struct rq *rq)
{
	return &((struct mz_rq *)rq->android_oem_data1)->vip_list;
}
static bool feature(unsigned int bit)
{
	return READ_ONCE(global_vip_task_enabled) & BIT(bit);
}
bool test_task_vip(struct task_struct *p)
{
	return p && READ_ONCE(vip(p)->is_vip);
}
EXPORT_SYMBOL_GPL(test_task_vip);
static bool static_vip(struct task_struct *p)
{
	return p && READ_ONCE(vip(p)->is_static_vip);
}
static bool fair_task(struct task_struct *p)
{
	return p && p->prio >= MAX_RT_PRIO && p->prio < MAX_PRIO;
}
static bool linked(struct mz_task_struct *v)
{
	return v->vip_entry.next && v->vip_entry.prev &&
		!list_empty(&v->vip_entry);
}
static struct task_struct *vip_task(struct list_head *entry)
{
	return (struct task_struct *)((char *)entry -
		offsetof(struct task_struct, android_oem_data1));
}
static void init_task_vip(struct task_struct *p)
{
	struct mz_task_struct *v = vip(p);

	INIT_LIST_HEAD(&v->vip_entry);
	v->is_vip = false;
	v->is_static_vip = false;
}
static void enqueue_vip_task(struct rq *rq, struct task_struct *p)
{
	struct mz_task_struct *v = vip(p);

	if (!fair_task(p) || linked(v))
		return;
	if (!v->vip_entry.next && !v->vip_entry.prev)
		INIT_LIST_HEAD(&v->vip_entry);
	if (!test_task_vip(p) || READ_ONCE(p->__state))
		return;
	get_task_struct(p);
	list_add_tail(&v->vip_entry, vip_list(rq));
	if (!v->total_exec) {
		v->sum_exec_baseline = p->se.sum_exec_runtime;
		if (READ_ONCE(global_debug_enabled))
			trace_printk("[mvt] enqueue %s : %d, tsk->__state:0x%x, pid_alive(p):%d, p->cpu %d, mts->sum_exec_baseline: %ld\n",
				p->comm, p->pid, READ_ONCE(p->__state), pid_alive(p),
				task_cpu(p), v->sum_exec_baseline);
	}
}
static void dequeue_vip_task(struct rq *rq, struct task_struct *p)
{
	struct mz_task_struct *v = vip(p);

	if (linked(v)) {
		list_del_init(&v->vip_entry);
		if (READ_ONCE(global_debug_enabled))
			trace_printk("[mvt] dequeue %s : %d, p->on_cpu %d p->cpu:%d\n",
				p->comm, p->pid, p->on_cpu, task_cpu(p));
		/* The rq and scheduler own a task reference throughout this hook. */
		put_task_struct(p);
	}
	if (READ_ONCE(p->__state))
		v->total_exec = 0;
}
static void account_vip_runtime(struct rq *rq, struct task_struct *p)
{
	struct mz_task_struct *v = vip(p);
	s64 delta;

	update_rq_clock(rq);
	delta = p->se.sum_exec_runtime - v->sum_exec_baseline;
	if (READ_ONCE(global_debug_enabled))
		trace_printk("[mvt] vip task: %s, pid:%d, cpu:%d, delta:%ld, curr->se.sum_exec_runtime:%ld, mts->sum_exec_baseline:%ld\n",
			p->comm, p->pid, task_cpu(p), delta,
			p->se.sum_exec_runtime, v->sum_exec_baseline);
	if (delta < 0)
		delta = 0;
	else
		delta += rq_clock_task(rq) - p->se.exec_start;
	if (READ_ONCE(global_debug_enabled))
		trace_printk("[mvt] delta: %ld \n", delta);
	if (delta < 4000000)
		return;
	v->sum_exec_baseline += delta;
	v->total_exec += delta;
	if (READ_ONCE(global_debug_enabled))
		trace_printk("[mvt] vip task: %s, pid:%d, cpu:%d, total_exec:%ld\n",
			p->comm, p->pid, task_cpu(p), v->total_exec);
	if (v->total_exec > 8000000) {
		if (READ_ONCE(global_debug_enabled))
			trace_printk("[mvt] expire total_exec, vip task: %s, pid:%d, cpu:%d, total_exec:%ld\n",
				p->comm, p->pid, task_cpu(p), v->total_exec);
		list_del_init(&v->vip_entry);
		put_task_struct(p);
	} else {
		list_move_tail(&v->vip_entry, vip_list(rq));
		if (READ_ONCE(global_debug_enabled))
			trace_printk("%s, requeue to exec, vip task: %s, pid:%d, cpu:%d\n",
				"account_vip_runtime", p->comm, p->pid, task_cpu(p));
	}
}
void mz_check_preempt_wakeup(struct rq *rq, struct task_struct *p,
			   bool *preempt, bool *nopreempt)
{
	struct task_struct *curr = rq->curr;
	bool p_static, c_static;

	if (!feature(5) || !strcmp(curr->comm, "RenderThread"))
		return;
	p_static = static_vip(p);
	c_static = static_vip(curr);
	if (!p_static && !c_static)
		return;
	if (p_static && !c_static) {
		*preempt = true;
		if (READ_ONCE(global_debug_enabled))
			trace_printk("[mvt] vip wakeup preempt task: %s, pid: %d, state: 0x%x, curr task: %s, pid: %d, state: 0x%x, rq->cpu: %d\n",
				p->comm, p->pid, READ_ONCE(p->__state),
				curr->comm, curr->pid, READ_ONCE(curr->__state), cpu_of(rq));
		return;
	}
	if (p_static && c_static)
		return;
	*nopreempt = true;
	if (linked(vip(curr)))
		account_vip_runtime(rq, curr);
}
EXPORT_SYMBOL(mz_check_preempt_wakeup);
void mz_replace_next_task_fair(struct rq *rq, struct task_struct **p,
		struct sched_entity **se, bool *repick, bool simple)
{
	struct list_head *entry, *next;
	struct task_struct *candidate;

	if (!feature(4))
		return;
	list_for_each_safe(entry, next, vip_list(rq)) {
		candidate = vip_task(entry);
		if (candidate->on_rq != TASK_ON_RQ_QUEUED ||
		    task_rq(candidate) != rq || task_cpu(candidate) != cpu_of(rq) ||
		    !test_task_vip(candidate)) {
			list_del_init(entry);
			put_task_struct(candidate);
			continue;
		}
		*p = candidate;
		*se = &candidate->se;
		*repick = true;
		list_move_tail(entry, vip_list(rq));
		if (READ_ONCE(global_debug_enabled))
			trace_printk("[mvt] PICK vip task: %s, pid: %d, state: 0x%x, is_vip:%d, se->on_rq: %d, rq->cpu:%d, task cpu:%d\n",
				candidate->comm, candidate->pid, READ_ONCE(candidate->__state),
				test_task_vip(candidate), candidate->se.on_rq,
				cpu_of(rq), task_cpu(candidate));
		if (simple) {
			for (; *se; *se = (*se)->parent)
				set_next_entity(cfs_rq_of(*se), *se);
		}
		return;
	}
}
EXPORT_SYMBOL(mz_replace_next_task_fair);

static void mark_static(struct task_struct *p, bool enabled)
{
	WRITE_ONCE(vip(p)->is_vip, enabled);
	WRITE_ONCE(vip(p)->is_static_vip, enabled);
}
static void parse_whitelist(struct task_struct *p)
{
	struct task_struct *leader = p->group_leader;

	if (!strcmp(p->comm, "RenderThread") && static_vip(leader))
		mark_static(p, true);
	if (!strncmp(p->comm, "Thread-", 7) && leader &&
	    !strcmp(leader->comm, "tutu.ABenchMark"))
		mark_static(p, true);
}
static void fork_hook(void *unused, struct task_struct *p)
{
	init_task_vip(p);
}
static void finish_fork_hook(void *unused, struct task_struct *p)
{
	if (!strncmp(p->comm, "cbench_v8a", 10))
		mark_static(p, true);
}
static void prctl_hook(void *unused, int option, struct task_struct *p)
{
	if (option == PR_SET_NAME)
		parse_whitelist(p);
}
static void cgroup_hook(void *unused, int ret, struct task_struct *p)
{
	struct cgroup_subsys_state *css;
	struct task_struct *thread;
	bool top = false;

	if (ret || !p)
		return;
	if (!vip(p)->vip_entry.next && !vip(p)->vip_entry.prev)
		INIT_LIST_HEAD(&vip(p)->vip_entry);
	rcu_read_lock();
	css = task_css(p, cpu_cgrp_id);
	if (css && css->cgroup && css->cgroup->kn) {
		const char *name = rcu_dereference(css->cgroup->kn->name);

		top = name && !strcmp(name, "top-app");
	}
	mark_static(p, top);
	for_each_thread(p, thread) {
		if (!strcmp(thread->comm, "RenderThread"))
			mark_static(thread, top);
	}
	rcu_read_unlock();
	if (top)
		set_user_nice(p, -20);
}
static void enqueue_hook(void *unused, struct rq *rq, struct task_struct *p,
			 int flags)
{
	if (feature(4))
		enqueue_vip_task(rq, p);
}
static void dequeue_hook(void *unused, struct rq *rq, struct task_struct *p,
			 int flags)
{
	/* Disabling admission must still release every queued task reference. */
	dequeue_vip_task(rq, p);
}
static void tick_hook(void *unused, struct rq *rq)
{
	raw_spin_rq_lock(rq);
	if (linked(vip(rq->curr)))
		account_vip_runtime(rq, rq->curr);
	raw_spin_rq_unlock(rq);
}
static void mutex_hook(void *unused, struct mutex *lock,
		struct mutex_waiter *waiter, struct list_head *head, bool *added)
{
	struct mutex_waiter *other;
	struct list_head *before = head;
	int length = 0;

	if (!feature(1))
		return;
	*added = false;
	if (!lock || !waiter || !head || !static_vip(current))
		return;
	if (READ_ONCE(global_debug_enabled)) {
		trace_printk("[mvt mutex] comm:%s, pid:%d, cpu:%d\n",
			current->comm, current->pid, task_cpu(current));
		list_for_each_entry(other, head, list)
			length++;
		trace_printk("[mvt mutex] mutex list length: %d\n", length);
	}
	list_for_each_entry(other, head, list) {
		if (!static_vip(other->task)) {
			before = &other->list;
			break;
		}
	}
	list_add_tail(&waiter->list, before);
	*added = true;
}
static void rwsem_hook(void *unused, struct rwsem_waiter *waiter,
		struct rw_semaphore *sem, bool *added)
{
	struct rwsem_waiter *other;
	struct list_head *head = &sem->wait_list;
	struct list_head *before = head;
	int length = 0;

	if (!feature(2) || !waiter || !static_vip(waiter->task))
		return;
	if (READ_ONCE(global_debug_enabled)) {
		trace_printk("[mvt rwsem] comm:%s, pid:%d, cpu:%d\n",
			waiter->task->comm, waiter->task->pid, task_cpu(waiter->task));
		list_for_each_entry(other, head, list)
			length++;
		trace_printk("[mvt rwsem] rwsem list length: %d\n", length);
	}
	list_for_each_entry(other, head, list) {
		if (!static_vip(other->task)) {
			before = &other->list;
			break;
		}
	}
	list_add_tail(&waiter->list, before);
	*added = true;
}
static void migrate_hook(void *unused, struct task_struct *p, int dst,
			 int *can_migrate)
{
	if (feature(6) && !static_vip(p) && fair_task(p) &&
	    dst >= 0 && dst < nr_cpu_ids &&
	    !list_empty_careful(vip_list(cpu_rq(dst)))) {
		*can_migrate = 0;
		if (READ_ONCE(global_debug_enabled))
			trace_printk("[mvt] non-vip task=%-12s pid=%d can not migrate vip_cpu=%d\n",
				p->comm, p->pid, dst);
	}
}

static ssize_t debug_read(struct file *file, char __user *buf, size_t count,
			  loff_t *pos)
{
	char text[30];
	int len = scnprintf(text, sizeof(text), "debug_enabled: %s\n",
		READ_ONCE(global_debug_enabled) ? "TRUE" : "FALSE");

	return simple_read_from_buffer(buf, count, pos, text, len);
}
static ssize_t enabled_read(struct file *file, char __user *buf, size_t count,
			    loff_t *pos)
{
	char text[300];
	unsigned long mask = READ_ONCE(global_vip_task_enabled);
	int len = scnprintf(text, sizeof(text),
		"\n\nvip task feature list:\nFEATURE_MUTEX     %s\n"
		"FEATURE_RWSEM     %s\nFEATURE_BINDER    %s\n"
		"FEATURE_SCHED     %s\nFEATURE_PREEMPT   %s\n"
		"FEATURE_MIGRATE   %s\n\n\nenabled: 0x%x\n",
		mask & BIT(1) ? "TRUE" : "FALSE",
		mask & BIT(2) ? "TRUE" : "FALSE",
		mask & BIT(3) ? "TRUE" : "FALSE",
		mask & BIT(4) ? "TRUE" : "FALSE",
		mask & BIT(5) ? "TRUE" : "FALSE",
		mask & BIT(6) ? "TRUE" : "FALSE", (unsigned int)mask);

	return simple_read_from_buffer(buf, count, pos, text, len);
}
static ssize_t mask_write(const char __user *buf, size_t count, bool debug)
{
	char text[20] = {};
	size_t copied = min(count, sizeof(text) - 1);
	int value, ret;

	if (copy_from_user(text, buf, copied))
		return -EFAULT;
	ret = kstrtoint(strim(text), debug ? 10 : 16, &value);
	if (ret)
		return ret;
	if (debug)
		WRITE_ONCE(global_debug_enabled, value);
	else
		WRITE_ONCE(global_vip_task_enabled, (long)value);
	return copied;
}
static ssize_t debug_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *pos)
{
	return mask_write(buf, count, true);
}
static ssize_t enabled_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	return mask_write(buf, count, false);
}
static ssize_t tasks_read(struct file *file, char __user *buf, size_t count,
			  loff_t *pos)
{
	char *text;
	struct task_struct *group, *thread;
	int len = 0;
	ssize_t ret;

	text = kzalloc(2000, GFP_KERNEL);
	if (!text)
		return -ENOMEM;
	rcu_read_lock();
	for_each_process_thread(group, thread) {
		if (test_task_vip(thread))
			len += scnprintf(text + len, 2000 - len,
				"vip task: %s pid: %d\n", thread->comm,
				task_pid_nr(thread));
	}
	rcu_read_unlock();
	ret = simple_read_from_buffer(buf, count, pos, text, len);
	kfree(text);
	return ret;
}
static ssize_t tasks_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *pos)
{
	char text[20] = {}, pid_text[8] = "0", value[8] = "0";
	char *cursor, *word;
	struct task_struct *p;
	size_t copied;
	int pid, ret;

	if (count < 2 || count > 20)
		return -EINVAL;
	copied = min(count, sizeof(text) - 1);
	if (copy_from_user(text, buf, copied))
		return -EFAULT;
	cursor = strim(text);
	word = strsep(&cursor, " ");
	if (word && *word)
		strscpy(pid_text, word, sizeof(pid_text));
	word = strsep(&cursor, " ");
	if (word && *word)
		strscpy(value, word, sizeof(value));
	ret = kstrtoint(pid_text, 10, &pid);
	if (ret)
		return ret;
	if (pid < 0 || pid > 32768)
		return -EINVAL;
	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (p)
		get_task_struct(p);
	rcu_read_unlock();
	if (p) {
		if (!strncmp(value, "true", 4))
			WRITE_ONCE(vip(p)->is_vip, true);
		else if (!strncmp(value, "false", 5))
			WRITE_ONCE(vip(p)->is_vip, false);
		put_task_struct(p);
	}
	return copied;
}
static const struct proc_ops debug_ops = {
	.proc_read = debug_read, .proc_write = debug_write,
};
static const struct proc_ops enabled_ops = {
	.proc_read = enabled_read, .proc_write = enabled_write,
};
static const struct proc_ops tasks_ops = {
	.proc_read = tasks_read, .proc_write = tasks_write,
};

static int __init mz_vip_init(void)
{
	struct proc_dir_entry *root, *dir;
	struct task_struct *group, *thread;
	int cpu, ret;

	for_each_possible_cpu(cpu)
		INIT_LIST_HEAD(vip_list(cpu_rq(cpu)));
	/* Hook storage initialization before walking existing tasks; forked tasks
	 * then receive independent list heads instead of copied parent links.
	 */
	ret = register_trace_android_rvh_sched_fork(fork_hook, NULL);
	if (ret)
		return ret;
	rcu_read_lock();
	for_each_process_thread(group, thread) {
		if (!vip(thread)->vip_entry.next && !vip(thread)->vip_entry.prev)
			init_task_vip(thread);
	}
	rcu_read_unlock();
	WRITE_ONCE(global_vip_task_enabled, 0x76);
	root = proc_mkdir("mz_scheduler", NULL);
	dir = root ? proc_mkdir("vip_task", root) :
		proc_mkdir("mz_scheduler/vip_task", NULL);
	if (!dir)
		return -ENOMEM;
	if (!proc_create("debug_enabled", 0666, dir, &debug_ops) ||
	    !proc_create("enabled", 0666, dir, &enabled_ops) ||
	    !proc_create("vip_task", 0666, dir, &tasks_ops)) {
		proc_remove(dir);
		return -ENOMEM;
	}
#define VIP_REGISTER(name, callback) do { \
	ret = register_trace_##name(callback, NULL); \
	if (ret) { \
		pr_err("Meizu VIP hook %s failed: %d\n", #name, ret); \
		return ret; \
	} \
} while (0)
	VIP_REGISTER(android_vh_alter_mutex_list_add, mutex_hook);
	VIP_REGISTER(android_vh_cgroup_set_task, cgroup_hook);
	VIP_REGISTER(android_rvh_dequeue_task, dequeue_hook);
	VIP_REGISTER(android_rvh_enqueue_task, enqueue_hook);
	VIP_REGISTER(android_vh_scheduler_tick, tick_hook);
	VIP_REGISTER(android_rvh_finish_prio_fork, finish_fork_hook);
	VIP_REGISTER(android_vh_alter_rwsem_list_add, rwsem_hook);
	VIP_REGISTER(android_rvh_can_migrate_task, migrate_hook);
	VIP_REGISTER(android_vh_syscall_prctl_finished, prctl_hook);
#undef VIP_REGISTER
	return 0;
}
late_initcall(mz_vip_init);
MODULE_DESCRIPTION("Meizu VIP task scheduling");
MODULE_LICENSE("GPL");
