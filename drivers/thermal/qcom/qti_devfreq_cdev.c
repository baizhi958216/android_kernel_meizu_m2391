// SPDX-License-Identifier: GPL-2.0-only
/* Additional GPU cooling vote exposed to the Qualcomm thermal HAL. */
#include <linux/devfreq_cooling.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static void qti_devfreq_release(void *data)
{
	devfreq_cooling_unregister(data);
}

static int qti_devfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct platform_device *supplier;
	struct thermal_cooling_device *cdev;
	struct device_node *np;
	struct device_link *link;
	struct devfreq *df;
	int ret;

	np = of_parse_phandle(dev->of_node, "qcom,devfreq", 0);
	if (!np)
		return -EINVAL;
	supplier = of_find_device_by_node(np);
	of_node_put(np);
	if (!supplier)
		return -EPROBE_DEFER;

	/* KGSL owns the OPP table and the devfreq instance until its remove. */
	if (!device_trylock(&supplier->dev)) {
		ret = -EPROBE_DEFER;
		goto put_supplier;
	}
	if (!device_is_bound(&supplier->dev)) {
		ret = -EPROBE_DEFER;
		goto unlock_supplier;
	}
	link = device_link_add(dev, &supplier->dev, DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!link) {
		ret = -EINVAL;
		goto unlock_supplier;
	}
	df = devfreq_get_devfreq_by_node(supplier->dev.of_node);
	if (IS_ERR(df)) {
		ret = PTR_ERR(df);
		goto unlock_supplier;
	}
	cdev = of_devfreq_cooling_register_named(dev->of_node, df, "gpu");
	if (IS_ERR(cdev))
		ret = PTR_ERR(cdev);
	else
		ret = devm_add_action_or_reset(dev, qti_devfreq_release, cdev);
unlock_supplier:
	device_unlock(&supplier->dev);
put_supplier:
	put_device(&supplier->dev);
	return ret;
}

static const struct of_device_id qti_devfreq_match[] = {
	{ .compatible = "qcom,devfreq-cdev" },
	{}
};
MODULE_DEVICE_TABLE(of, qti_devfreq_match);

static struct platform_driver qti_devfreq_driver = {
	.probe = qti_devfreq_probe,
	.driver = {
		.name = "qcom-devfreq-cdev",
		.of_match_table = qti_devfreq_match,
	},
};
module_platform_driver(qti_devfreq_driver);
MODULE_DESCRIPTION("Qualcomm GPU thermal HAL cooling vote");
MODULE_LICENSE("GPL");
