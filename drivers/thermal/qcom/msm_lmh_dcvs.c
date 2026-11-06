// SPDX-License-Identifier: GPL-2.0-only
/* The Kalama LMh firmware uses two board-supplied current-sense references. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

static void isens_release(void *data)
{
	struct regulator *reg = data;

	regulator_disable(reg);
	regulator_set_load(reg, 0);
	regulator_set_voltage(reg, 0, INT_MAX);
}

static int isens_enable(struct device *dev, const char *name,
			const char *supply, const char *settings)
{
	struct regulator *reg;
	u32 config[3];
	int ret;

	if (!of_property_present(dev->of_node, settings) &&
	    !of_property_present(dev->of_node, supply))
		return 0;
	if (of_property_count_u32_elems(dev->of_node, settings) != ARRAY_SIZE(config))
		return -EINVAL;
	ret = of_property_read_u32_array(dev->of_node, settings, config, ARRAY_SIZE(config));
	if (ret)
		return ret;
	if (!config[0] || config[0] > config[1] ||
	    config[1] > INT_MAX || config[2] > INT_MAX)
		return -EINVAL;
	reg = devm_regulator_get_optional(dev, name);
	if (IS_ERR(reg))
		return dev_err_probe(dev, PTR_ERR(reg), "cannot get %s\n", name);
	ret = regulator_set_voltage(reg, config[0], config[1]);
	if (ret)
		return ret;
	ret = regulator_set_load(reg, config[2]);
	if (ret)
		goto release_voltage;
	ret = regulator_enable(reg);
	if (ret)
		goto release_load;
	return devm_add_action_or_reset(dev, isens_release, reg);

release_load:
	regulator_set_load(reg, 0);
release_voltage:
	regulator_set_voltage(reg, 0, INT_MAX);
	return ret;
}

static int limits_dcvs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *cpu, *lmh;
	int ret;

	/* CPU-controlled LMh instances need a different register/IRQ interface. */
	for_each_of_cpu_node(cpu) {
		lmh = of_parse_phandle(cpu, "qcom,lmh-dcvs", 0);
		of_node_put(lmh);
		if (lmh == dev->of_node) {
			of_node_put(cpu);
			return -ENODEV;
		}
	}
	ret = isens_enable(dev, "isens_vref_1p8", "isens_vref_1p8-supply",
			   "isens-vref-1p8-settings");
	if (ret)
		return ret;
	return isens_enable(dev, "isens_vref_0p8", "isens_vref_0p8-supply",
			    "isens-vref-0p8-settings");
}

static const struct of_device_id limits_dcvs_match[] = {
	{ .compatible = "qcom,msm-hw-limits" },
	{}
};
MODULE_DEVICE_TABLE(of, limits_dcvs_match);

static struct platform_driver limits_dcvs_driver = {
	.probe = limits_dcvs_probe,
	.driver = {
		.name = "msm-lmh-dcvs",
		.of_match_table = limits_dcvs_match,
	},
};
module_platform_driver(limits_dcvs_driver);
MODULE_DESCRIPTION("Qualcomm LMh current-sense reference supplies");
MODULE_LICENSE("GPL");
