// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021,2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/thermal.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>

#define CPU_MAP_CT 2
#define CC_CDEV_DRIVER "CPU-voltage-cdev"

struct limits_freq_table {
	unsigned long frequency;
	unsigned long volt;
};

struct limits_freq_map {
	unsigned long frequency[CPU_MAP_CT];
};

struct cc_limits_data {
	struct cpufreq_policy *policies[CPU_MAP_CT];
	unsigned int num_policies;
	int				map_freq_ct;
	int				thermal_state;
	int				cpu_map[CPU_MAP_CT];
	struct limits_freq_map		*map_freq;
	struct freq_qos_request		cc_qos_req[CPU_MAP_CT];
};


static int cc_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct cc_limits_data *cc_cdev = cdev->devdata;
	int idx = 0, ret = 0;
	if (state > cc_cdev->map_freq_ct)
		return -EINVAL;

	if (state == cc_cdev->thermal_state)
		return 0;


	for (idx = 0; idx < cc_cdev->num_policies ; idx++) {
		pr_debug("Mitigate CPU:%d to freq:%lu\n", cc_cdev->cpu_map[idx],
				cc_cdev->map_freq[state].frequency[idx]);
		ret = freq_qos_update_request(&cc_cdev->cc_qos_req[idx],
				cc_cdev->map_freq[state].frequency[idx]);
		if (ret < 0) {
			while (idx--)
				freq_qos_update_request(&cc_cdev->cc_qos_req[idx],
					cc_cdev->map_freq[cc_cdev->thermal_state].frequency[idx]);
			return ret;
		}
	}
	cc_cdev->thermal_state = state;
	return 0;
}

static int cc_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct cc_limits_data *cc_cdev = cdev->devdata;

	*state = cc_cdev->thermal_state;

	return 0;
}

static int cc_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct cc_limits_data *cc_cdev = cdev->devdata;

	*state = cc_cdev->map_freq_ct;

	return 0;
}

static struct thermal_cooling_device_ops cc_cooling_ops = {
	.get_max_state = cc_get_max_state,
	.get_cur_state = cc_get_cur_state,
	.set_cur_state = cc_set_cur_state,
};

static void put_policy(void *arg)
{
	cpufreq_cpu_put(arg);
}

static void remove_limit(void *arg)
{
	freq_qos_remove_request(arg);
}

static int get_rail_table(struct device *dev, struct cpufreq_policy *policy,
			  struct limits_freq_table **result)
{
	struct device *cpu_dev = get_cpu_device(policy->cpu);
	struct limits_freq_table *table;
	unsigned long hz = 0;
	int count, i;

	count = dev_pm_opp_get_opp_count(cpu_dev);
	if (count <= 0)
		return count < 0 ? count : -EPROBE_DEFER;
	table = devm_kcalloc(dev, count, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	for (i = 0; i < count; i++, hz++) {
		struct dev_pm_opp *opp = dev_pm_opp_find_freq_ceil(cpu_dev, &hz);

		if (IS_ERR(opp))
			return PTR_ERR(opp);
		table[i].frequency = hz / 1000; /* cpufreq uses kHz */
		table[i].volt = dev_pm_opp_get_voltage(opp);
		dev_pm_opp_put(opp);
	}
	*result = table;
	return count;
}

static struct cpufreq_policy *get_cluster_policy(struct device_node *node, const char *prop)
{
	struct of_phandle_iterator it;
	int ret;

	of_for_each_phandle(&it, ret, node, prop, NULL, 0) {
		int cpu = of_cpu_node_to_id(it.node);
		struct cpufreq_policy *policy;

		if (cpu < 0)
			continue;
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			of_node_put(it.node);
			return policy;
		}
	}
	return NULL;
}

static int register_voltage_cooling(struct device *dev, struct device_node *node)
{
	static const char * const props[] = {"qcom,cluster0", "qcom,cluster1"};
	struct limits_freq_table *table[CPU_MAP_CT];
	struct thermal_cooling_device *cdev;
	struct cc_limits_data *limits;
	int count[CPU_MAP_CT], i, j, primary = 0, ret;

	limits = devm_kzalloc(dev, sizeof(*limits), GFP_KERNEL);
	if (!limits)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(props); i++) {
		struct cpufreq_policy *policy;
		unsigned int n = limits->num_policies;

		if (!of_property_present(node, props[i]))
			continue;
		policy = get_cluster_policy(node, props[i]);
		if (!policy)
			return -EPROBE_DEFER;
		ret = devm_add_action_or_reset(dev, put_policy, policy);
		if (ret)
			return ret;
		if (n && limits->policies[0] == policy)
			return -EINVAL;
		limits->policies[n] = policy;
		limits->cpu_map[n] = policy->cpu;
		count[n] = get_rail_table(dev, policy, &table[n]);
		if (count[n] < 0)
			return count[n];
		if (table[n][count[n] - 1].volt > table[primary][count[primary] - 1].volt ||
		    (table[n][count[n] - 1].volt == table[primary][count[primary] - 1].volt &&
		     count[n] > count[primary]))
			primary = n;
		limits->num_policies++;
	}
	if (!limits->num_policies)
		return -EINVAL;
	limits->map_freq_ct = count[primary] - 1;
	limits->map_freq = devm_kcalloc(dev, count[primary], sizeof(*limits->map_freq), GFP_KERNEL);
	if (!limits->map_freq)
		return -ENOMEM;

	for (i = 0; i < count[primary]; i++) {
		int pos = count[primary] - 1 - i;

		for (j = 0; j < limits->num_policies; j++) {
			int k = j == primary ? pos : count[j] - 1;

			while (k && table[j][k].volt > table[primary][pos].volt)
				k--;
			limits->map_freq[i].frequency[j] = table[j][k].frequency;
		}
	}
	for (i = 0; i < limits->num_policies; i++) {
		ret = freq_qos_add_request(&limits->policies[i]->constraints, &limits->cc_qos_req[i],
					  FREQ_QOS_MAX, limits->map_freq[0].frequency[i]);
		if (ret < 0)
			return ret;
		ret = devm_add_action_or_reset(dev, remove_limit, &limits->cc_qos_req[i]);
		if (ret)
			return ret;
	}
	cdev = devm_thermal_of_cooling_device_register(dev, node, node->name,
						      limits, &cc_cooling_ops);
	return PTR_ERR_OR_ZERO(cdev);
}

static int cc_cooling_probe(struct platform_device *pdev)
{
	for_each_available_child_of_node_scoped(pdev->dev.of_node, child) {
		int ret = register_voltage_cooling(&pdev->dev, child);

		if (ret)
			return dev_err_probe(&pdev->dev, ret, "%pOF: voltage cooling\n", child);
	}
	return 0;
}

static const struct of_device_id cc_cooling_device_match[] = {
	{.compatible = "qcom,cc-cooling-devices"},
	{}
};
MODULE_DEVICE_TABLE(of, cc_cooling_device_match);

static struct platform_driver cc_cooling_driver = {
	.probe          = cc_cooling_probe,
	.driver         = {
		.name   = CC_CDEV_DRIVER,
		.of_match_table = cc_cooling_device_match,
	},
};

module_platform_driver(cc_cooling_driver);
MODULE_DESCRIPTION("CPU Voltage cooling device driver");
MODULE_LICENSE("GPL v2");
