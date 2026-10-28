// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

struct bcl_soc {
	struct notifier_block battery_notifier;
	struct work_struct update_work;
	struct thermal_zone_device *zone;
};

static int bcl_soc_read(struct thermal_zone_device *zone, int *depletion)
{
	struct power_supply *battery;
	union power_supply_propval capacity;
	int ret;

	battery = power_supply_get_by_name("battery");
	if (!battery)
		return -EAGAIN;
	ret = power_supply_get_property(battery, POWER_SUPPLY_PROP_CAPACITY, &capacity);
	power_supply_put(battery);
	if (ret)
		return ret;

	if (capacity.intval < 0 || capacity.intval > 100)
		return -ERANGE;
	*depletion = 100 - capacity.intval;
	return 0;
}

static const struct thermal_zone_device_ops bcl_soc_ops = {
	.get_temp = bcl_soc_read,
};

static void bcl_soc_update(struct work_struct *work)
{
	struct bcl_soc *soc = container_of(work, struct bcl_soc, update_work);

	thermal_zone_device_update(soc->zone, THERMAL_EVENT_UNSPECIFIED);
}

static int bcl_soc_notify(struct notifier_block *nb, unsigned long event, void *data)
{
	struct bcl_soc *soc = container_of(nb, struct bcl_soc, battery_notifier);
	struct power_supply *supply = data;

	if (event == PSY_EVENT_PROP_CHANGED && !strcmp(supply->desc->name, "battery"))
		schedule_work(&soc->update_work);
	return NOTIFY_OK;
}

static void bcl_soc_stop(void *data)
{
	struct bcl_soc *soc = data;

	power_supply_unreg_notifier(&soc->battery_notifier);
	cancel_work_sync(&soc->update_work);
}

static int bcl_soc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcl_soc *soc;
	int ret;

	soc = devm_kzalloc(dev, sizeof(*soc), GFP_KERNEL);
	if (!soc)
		return -ENOMEM;

	soc->zone = devm_thermal_of_zone_register(dev, 0, soc, &bcl_soc_ops);
	if (IS_ERR(soc->zone))
		return dev_err_probe(dev, PTR_ERR(soc->zone), "Failed to register depletion zone\n");

	INIT_WORK(&soc->update_work, bcl_soc_update);
	soc->battery_notifier.notifier_call = bcl_soc_notify;
	ret = power_supply_reg_notifier(&soc->battery_notifier);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, bcl_soc_stop, soc);
	if (!ret)
		schedule_work(&soc->update_work);
	return ret;
}

static const struct of_device_id bcl_soc_match[] = {
	{ .compatible = "qcom,msm-bcl-soc" },
	{}
};
MODULE_DEVICE_TABLE(of, bcl_soc_match);

static struct platform_driver bcl_soc_driver = {
	.probe = bcl_soc_probe,
	.driver = {
		.name = "bcl_soc_peripheral",
		.of_match_table = bcl_soc_match,
	},
};
module_platform_driver(bcl_soc_driver);

MODULE_DESCRIPTION("Qualcomm battery depletion thermal sensor");
MODULE_LICENSE("GPL");
