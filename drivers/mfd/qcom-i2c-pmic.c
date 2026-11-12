// SPDX-License-Identifier: GPL-2.0-only
/* Each PM8008 register bank has its own firmware-described I2C client. */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>

static const struct regmap_config pm8008_split_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
};

static int pm8008_split_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regmap *regmap;

	/* The two Kalama camera PMICs have no summary interrupt wiring. */
	if (of_property_present(dev->of_node, "interrupt-controller"))
		return -ENODEV;
	regmap = devm_regmap_init_i2c(client, &pm8008_split_regmap);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "cannot initialise PMIC regmap\n");
	return devm_of_platform_populate(dev);
}

static const struct of_device_id pm8008_split_match[] = {
	{ .compatible = "qcom,i2c-pmic" },
	{}
};
MODULE_DEVICE_TABLE(of, pm8008_split_match);

static struct i2c_driver pm8008_split_driver = {
	.driver = {
		.name = "qcom-i2c-pmic",
		.of_match_table = pm8008_split_match,
	},
	.probe = pm8008_split_probe,
};
module_i2c_driver(pm8008_split_driver);
MODULE_DESCRIPTION("Qualcomm split-address I2C PMIC register banks");
MODULE_LICENSE("GPL");
