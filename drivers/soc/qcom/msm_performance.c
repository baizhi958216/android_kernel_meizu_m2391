// SPDX-License-Identifier: GPL-2.0-only
/* Graphics frame events consumed by the Qualcomm performance HAL. */
#include <linux/circ_buf.h>
#include <linux/kobject.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/wait.h>
#include <soc/qcom/msm_performance.h>

struct gfx_event {
	pid_t pid;
	u32 ctx_id;
	u32 timestamp;
	enum gfx_evt_t type;
	s64 time_us;
};

static struct kobject *perf_kobj;
static DEFINE_SPINLOCK(event_lock);
static DECLARE_WAIT_QUEUE_HEAD(event_wait);
static struct gfx_event events[512];
static unsigned int head, tail;
static pid_t game_pid;
static bool stopping;

void msm_perf_events_update(enum evt_update_t group, enum gfx_evt_t type, pid_t pid,
			    u32 ctx_id, u32 timestamp, bool end_of_frame)
{
	unsigned long flags;

	if (group != MSM_PERF_GFX || !end_of_frame || !timestamp)
		return;
	spin_lock_irqsave(&event_lock, flags);
	if (pid == game_pid && !stopping) {
		if (!CIRC_SPACE(head, tail, ARRAY_SIZE(events)))
			tail = (tail + 1) % ARRAY_SIZE(events);
		events[head] = (struct gfx_event) {
			.pid = pid, .ctx_id = ctx_id, .timestamp = timestamp,
			.type = type, .time_us = ktime_to_us(ktime_get()),
		};
		head = (head + 1) % ARRAY_SIZE(events);
		if (type == MSM_PERF_QUEUE || type == MSM_PERF_RETIRED)
			wake_up_interruptible(&event_wait);
	}
	spin_unlock_irqrestore(&event_lock, flags);
}
EXPORT_SYMBOL_GPL(msm_perf_events_update);

static ssize_t evnt_gplaf_pid_show(struct kobject *kobj, struct kobj_attribute *attr,
				   char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(game_pid));
}

static ssize_t evnt_gplaf_pid_store(struct kobject *kobj, struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	unsigned long flags;
	int pid, ret = kstrtoint(buf, 0, &pid);

	if (ret)
		return ret;
	spin_lock_irqsave(&event_lock, flags);
	if (pid != game_pid) {
		WRITE_ONCE(game_pid, pid);
		tail = head;
	}
	spin_unlock_irqrestore(&event_lock, flags);
	return count;
}

static ssize_t gfx_evt_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	unsigned long flags;
	ssize_t len = 0;
	int ret;

	ret = wait_event_interruptible(event_wait, READ_ONCE(head) != READ_ONCE(tail) ||
				       READ_ONCE(stopping));
	if (ret)
		return ret;
	spin_lock_irqsave(&event_lock, flags);
	/* Leave room for the longest record. Unread events stay in the queue. */
	while (head != tail && PAGE_SIZE - len > 80) {
		const struct gfx_event *ev = &events[tail];

		len += sysfs_emit_at(buf, len, "%d %d %u %d %lld :", ev->pid,
				     (int)ev->ctx_id, ev->timestamp, ev->type, ev->time_us);
		tail = (tail + 1) % ARRAY_SIZE(events);
	}
	spin_unlock_irqrestore(&event_lock, flags);
	return len;
}

static struct kobj_attribute evnt_gplaf_pid_attr = __ATTR_RW(evnt_gplaf_pid);
static struct kobj_attribute gfx_evt_attr = __ATTR_RO(gfx_evt);
static struct attribute *parameters_attrs[] = { &evnt_gplaf_pid_attr.attr, NULL };
static struct attribute *notify_attrs[] = { &gfx_evt_attr.attr, NULL };
static const struct attribute_group parameters = { .name = "parameters", .attrs = parameters_attrs };
static const struct attribute_group notify = { .name = "notify", .attrs = notify_attrs };
static const struct attribute_group *groups[] = { &parameters, &notify, NULL };

static int __init msm_perf_init(void)
{
	int ret;

	perf_kobj = kobject_create_and_add("msm_performance", kernel_kobj);
	if (!perf_kobj)
		return -ENOMEM;
	ret = sysfs_create_groups(perf_kobj, groups);
	if (ret)
		kobject_put(perf_kobj);
	return ret;
}

static void __exit msm_perf_exit(void)
{
	WRITE_ONCE(stopping, true);
	wake_up_interruptible_all(&event_wait);
	sysfs_remove_groups(perf_kobj, groups);
	kobject_put(perf_kobj);
}
module_init(msm_perf_init);
module_exit(msm_perf_exit);

MODULE_DESCRIPTION("Qualcomm graphics frame events");
MODULE_LICENSE("GPL");
