// SPDX-License-Identifier: GPL-2.0-only
/* Graphics frame events consumed by the Qualcomm performance HAL. */
#include <linux/circ_buf.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpuhotplug.h>
#include <linux/math64.h>
#include <linux/sched/topology.h>
#include <soc/qcom/pmu_lib.h>
#include <linux/meizu_thermal_boost.h>
#include <linux/slab.h>
#include <linux/scmi_plh.h>
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
/* PLH clients hold a module reference while their attributes are registered. */
struct kobject *msm_perf_kobject(void)
{
	return perf_kobj;
}
EXPORT_SYMBOL_GPL(msm_perf_kobject);

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

#ifdef CONFIG_CPU_FREQ
struct perf_limit {
	struct freq_qos_request request[2];
	struct cpufreq_policy *policy;
	s32 value[2];
};

enum perf_constraint { MINIMUM, MAXIMUM };
static struct perf_limit limits[NR_CPUS];
static DEFINE_MUTEX(limits_lock);

static void remove_limits(struct perf_limit *limit)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(limit->request); i++)
		if (freq_qos_request_active(&limit->request[i]))
			freq_qos_remove_request(&limit->request[i]);
	limit->policy = NULL;
}

static int bind_limits(unsigned int cpu)
{
	struct perf_limit *limit = &limits[cpu];
	struct cpufreq_policy *policy;
	int member, ret;

	if (limit->policy)
		return 0;
	/* An offline CPU can still share an active frequency domain. */
	for_each_online_cpu(member) {
		policy = cpufreq_cpu_get(member);
		if (!policy)
			continue;
		if (cpumask_test_cpu(cpu, policy->related_cpus))
			goto found;
		cpufreq_cpu_put(policy);
	}
	return -EAGAIN;
found:
	ret = freq_qos_add_request(&policy->constraints, &limit->request[MINIMUM],
				   FREQ_QOS_MIN, FREQ_QOS_MIN_DEFAULT_VALUE);
	if (ret >= 0)
		ret = freq_qos_add_request(&policy->constraints, &limit->request[MAXIMUM],
					   FREQ_QOS_MAX, FREQ_QOS_MAX_DEFAULT_VALUE);
	if (ret < 0) {
		remove_limits(limit);
	} else {
		limit->policy = policy;
		limit->value[MINIMUM] = FREQ_QOS_MIN_DEFAULT_VALUE;
		limit->value[MAXIMUM] = FREQ_QOS_MAX_DEFAULT_VALUE;
	}
	cpufreq_cpu_put(policy);
	return ret < 0 ? ret : 0;
}

/* cpufreq drops the policy from its lookup table before this event. */
static int limits_policy_event(struct notifier_block *nb, unsigned long event, void *data)
{
	int cpu;

	if (event == CPUFREQ_REMOVE_POLICY) {
		mutex_lock(&limits_lock);
		for_each_possible_cpu(cpu)
			if (limits[cpu].policy == data)
				remove_limits(&limits[cpu]);
		mutex_unlock(&limits_lock);
	}
	return NOTIFY_OK;
}
static struct notifier_block limits_nb = { .notifier_call = limits_policy_event };

static ssize_t show_limits(char *buf, enum perf_constraint kind)
{
	ssize_t len = 0;
	int cpu;
	s32 value;

	mutex_lock(&limits_lock);
	for_each_present_cpu(cpu) {
		value = limits[cpu].policy ? limits[cpu].value[kind] :
			kind == MINIMUM ? FREQ_QOS_MIN_DEFAULT_VALUE : FREQ_QOS_MAX_DEFAULT_VALUE;
		len += sysfs_emit_at(buf, len, "%d:%d ", cpu, value);
	}
	mutex_unlock(&limits_lock);
	return len + sysfs_emit_at(buf, len, "\n");
}

static int apply_limit(struct perf_limit *limit, enum perf_constraint kind, s32 value)
{
	int ret;

	if (kind == MINIMUM && IS_REACHABLE(CONFIG_MEIZU_THERMAL_BOOST))
		mz_thermal_boost_request(limit->policy, value);
	ret = freq_qos_update_request(&limit->request[kind], value);
	if (ret >= 0) {
		limit->value[kind] = value;
		if (kind == MINIMUM && IS_REACHABLE(CONFIG_MEIZU_THERMAL_BOOST))
			mz_thermal_boost_restore(limit->policy, value);
	}
	return ret < 0 ? ret : 0;
}

struct perf_change { s32 requested, previous; };

static ssize_t store_limits(const char *buf, size_t count, enum perf_constraint kind)
{
	struct perf_change *changes __free(kfree) = NULL;
	char *input __free(kfree) = kstrndup(buf, count, GFP_KERNEL);
	char *cursor = input, *token, *freq;
	cpumask_t specified;
	unsigned int cpu, value;
	int ret;

	if (!input)
		return -ENOMEM;
	changes = kcalloc(nr_cpu_ids, sizeof(*changes), GFP_KERNEL);
	if (!changes)
		return -ENOMEM;
	cpumask_clear(&specified);
	while ((token = strsep(&cursor, " \t\r\n"))) {
		if (!*token)
			continue;
		freq = strchr(token, ':');
		if (!freq)
			return -EINVAL;
		*freq++ = '\0';
		if (kstrtouint(token, 10, &cpu) || cpu >= nr_cpu_ids || !cpu_present(cpu) ||
		    kstrtouint(freq, 10, &value))
			return -EINVAL;
		/* The stock HAL also represents the default with unsigned -1. */
		if (value == UINT_MAX)
			value = kind == MINIMUM ? FREQ_QOS_MIN_DEFAULT_VALUE : FREQ_QOS_MAX_DEFAULT_VALUE;
		if (value > S32_MAX)
			return -ERANGE;
		changes[cpu].requested = value;
		cpumask_set_cpu(cpu, &specified);
	}
	if (cpumask_empty(&specified))
		return -EINVAL;

	guard(mutex)(&limits_lock);
	for_each_cpu(cpu, &specified) {
		ret = bind_limits(cpu);
		if (ret)
			return ret;
		changes[cpu].previous = limits[cpu].value[kind];
	}
	for_each_cpu(cpu, &specified) {
		ret = apply_limit(&limits[cpu], kind, changes[cpu].requested);
		if (ret) {
			unsigned int done;

			for_each_cpu(done, &specified) {
				if (done == cpu)
					break;
				apply_limit(&limits[done], kind, changes[done].previous);
			}
			return ret;
		}
	}
	return count;
}

static ssize_t cpu_min_freq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return show_limits(buf, MINIMUM);
}
static ssize_t cpu_max_freq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return show_limits(buf, MAXIMUM);
}
static ssize_t cpu_min_freq_store(struct kobject *kobj, struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	return store_limits(buf, count, MINIMUM);
}
static ssize_t cpu_max_freq_store(struct kobject *kobj, struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	return store_limits(buf, count, MAXIMUM);
}
static struct kobj_attribute cpu_min_freq_attr = __ATTR_RW(cpu_min_freq);
static struct kobj_attribute cpu_max_freq_attr = __ATTR_RW(cpu_max_freq);
#endif

static int hotplug_state;
static DEFINE_MUTEX(instruction_lock);
struct pmu_counts {
	u64 instructions;
	u64 cycles;
};
static DEFINE_PER_CPU(struct pmu_counts, previous);

static int monitor_hotplug(unsigned int cpu)
{
	mutex_lock(&instruction_lock);
	per_cpu(previous, cpu) = (struct pmu_counts){};
	mutex_unlock(&instruction_lock);
	sysfs_notify(perf_kobj, "events", "cpu_hotplug");
	return 0;
}

/* perfd polls this file for a change, then re-reads the online mask. */
static ssize_t cpu_hotplug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "\n");
}
static struct kobj_attribute cpu_hotplug_attr = __ATTR_RO(cpu_hotplug);

#if IS_REACHABLE(CONFIG_QCOM_PMU_LIB)
static ssize_t inst_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct pmu_counts *sample __free(kfree) = kcalloc(nr_cpu_ids, sizeof(*sample), GFP_KERNEL);
	u64 inst[2] = {}, ipc[2] = {};
	unsigned long capacity = ULONG_MAX;
	int cpu, ret = 0;

	if (!sample)
		return -ENOMEM;
	cpus_read_lock();
	mutex_lock(&instruction_lock);
	for_each_possible_cpu(cpu)
		capacity = min(capacity, arch_scale_cpu_capacity(cpu));
	for_each_online_cpu(cpu) {
		ret = qcom_pmu_read(cpu, 0x08, &sample[cpu].instructions);
		if (!ret)
			ret = qcom_pmu_read(cpu, 0x11, &sample[cpu].cycles);
		if (ret)
			goto out;
	}
	for_each_online_cpu(cpu) {
		struct pmu_counts *last = &per_cpu(previous, cpu), *now = &sample[cpu];
		bool big = arch_scale_cpu_capacity(cpu) > capacity;
		u64 ins, cyc;

		ins = now->instructions >= last->instructions ? now->instructions - last->instructions : 0;
		cyc = now->cycles >= last->cycles ? now->cycles - last->cycles : 0;
		*last = *now;
		inst[big] += ins;
		if (cyc && (!big || cyc >= 650000))
			ipc[big] = max(ipc[big], mul_u64_u64_div_u64(ins, 100, cyc));
	}
out:
	mutex_unlock(&instruction_lock);
	cpus_read_unlock();
	if (ret)
		return ret;
	return sysfs_emit(buf, "%llu:%llu:%llu:%llu\n", inst[1], ipc[1], inst[0], ipc[0]);
}
static struct kobj_attribute inst_attr = __ATTR_RO(inst);
#endif

static struct kobj_attribute evnt_gplaf_pid_attr = __ATTR_RW(evnt_gplaf_pid);
static struct kobj_attribute gfx_evt_attr = __ATTR_RO(gfx_evt);
static struct attribute *parameters_attrs[] = {
	&evnt_gplaf_pid_attr.attr,
#if IS_REACHABLE(CONFIG_QCOM_PMU_LIB)
	&inst_attr.attr,
#endif
#ifdef CONFIG_CPU_FREQ
	&cpu_min_freq_attr.attr, &cpu_max_freq_attr.attr,
#endif
	NULL
};
static struct attribute *notify_attrs[] = { &gfx_evt_attr.attr, NULL };
static struct attribute *events_attrs[] = { &cpu_hotplug_attr.attr, NULL };
static const struct attribute_group hotplug = { .name = "events", .attrs = events_attrs };
static const struct attribute_group parameters = { .name = "parameters", .attrs = parameters_attrs };
static const struct attribute_group notify = { .name = "notify", .attrs = notify_attrs };
static const struct attribute_group *groups[] = { &parameters, &notify, &hotplug, NULL };

static int __init msm_perf_init(void)
{
	int ret;

	perf_kobj = kobject_create_and_add("msm_performance", kernel_kobj);
	if (!perf_kobj)
		return -ENOMEM;
#ifdef CONFIG_CPU_FREQ
	ret = cpufreq_register_notifier(&limits_nb, CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		kobject_put(perf_kobj);
		return ret;
	}
#endif
	hotplug_state = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "soc/qcom/perf:online",
						monitor_hotplug, monitor_hotplug);
	ret = hotplug_state < 0 ? hotplug_state : sysfs_create_groups(perf_kobj, groups);
	if (ret) {
		if (hotplug_state >= 0)
			cpuhp_remove_state_nocalls(hotplug_state);
#ifdef CONFIG_CPU_FREQ
		cpufreq_unregister_notifier(&limits_nb, CPUFREQ_POLICY_NOTIFIER);
#endif
		kobject_put(perf_kobj);
	}
	return ret;
}

static void __exit msm_perf_exit(void)
{
	WRITE_ONCE(stopping, true);
	wake_up_interruptible_all(&event_wait);
	cpuhp_remove_state_nocalls(hotplug_state);
	sysfs_remove_groups(perf_kobj, groups);
#ifdef CONFIG_CPU_FREQ
	/* Serialize with REMOVE_POLICY until every request is detached. */
	mutex_lock(&limits_lock);
	for (int cpu = 0; cpu < nr_cpu_ids; cpu++)
		if (limits[cpu].policy)
			remove_limits(&limits[cpu]);
	mutex_unlock(&limits_lock);
	cpufreq_unregister_notifier(&limits_nb, CPUFREQ_POLICY_NOTIFIER);
#endif
	kobject_put(perf_kobj);
}
module_init(msm_perf_init);
module_exit(msm_perf_exit);

MODULE_DESCRIPTION("Qualcomm graphics frame events");
MODULE_LICENSE("GPL");
