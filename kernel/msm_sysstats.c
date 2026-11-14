// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Qualcomm SYSSTATS v2 request/reply ABI for product performance clients.
 */
#include <linux/cgroup.h>
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <linux/module.h>
#include <linux/msm_sysstats.h>
#include <linux/pid_namespace.h>
#include <linux/sched/cputime.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/swap.h>
#include <linux/vmalloc.h>
#include <net/genetlink.h>

static struct genl_family family;
static DECLARE_RWSEM(gpu_lock);
static u64 (*gpu_bytes)(pid_t);

void sysstats_register_kgsl_stats_cb(u64 (*cb)(pid_t))
{
	guard(rwsem_write)(&gpu_lock);
	gpu_bytes = cb;
}
EXPORT_SYMBOL_GPL(sysstats_register_kgsl_stats_cb);

void sysstats_unregister_kgsl_stats_cb(void)
{
	guard(rwsem_write)(&gpu_lock);
	gpu_bytes = NULL;
}
EXPORT_SYMBOL_GPL(sysstats_unregister_kgsl_stats_cb);

static u64 kgsl_bytes(pid_t pid)
{
	guard(rwsem_read)(&gpu_lock);
	return gpu_bytes ? gpu_bytes(pid) : 0;
}

static int count_dma_fd(const void *total, struct file *file, unsigned int fd)
{
	struct dma_buf *buf;
	static const char * const heaps[] = {
		"qcom,system", "qcom,system-uncached", "system-secure",
		"qcom,secure-pixel", "qcom,secure-non-pixel",
	};

	if (!is_dma_buf_file(file))
		return 0;
	buf = file->private_data;
	if (buf->exp_name && match_string(heaps, ARRAY_SIZE(heaps), buf->exp_name) >= 0)
		*(u64 *)total += buf->size;
	return 0;
}

static u64 task_dma_bytes(struct task_struct *task)
{
	struct task_struct *leader = task->group_leader;
	struct task_struct *thread;
	u64 bytes = 0;

	rcu_read_lock();
	for_each_thread(leader, thread) {
		task_lock(thread);
		/* Threads normally share their leader's descriptor table. */
		if (thread->files && (thread == leader ||
		    thread->files != READ_ONCE(leader->files)))
			iterate_fd(thread->files, 0, count_dma_fd, &bytes);
		task_unlock(thread);
	}
	rcu_read_unlock();
	return bytes;
}

/* The cursor advances past non-leader PIDs, but a caller with a full skb can
 * retry its current leader on the next dump invocation.
 */
static struct task_struct *next_leader(struct pid_namespace *ns, int *cursor)
{
	struct task_struct *task;
	struct pid *pid;

	guard(rcu)();
	while ((pid = find_ge_pid(*cursor, ns))) {
		*cursor = pid_nr_ns(pid, ns);
		task = get_pid_task(pid, PIDTYPE_TGID);
		if (task)
			return task;
		(*cursor)++;
	}
	return NULL;
}

static u64 pages_kb(unsigned long pages)
{
	return (u64)pages << (PAGE_SHIFT - 10);
}

static void fill_rss(struct sysstats_task *stats, struct task_struct *p)
{
	struct mm_struct *mm = get_task_mm(p);

	if (mm) {
		stats->anon_rss = pages_kb(get_mm_counter(mm, MM_ANONPAGES));
		stats->file_rss = pages_kb(get_mm_counter(mm, MM_FILEPAGES));
		stats->shmem_rss = pages_kb(get_mm_counter(mm, MM_SHMEMPAGES));
		stats->swap_rss = pages_kb(get_mm_counter(mm, MM_SWAPENTS));
		mmput(mm);
	}
	stats->unreclaimable = task_dma_bytes(p) >> 10;
}

static void fill_task(struct sysstats_task *stats, struct task_struct *p)
{
	u64 user, system;
	unsigned int seq;
	char comm[TASK_COMM_LEN];

	fill_rss(stats, p);
	stats->pid = task_pid_vnr(p);
	stats->unreclaimable += kgsl_bytes(task_tgid_nr(p)) >> 10;
	task_cputime_adjusted(p, &user, &system);
	stats->utime = div_u64(user, NSEC_PER_USEC);
	stats->stime = div_u64(system, NSEC_PER_USEC);
	/* v2 passes the signal's cumulative child times in their native units. */
	do {
		seq = read_seqbegin(&p->signal->stats_lock);
		stats->cutime = p->signal->cutime;
		stats->cstime = p->signal->cstime;
	} while (read_seqretry(&p->signal->stats_lock, seq));
	stats->uid = from_kuid_munged(current_user_ns(), task_uid(p));
	stats->ppid = task_ppid_nr_ns(p, task_active_pid_ns(current));
	get_task_comm(comm, p);
	strscpy(stats->name, comm, sizeof(stats->name));
#ifdef CONFIG_CPUSETS
	{
		struct cgroup_subsys_state *css = task_get_css(p, cpuset_cgrp_id);

		cgroup_path_ns(css->cgroup, stats->state, sizeof(stats->state),
			       current->nsproxy->cgroup_ns);
		css_put(css);
	}
#endif
}

static void add_zone_stats(struct zone *zone, __u64 *free, __u64 *active_anon,
			   __u64 *inactive_anon, __u64 *active_file, __u64 *inactive_file)
{
	*free += pages_kb(zone_page_state(zone, NR_FREE_PAGES));
	*active_anon += pages_kb(zone_page_state(zone, NR_ZONE_ACTIVE_ANON));
	*inactive_anon += pages_kb(zone_page_state(zone, NR_ZONE_INACTIVE_ANON));
	*active_file += pages_kb(zone_page_state(zone, NR_ZONE_ACTIVE_FILE));
	*inactive_file += pages_kb(zone_page_state(zone, NR_ZONE_INACTIVE_FILE));
}

/* The v2 layout contains these five named fields for each zone. */
#define ADD_ZONE_STATS(s, name, z) \
	add_zone_stats(z, &(s)->name##_nr_free, &(s)->name##_nr_active_anon, \
		       &(s)->name##_nr_inactive_anon, &(s)->name##_nr_active_file, \
		       &(s)->name##_nr_inactive_file)

static void fill_memory(struct sysstats_mem *stats)
{
	struct sysinfo info;
	struct task_struct *p;
	int cursor = 1, nid, i;
	u64 dma = 0;

	si_meminfo(&info);
	si_swapinfo(&info);
	stats->memtotal = pages_kb(info.totalram);
	stats->misc_reclaimable = pages_kb(global_node_page_state(NR_KERNEL_MISC_RECLAIMABLE));
	stats->buffer = pages_kb(info.bufferram);
	stats->swap_used = pages_kb(info.totalswap - info.freeswap);
	stats->swap_total = pages_kb(info.totalswap);
	stats->vmalloc_total = pages_kb(vmalloc_nr_pages());
	stats->swapcache = pages_kb(total_swapcache_pages());
	stats->slab_reclaimable = pages_kb(global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B));
	stats->slab_unreclaimable = pages_kb(global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B));
	stats->free_cma = pages_kb(global_zone_page_state(NR_FREE_CMA_PAGES));
	stats->file_mapped = pages_kb(global_node_page_state(NR_FILE_MAPPED));
	stats->kernelstack = global_node_page_state(NR_KERNEL_STACK_KB);
	stats->pagetable = pages_kb(global_node_page_state(NR_PAGETABLE));
	stats->shmem = pages_kb(info.sharedram);
#if IS_ENABLED(CONFIG_ZSMALLOC)
	stats->zram_compressed = pages_kb(global_zone_page_state(NR_ZSPAGES));
#endif
	while ((p = next_leader(&init_pid_ns, &cursor))) {
		dma += task_dma_bytes(p);
		put_task_struct(p);
		cursor++;
		cond_resched();
	}
	stats->unreclaimable = (dma + kgsl_bytes(-1)) >> 10;

	for_each_online_node(nid) {
		for (i = 0; i < MAX_NR_ZONES; i++) {
			struct zone *z = &NODE_DATA(nid)->node_zones[i];

			if (!populated_zone(z))
				continue;
			if (!strcmp(z->name, "Normal"))
				ADD_ZONE_STATS(stats, normal, z);
			else if (!strcmp(z->name, "Movable"))
				ADD_ZONE_STATS(stats, movable, z);
			else if (!strcmp(z->name, "DMA"))
				ADD_ZONE_STATS(stats, dma, z);
			else if (!strcmp(z->name, "HighMem"))
				ADD_ZONE_STATS(stats, highmem, z);
		}
	}
}

static int sysstats_query(struct sk_buff *request, struct genl_info *info)
{
	bool task = info->genlhdr->cmd == SYSSTATS_TASK_CMD_GET;
	size_t size = task ? sizeof(struct sysstats_task) : sizeof(struct sysstats_mem);
	struct sk_buff *skb;
	struct nlattr *attr;
	struct task_struct *p = NULL;
	void *hdr;
	int ret = -EMSGSIZE;

	if (task) {
		if (!info->attrs[SYSSTATS_TASK_CMD_ATTR_PID])
			return -EINVAL;
		rcu_read_lock();
		p = get_pid_task(find_vpid(nla_get_u32(info->attrs[SYSSTATS_TASK_CMD_ATTR_PID])),
				 PIDTYPE_PID);
		rcu_read_unlock();
		if (!p)
			return -ESRCH;
	}
	skb = genlmsg_new(nla_total_size_64bit(size), GFP_KERNEL);
	if (!skb) {
		ret = -ENOMEM;
		goto put;
	}
	hdr = genlmsg_put_reply(skb, info, &family, 0,
				task ? SYSSTATS_TASK_CMD_NEW : SYSSTATS_MEMINFO_CMD_NEW);
	if (!hdr)
		goto free;
	attr = nla_reserve_64bit(skb, task ? SYSSTATS_TASK_TYPE_STATS : SYSSTATS_MEMINFO_TYPE_STATS,
				 size, SYSSTATS_TYPE_NULL);
	if (!attr)
		goto free;
	memset(nla_data(attr), 0, size);
	if (task)
		fill_task(nla_data(attr), p);
	else
		fill_memory(nla_data(attr));
	genlmsg_end(skb, hdr);
	ret = genlmsg_reply(skb, info);
	goto put;
free:
	nlmsg_free(skb);
put:
	if (p)
		put_task_struct(p);
	return ret;
}

static int sysstats_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	bool names = info->genlhdr->cmd == SYSSTATS_PIDS_CMD_GET;
	struct nlattr *query = info->attrs[names ? SYSSTATS_TASK_CMD_ATTR_PIDS_OF_NAME :
					   SYSSTATS_TASK_CMD_ATTR_FOREACH];
	int cursor = max_t(int, cb->args[0], 1);
	struct task_struct *p;
	char comm[TASK_COMM_LEN];
	s16 score, low = 0, high = 0;
	size_t size = names ? sizeof(struct sysstats_pid) : sizeof(struct sysstats_task);

	if (!query)
		return -EINVAL;
	if (!names) {
		u32 range = nla_get_u32(query);

		low = (s16)range;
		high = (s16)(range >> 16);
	}
	while ((p = next_leader(task_active_pid_ns(current), &cursor))) {
		void *hdr;
		struct nlattr *attr;

		score = READ_ONCE(p->signal->oom_score_adj);
		if (names) {
			get_task_comm(comm, p);
			if (strcmp(comm, nla_data(query)))
				goto advance;
		} else if ((p->flags & PF_KTHREAD) || score < low || score > high) {
			goto advance;
		}
		hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
				  &family, NLM_F_MULTI, info->genlhdr->cmd);
		if (!hdr)
			break;
		attr = nla_reserve_64bit(skb, names ? SYSSTATS_PID_TYPE_STATS :
					 SYSSTATS_TASK_TYPE_FOREACH, size, SYSSTATS_TYPE_NULL);
		if (!attr) {
			genlmsg_cancel(skb, hdr);
			break;
		}
		memset(nla_data(attr), 0, size);
		if (names) {
			struct sysstats_pid *stats = nla_data(attr);

			stats->pid = cursor;
		} else {
			struct sysstats_task *stats = nla_data(attr);

			stats->pid = cursor;
			stats->oom_score = score;
			fill_rss(stats, p);
		}
		genlmsg_end(skb, hdr);
advance:
		put_task_struct(p);
		cursor++;
		cond_resched();
	}
	if (p)
		put_task_struct(p);
	cb->args[0] = cursor;
	return skb->len;
}

static const struct nla_policy policy[] = {
	[SYSSTATS_TASK_CMD_ATTR_PID] = { .type = NLA_U32 },
	[SYSSTATS_TASK_CMD_ATTR_FOREACH] = { .type = NLA_U32 },
	[SYSSTATS_TASK_CMD_ATTR_PIDS_OF_NAME] = { .type = NLA_NUL_STRING, .len = TS_COMM_LEN },
};

static const struct genl_ops ops[] = {
	{ .cmd = SYSSTATS_TASK_CMD_GET, .doit = sysstats_query, .dumpit = sysstats_dump },
	{ .cmd = SYSSTATS_MEMINFO_CMD_GET, .doit = sysstats_query },
	{ .cmd = SYSSTATS_PIDS_CMD_GET, .dumpit = sysstats_dump },
};

static struct genl_family family __ro_after_init = {
	.name = SYSSTATS_GENL_NAME, .version = SYSSTATS_GENL_VERSION,
	.maxattr = ARRAY_SIZE(policy) - 1, .policy = policy,
	.module = THIS_MODULE, .ops = ops, .n_ops = ARRAY_SIZE(ops),
};

static int __init sysstats_init(void)
{
	return genl_register_family(&family);
}

static void __exit sysstats_exit(void)
{
	genl_unregister_family(&family);
}

module_init(sysstats_init);
module_exit(sysstats_exit);
MODULE_IMPORT_NS("MINIDUMP");
MODULE_IMPORT_NS("DMA_BUF");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm SYSSTATS v2 process and memory statistics");
