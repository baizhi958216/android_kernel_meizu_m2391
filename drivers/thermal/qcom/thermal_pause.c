// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm CPU pause cooling. WALT keeps one vote per client and CPU.
 * Cooling devices sharing CPUs must preserve each other's thermal votes.
 */
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched/walt.h>
#include <linux/thermal.h>
#include <linux/thermal_pause.h>
#include <linux/workqueue.h>

struct pause_cdev {
	cpumask_t cpus;
	struct delayed_work retry;
	bool requested;
	bool active;
};

static DEFINE_MUTEX(pause_lock);
static BLOCKING_NOTIFIER_HEAD(pause_notifiers);
static unsigned int votes[NR_CPUS];
static cpumask_t thermal_cpus;

void thermal_pause_notifier_register(struct notifier_block *nb)
{
	blocking_notifier_chain_register(&pause_notifiers, nb);
}
EXPORT_SYMBOL_GPL(thermal_pause_notifier_register);

void thermal_pause_notifier_unregister(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&pause_notifiers, nb);
}
EXPORT_SYMBOL_GPL(thermal_pause_notifier_unregister);

const struct cpumask *thermal_paused_cpumask(void)
{
	return &thermal_cpus;
}
EXPORT_SYMBOL_GPL(thermal_paused_cpumask);

static int apply_pause(struct pause_cdev *pc)
{
	cpumask_t change, request;
	bool pause = pc->requested;
	unsigned int cpu;
	int ret;

	lockdep_assert_held(&pause_lock);
	if (pause == pc->active)
		return 0;
	cpumask_clear(&change);
	for_each_cpu(cpu, &pc->cpus)
		if (votes[cpu] == (pause ? 0 : 1))
			cpumask_set_cpu(cpu, &change);

	if (!cpumask_empty(&change)) {
		cpumask_copy(&request, &change);
		ret = pause ? walt_pause_cpus(&request, PAUSE_THERMAL) :
			      walt_resume_cpus(&request, PAUSE_THERMAL);
		if (ret)
			return ret;
	}
	for_each_cpu(cpu, &pc->cpus) {
		if (pause)
			votes[cpu]++;
		else
			votes[cpu]--;
	}
	pc->active = pause;
	for_each_cpu(cpu, &change) {
		if (pause)
			cpumask_set_cpu(cpu, &thermal_cpus);
		else
			cpumask_clear_cpu(cpu, &thermal_cpus);
	}
	for_each_cpu(cpu, &change)
		blocking_notifier_call_chain(&pause_notifiers, pause, (void *)(long)cpu);
	return 0;
}

static void pause_retry(struct work_struct *work)
{
	struct pause_cdev *pc = container_of(to_delayed_work(work), struct pause_cdev, retry);

	mutex_lock(&pause_lock);
	if (apply_pause(pc) == -EAGAIN)
		mod_delayed_work(system_wq, &pc->retry, msecs_to_jiffies(250));
	mutex_unlock(&pause_lock);
}

static int pause_set_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct pause_cdev *pc = cdev->devdata;
	int ret;

	if (state > 1)
		return -EINVAL;
	mutex_lock(&pause_lock);
	pc->requested = state;
	ret = apply_pause(pc);
	if (ret == -EAGAIN)
		mod_delayed_work(system_wq, &pc->retry, msecs_to_jiffies(250));
	mutex_unlock(&pause_lock);
	return ret;
}

static int pause_get_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct pause_cdev *pc = cdev->devdata;

	mutex_lock(&pause_lock);
	*state = pc->active;
	mutex_unlock(&pause_lock);
	return 0;
}

static int pause_get_max_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = 1;
	return 0;
}

static const struct thermal_cooling_device_ops pause_ops = {
	.get_max_state = pause_get_max_state,
	.get_cur_state = pause_get_state,
	.set_cur_state = pause_set_state,
};

static void pause_release(void *data)
{
	struct pause_cdev *pc = data;

	disable_delayed_work_sync(&pc->retry);
	mutex_lock(&pause_lock);
	pc->requested = false;
	WARN_ON_ONCE(apply_pause(pc));
	mutex_unlock(&pause_lock);
}

static int pause_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	for_each_available_child_of_node_scoped(dev->of_node, child) {
		struct thermal_cooling_device *cdev;
		struct pause_cdev *pc;
		struct of_phandle_iterator it;
		const char *name;
		int cpu, ret;

		pc = devm_kzalloc(dev, sizeof(*pc), GFP_KERNEL);
		if (!pc)
			return -ENOMEM;
		of_for_each_phandle(&it, ret, child, "qcom,cpus", NULL, 0) {
			cpu = of_cpu_node_to_id(it.node);
			if (cpu < 0 || !cpu_possible(cpu)) {
				of_node_put(it.node);
				return -EINVAL;
			}
			cpumask_set_cpu(cpu, &pc->cpus);
		}
		if (ret != -ENOENT || cpumask_empty(&pc->cpus))
			return -EINVAL;
		if (of_property_read_string(child, "qcom,cdev-alias", &name)) {
			name = devm_kasprintf(dev, GFP_KERNEL, "thermal-pause-%lX",
					      cpumask_bits(&pc->cpus)[0]);
			if (!name)
				return -ENOMEM;
		}
		INIT_DELAYED_WORK(&pc->retry, pause_retry);
		ret = devm_add_action_or_reset(dev, pause_release, pc);
		if (ret)
			return ret;
		cdev = devm_thermal_of_cooling_device_register(dev, child, name, pc, &pause_ops);
		if (IS_ERR(cdev))
			return dev_err_probe(dev, PTR_ERR(cdev), "registering %s\n", name);
	}
	return 0;
}

static const struct of_device_id pause_match[] = {
	{ .compatible = "qcom,thermal-pause" }, {}
};
MODULE_DEVICE_TABLE(of, pause_match);

static struct platform_driver pause_driver = {
	.probe = pause_probe,
	.driver = {
		.name = "thermal_pause",
		.of_match_table = pause_match,
	},
};
module_platform_driver(pause_driver);
MODULE_DESCRIPTION("Qualcomm WALT CPU pause cooling");
MODULE_LICENSE("GPL");
