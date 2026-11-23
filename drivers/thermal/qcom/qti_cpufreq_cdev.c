// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/thermal.h>

#define CPUFREQ_CDEV "qcom-cpufreq-cdev"

struct cpufreq_cdev_device {
	struct cpufreq_policy *policy;
	struct freq_qos_request qos_max_freq_req;
	unsigned int cur_state, max_state;
	unsigned int freq_table[];
};

static int cpufreq_cdev_set_state(struct thermal_cooling_device *cdev,
					unsigned long state)
{
	struct cpufreq_cdev_device *cdev_data = cdev->devdata;
	int ret = 0;
	unsigned int freq;

	if (state > cdev_data->max_state)
		return -EINVAL;
	if (state == cdev_data->cur_state)
		return 0;

	if (freq_qos_request_active(&cdev_data->qos_max_freq_req)) {
		freq = cdev_data->freq_table[state];
		pr_debug("cdev:%s Limit:%u\n", cdev->type, freq);
		ret = freq_qos_update_request(&cdev_data->qos_max_freq_req,
						freq);
		if (ret < 0) {
			pr_err("Error placing qos request:%u. cdev:%s err:%d\n",
				freq, cdev->type, ret);
			return ret;
		}
	}
	cdev_data->cur_state = state;

	return 0;
}

static int cpufreq_cdev_get_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct cpufreq_cdev_device *cdev_data = cdev->devdata;

	*state = cdev_data->cur_state;
	return 0;
}

static int cpufreq_cdev_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct cpufreq_cdev_device *cdev_data = cdev->devdata;

	*state = cdev_data->max_state;
	return 0;
}

static struct thermal_cooling_device_ops cpufreq_cdev_ops = {
	.set_cur_state = cpufreq_cdev_set_state,
	.get_cur_state = cpufreq_cdev_get_state,
	.get_max_state = cpufreq_cdev_get_max_state,
};

static int compare_frequency_descending(const void *a, const void *b)
{
	return cmp_int(*(const unsigned int *)b, *(const unsigned int *)a);
}

static void cpufreq_cdev_release(void *data)
{
	struct cpufreq_cdev_device *state = data;

	freq_qos_remove_request(&state->qos_max_freq_req);
	cpufreq_cpu_put(state->policy);
}

static int register_cpu_cooling(struct device *dev, struct device_node *node)
{
	struct thermal_cooling_device *cdev;
	struct cpufreq_frequency_table *entry;
	struct cpufreq_cdev_device *state;
	struct cpufreq_policy *policy = NULL;
	struct of_phandle_iterator it;
	int cpu, ret, count, i = 0;

	of_for_each_phandle(&it, ret, node, "qcom,cpus", NULL, 0) {
		cpu = of_cpu_node_to_id(it.node);
		if (cpu >= 0)
			policy = cpufreq_cpu_get(cpu);
		if (policy) {
			of_node_put(it.node);
			break;
		}
	}
	if (!policy)
		return dev_err_probe(dev, -EPROBE_DEFER, "%pOF: waiting for CPU policy\n", node);
	count = cpufreq_table_count_valid_entries(policy);
	if (!count) {
		ret = -EINVAL;
		goto put_policy;
	}
	state = devm_kzalloc(dev, struct_size(state, freq_table, count), GFP_KERNEL);
	if (!state) {
		ret = -ENOMEM;
		goto put_policy;
	}
	cpufreq_for_each_valid_entry(entry, policy->freq_table)
		state->freq_table[i++] = entry->frequency;
	sort(state->freq_table, count, sizeof(*state->freq_table),
	     compare_frequency_descending, NULL);
	state->max_state = count - 1;
	state->policy = policy;
	ret = freq_qos_add_request(&policy->constraints, &state->qos_max_freq_req,
				  FREQ_QOS_MAX, state->freq_table[0]);
	if (ret < 0)
		goto put_policy;
	ret = devm_add_action_or_reset(dev, cpufreq_cdev_release, state);
	if (ret)
		return ret;
	cdev = devm_thermal_of_cooling_device_register(dev, node, node->name,
						      state, &cpufreq_cdev_ops);
	return PTR_ERR_OR_ZERO(cdev);

put_policy:
	cpufreq_cpu_put(policy);
	return ret;
}

static int cpufreq_cdev_probe(struct platform_device *pdev)
{
	for_each_available_child_of_node_scoped(pdev->dev.of_node, node) {
		int ret = register_cpu_cooling(&pdev->dev, node);

		if (ret)
			return ret;
	}
	return 0;
}

static const struct of_device_id cpufreq_cdev_match[] = {
	{.compatible = "qcom,cpufreq-cdev"},
	{}
};
MODULE_DEVICE_TABLE(of, cpufreq_cdev_match);

static struct platform_driver cpufreq_cdev_driver = {
	.probe          = cpufreq_cdev_probe,
	.driver         = {
		.name   = CPUFREQ_CDEV,
		.of_match_table = cpufreq_cdev_match,
	},
};

module_platform_driver(cpufreq_cdev_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. cpufreq cooling driver");
MODULE_LICENSE("GPL v2");
