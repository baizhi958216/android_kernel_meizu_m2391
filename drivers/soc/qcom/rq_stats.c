// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2015, 2017, 2019-2021, The Linux Foundation.
 */

#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <trace/hooks/sched.h>

static struct kobject *rq_kobj;
static DEFINE_RAW_SPINLOCK(timer_lock);
static unsigned long interval_jiffies = 5;
static unsigned long last_jiffy;
static ktime_t start_time;

static void notify_pollers(struct work_struct *work)
{
	sysfs_notify(rq_kobj, NULL, "def_timer_ms");
}

static DECLARE_WORK(notify_work, notify_pollers);

static void jiffies_update(void *data, void *unused)
{
	unsigned long flags, now;
	bool notify;

	raw_spin_lock_irqsave(&timer_lock, flags);
	now = jiffies;
	notify = now - last_jiffy >= interval_jiffies;
	if (notify)
		last_jiffy = now;
	raw_spin_unlock_irqrestore(&timer_lock, flags);

	if (notify)
		schedule_work(&notify_work);
}

static ssize_t def_timer_ms_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	unsigned long flags;
	ktime_t start;

	raw_spin_lock_irqsave(&timer_lock, flags);
	start = start_time;
	raw_spin_unlock_irqrestore(&timer_lock, flags);

	return sysfs_emit(buf, "%u\n", (u32)ktime_ms_delta(ktime_get(), start));
}

static ssize_t def_timer_ms_store(struct kobject *kobj,
				  struct kobj_attribute *attr, const char *buf,
				  size_t count)
{
	unsigned long flags;
	unsigned int ms;

	if (kstrtouint(buf, 0, &ms))
		return -EINVAL;

	raw_spin_lock_irqsave(&timer_lock, flags);
	interval_jiffies = msecs_to_jiffies(ms);
	start_time = ktime_get();
	raw_spin_unlock_irqrestore(&timer_lock, flags);

	return count;
}

static struct kobj_attribute def_timer_ms_attr =
	__ATTR(def_timer_ms, 0600, def_timer_ms_show, def_timer_ms_store);

static int __init rq_stats_init(void)
{
	struct device *cpu = get_cpu_device(0);
	int ret;

	if (!cpu)
		return -ENODEV;

	rq_kobj = kobject_create_and_add("rq-stats", &cpu->kobj);
	if (!rq_kobj)
		return -ENOMEM;

	ret = sysfs_create_file(rq_kobj, &def_timer_ms_attr.attr);
	if (ret)
		goto put_kobj;

	ret = register_trace_android_vh_jiffies_update(jiffies_update, NULL);
	if (ret)
		goto remove_file;

	kobject_uevent(rq_kobj, KOBJ_ADD);
	return 0;

remove_file:
	sysfs_remove_file(rq_kobj, &def_timer_ms_attr.attr);
put_kobj:
	kobject_put(rq_kobj);
	return ret;
}
module_init(rq_stats_init);

static void __exit rq_stats_exit(void)
{
	unregister_trace_android_vh_jiffies_update(jiffies_update, NULL);
	tracepoint_synchronize_unregister();
	cancel_work_sync(&notify_work);
	sysfs_remove_file(rq_kobj, &def_timer_ms_attr.attr);
	kobject_put(rq_kobj);
}
module_exit(rq_stats_exit);

MODULE_DESCRIPTION("Qualcomm deferred tick notifications for IRQ balancing");
MODULE_LICENSE("GPL");
