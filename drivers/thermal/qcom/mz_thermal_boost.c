// SPDX-License-Identifier: GPL-2.0-only
/* M2391 .text+0x254..0x1228 recovery, original types from split/base BTF.
 * This changes an existing cooling MAX request, never creates a MIN request.
 * Frequency selection follows original table indices, not arithmetic scaling.
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/meizu_thermal_boost.h>

#define SCHED_BOOST BIT(0)
#define THERMAL_BOOST BIT(1)
#define THERMAL_RESTORE BIT(2)
struct policy_boost_info {
	unsigned int flag;
	bool throttled;
	struct cpufreq_policy *policy;
	struct freq_qos_request *boost_req;
	unsigned int boost_freq;
	unsigned int boost_freq_last;
	unsigned int restore_freq;
};
struct thermal_boost_cdev {
	int policy_num;
	unsigned long boost_lvl;
	bool boost_enabled;
	bool sched_boost_enabled;
	struct thermal_cooling_device *cdev;
	struct work_struct qos_work;
	struct policy_boost_info info[];
};
static DEFINE_MUTEX(boost_lock);
static struct thermal_boost_cdev *boost;
static struct workqueue_struct *cb_qos_wq;
static struct delayed_work init_work;
static struct proc_dir_entry *proc_dir;
static bool stopping;

static bool usable(void)
{
	return boost && boost->policy_num &&
		(boost->boost_enabled || boost->sched_boost_enabled) && boost->boost_lvl <= 15;
}
static int min_index(struct cpufreq_policy *policy)
{
	int index;

	if (!policy || !policy->freq_table)
		return -EINVAL;
	index = cpufreq_frequency_table_get_index(policy, policy->cpuinfo.min_freq);
	if (index <= 0 || boost->boost_lvl >= index)
		return -EINVAL;
	/* The original indexes a descending, hole-free OEM table. Keep that
	 * indexing but never send an invalid/sentinel entry as a QoS frequency.
	 */
	if (policy->freq_table[index - 1].frequency == CPUFREQ_ENTRY_INVALID ||
	    policy->freq_table[index - boost->boost_lvl].frequency == CPUFREQ_ENTRY_INVALID ||
	    policy->freq_table[index - boost->boost_lvl].frequency == CPUFREQ_TABLE_END)
		return -EINVAL;
	return index;
}
static struct policy_boost_info *find_policy(struct cpufreq_policy *policy)
{
	int i;

	if (!policy)
		return NULL;
	for (i = 0; i < boost->policy_num; i++)
		if (boost->info[i].policy->cpu == policy->cpu)
			return &boost->info[i];
	return NULL;
}
static bool any_throttled(void)
{
	int i;

	for (i = 0; i < boost->policy_num; i++)
		if (boost->info[i].throttled)
			return true;
	return false;
}
void mz_thermal_boost_store(struct freq_qos_request *qos_req,
		struct cpufreq_policy *policy, unsigned int *frequency)
{
	struct policy_boost_info *info;

	if (!qos_req || !frequency)
		return;
	mutex_lock(&boost_lock);
	if (usable() && min_index(policy) >= 0 && (info = find_policy(policy))) {
		if (!info->boost_req)
			info->boost_req = qos_req;
		info->restore_freq = *frequency;
		info->throttled = *frequency != policy->cpuinfo.min_freq;
	}
	mutex_unlock(&boost_lock);
}
EXPORT_SYMBOL(mz_thermal_boost_store);

void mz_thermal_boost_request(struct cpufreq_policy *policy, unsigned int frequency)
{
	struct policy_boost_info *info;
	int index;

	mutex_lock(&boost_lock);
	if (!usable() || !boost->boost_enabled || !any_throttled())
		goto out;
	index = min_index(policy);
	info = find_policy(policy);
	if (index < 0 || !info || !info->boost_req ||
	    policy->freq_table[index - 1].frequency > frequency)
		goto out;
	info->flag |= THERMAL_BOOST;
	info->boost_freq = policy->freq_table[index - boost->boost_lvl].frequency;
	queue_work(cb_qos_wq, &boost->qos_work);
out:
	mutex_unlock(&boost_lock);
}
EXPORT_SYMBOL(mz_thermal_boost_request);

void mz_thermal_boost_restore(struct cpufreq_policy *policy, unsigned int frequency)
{
	struct policy_boost_info *info;
	int index;

	mutex_lock(&boost_lock);
	if (!usable() || !boost->boost_enabled)
		goto out;
	index = min_index(policy);
	info = find_policy(policy);
	if (index < 0 || !info || !info->boost_req ||
	    info->boost_freq_last == frequency ||
	    policy->freq_table[index - 1].frequency <= frequency)
		goto out;
	info->flag |= THERMAL_RESTORE;
	info->boost_freq_last = frequency;
	queue_work(cb_qos_wq, &boost->qos_work);
out:
	mutex_unlock(&boost_lock);
}
EXPORT_SYMBOL(mz_thermal_boost_restore);

void mz_thermal_sched_boost_request(int enable)
{
	struct policy_boost_info *info;
	int i, index;

	mutex_lock(&boost_lock);
	if (!usable() || !boost->sched_boost_enabled || !any_throttled() || enable < 0)
		goto out;
	for (i = 0; i < boost->policy_num; i++) {
		info = &boost->info[i];
		index = min_index(info->policy);
		if (index < 0 || !info->boost_req)
			continue;
		if (!enable)
			info->flag |= THERMAL_RESTORE;
		else {
			info->flag |= SCHED_BOOST;
			info->boost_freq = info->policy->freq_table[index - boost->boost_lvl].frequency;
		}
	}
	queue_work(cb_qos_wq, &boost->qos_work);
out:
	mutex_unlock(&boost_lock);
}
EXPORT_SYMBOL(mz_thermal_sched_boost_request);

static void qos_work(struct work_struct *work)
{
	struct thermal_boost_cdev *state = container_of(work, struct thermal_boost_cdev, qos_work);
	struct freq_qos_request *req;
	unsigned int frequencies[3], flags;
	int i, n;

	for (i = 0; i < state->policy_num; i++) {
		mutex_lock(&boost_lock);
		req = state->info[i].boost_req;
		flags = state->info[i].flag;
		state->info[i].flag = 0;
		frequencies[0] = frequencies[1] = state->info[i].boost_freq;
		frequencies[2] = state->info[i].restore_freq;
		mutex_unlock(&boost_lock);
		if (!req)
			continue;
		/* Same SCHED -> THERMAL -> RESTORE priority/order as stock.
		 * Detach clears req and flushes this work before freeing a request.
		 */
		for (n = 0; n < 3; n++)
			if ((flags & BIT(n)) && freq_qos_request_active(req) &&
			    freq_qos_update_request(req, frequencies[n]) < 0)
				pr_err("mz_thermal_boost: QoS update failed\n");
	}
}

void mz_thermal_boost_forget(struct freq_qos_request *qos_req)
{
	struct thermal_boost_cdev *state;
	int i;

	mutex_lock(&boost_lock);
	state = boost;
	if (state)
		for (i = 0; i < state->policy_num; i++)
			if (state->info[i].boost_req == qos_req) {
				state->info[i].boost_req = NULL;
				state->info[i].flag = 0;
				state->info[i].throttled = false;
			}
	mutex_unlock(&boost_lock);
	if (state)
		flush_work(&state->qos_work);
}
EXPORT_SYMBOL_GPL(mz_thermal_boost_forget);

static ssize_t enabled_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	bool sched = (unsigned long)PDE_DATA(file_inode(file));
	char text[16];
	bool enabled;
	int len;

	mutex_lock(&boost_lock);
	enabled = boost && (sched ? boost->sched_boost_enabled : boost->boost_enabled);
	mutex_unlock(&boost_lock);
	/* Stock spells the disabled string this way. */
	len = scnprintf(text, sizeof(text), "%s\n", enabled ? "enabled" : "disabed");
	return simple_read_from_buffer(buf, count, pos, text, len);
}
static ssize_t enabled_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	bool sched = (unsigned long)PDE_DATA(file_inode(file));
	char text[20] = { 0 };
	size_t copied = min_t(size_t, count, sizeof(text) - 1);

	if (copy_from_user(text, buf, copied))
		return -EFAULT;
	mutex_lock(&boost_lock);
	if (!boost) {
		mutex_unlock(&boost_lock);
		return -ENODEV;
	}
	if (sched)
		boost->sched_boost_enabled = text[0] != '0';
	else
		boost->boost_enabled = text[0] != '0';
	mutex_unlock(&boost_lock);
	return copied;
}
static const struct proc_ops enabled_ops = {
	.proc_read = enabled_read,
	.proc_write = enabled_write,
};
static int get_max_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = 16;
	return 0;
}
static int get_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	mutex_lock(&boost_lock);
	*state = ((struct thermal_boost_cdev *)cdev->devdata)->boost_lvl;
	mutex_unlock(&boost_lock);
	return 0;
}
static int set_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	mutex_lock(&boost_lock);
	((struct thermal_boost_cdev *)cdev->devdata)->boost_lvl = state;
	mutex_unlock(&boost_lock);
	return 0;
}
static struct thermal_cooling_device_ops boost_cdev_ops = {
	.get_max_state = get_max_state,
	.get_cur_state = get_state,
	.set_cur_state = set_state,
};

static void boost_init_work(struct work_struct *work)
{
	struct cpufreq_policy *policies[NR_CPUS];
	struct cpufreq_policy *policy;
	struct thermal_boost_cdev *state;
	int cpu, count = 0, i;

	for_each_present_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			goto retry;
		for (i = 0; i < count; i++)
			if (policies[i] == policy)
				break;
		if (i == count)
			policies[count++] = policy;
		else
			cpufreq_cpu_put(policy);
	}
	if (!count)
		goto retry;
	state = kzalloc(struct_size(state, info, count), GFP_KERNEL);
	if (!state)
		goto release;
	state->policy_num = count;
	state->boost_lvl = 1;
	state->boost_enabled = state->sched_boost_enabled = true;
	for (i = 0; i < count; i++)
		state->info[i].policy = policies[i];
	INIT_WORK(&state->qos_work, qos_work);
	state->cdev = thermal_of_cooling_device_register(NULL, "thermal-boost", state, &boost_cdev_ops);
	if (IS_ERR(state->cdev)) {
		kfree(state);
		goto release;
	}
	proc_dir = proc_mkdir("mz_thermal_boost", NULL);
	if (!proc_dir || !proc_create_data("boost_enabled", 0666, proc_dir, &enabled_ops, NULL) ||
	    !proc_create_data("sched_boost_enabled", 0666, proc_dir, &enabled_ops, (void *)1)) {
		if (proc_dir)
			proc_remove(proc_dir);
		proc_dir = NULL;
		thermal_cooling_device_unregister(state->cdev);
		kfree(state);
		goto release;
	}
	mutex_lock(&boost_lock);
	boost = state;
	mutex_unlock(&boost_lock);
	return;
retry:
	for (i = 0; i < count; i++)
		cpufreq_cpu_put(policies[i]);
	mutex_lock(&boost_lock);
	if (!stopping)
		queue_delayed_work(system_wq, &init_work, msecs_to_jiffies(500));
	mutex_unlock(&boost_lock);
	return;
release:
	for (i = 0; i < count; i++)
		cpufreq_cpu_put(policies[i]);
}
static int __init thermal_boost_init(void)
{
	cb_qos_wq = alloc_workqueue("cb_qos_wq", WQ_HIGHPRI, 0);
	if (!cb_qos_wq)
		return -ENOMEM;
	INIT_DELAYED_WORK(&init_work, boost_init_work);
	queue_delayed_work(system_wq, &init_work, 0);
	return 0;
}
static void __exit thermal_boost_exit(void)
{
	struct thermal_boost_cdev *state;
	int i;

	mutex_lock(&boost_lock);
	stopping = true;
	mutex_unlock(&boost_lock);
	cancel_delayed_work_sync(&init_work);
	if (proc_dir)
		proc_remove(proc_dir);
	mutex_lock(&boost_lock);
	state = boost;
	boost = NULL;
	mutex_unlock(&boost_lock);
	destroy_workqueue(cb_qos_wq);
	if (state) {
		thermal_cooling_device_unregister(state->cdev);
		for (i = 0; i < state->policy_num; i++)
			cpufreq_cpu_put(state->info[i].policy);
		kfree(state);
	}
}
module_init(thermal_boost_init);
module_exit(thermal_boost_exit);
MODULE_DESCRIPTION("Meizu M2391 thermal QoS boost");
MODULE_LICENSE("GPL v2");
