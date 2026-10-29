// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define USERSPACE_CDEV_DRIVER		"userspace-cdev"

struct userspace_cdev {
	unsigned int			cur_level;
	unsigned int			max_level;
};

static int userspace_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct userspace_cdev *usr_cdev = cdev->devdata;

	if (!usr_cdev)
		return -EINVAL;
	*state = usr_cdev->max_level;

	return 0;
}

static int userspace_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct userspace_cdev *usr_cdev = cdev->devdata;

	if (!usr_cdev)
		return -EINVAL;
	*state = usr_cdev->cur_level;

	return 0;
}

static int userspace_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct userspace_cdev *usr_cdev = cdev->devdata;

	if (!usr_cdev)
		return -EINVAL;

	if (state > usr_cdev->max_level)
		return -EINVAL;

	if (usr_cdev->cur_level == state)
		return 0;
	usr_cdev->cur_level = state;

	return 0;
}

static struct thermal_cooling_device_ops userspace_cdev_ops = {
	.get_max_state = userspace_get_max_state,
	.get_cur_state = userspace_get_cur_state,
	.set_cur_state = userspace_set_cur_state,
};

static int userspace_device_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	for_each_available_child_of_node_scoped(dev->of_node, child) {
		struct userspace_cdev *state;
		struct thermal_cooling_device *cdev;
		int ret;

		state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
		if (!state)
			return -ENOMEM;
		ret = of_property_read_u32(child, "qcom,max-level", &state->max_level);
		if (ret)
			return dev_err_probe(dev, ret, "%pOF: missing max level\n", child);
		cdev = devm_thermal_of_cooling_device_register(dev, child, child->name,
							      state, &userspace_cdev_ops);
		if (IS_ERR(cdev))
			return dev_err_probe(dev, PTR_ERR(cdev), "%pOF: cooling device\n", child);
	}
	return 0;
}

static const struct of_device_id userspace_device_match[] = {
	{.compatible = "qcom,userspace-cooling-devices"},
	{}
};
MODULE_DEVICE_TABLE(of, userspace_device_match);

static struct platform_driver userspace_cdev_driver = {
	.probe          = userspace_device_probe,
	.driver         = {
		.name   = USERSPACE_CDEV_DRIVER,
		.of_match_table = userspace_device_match,
	},
};

static int __init userspace_cdev_init(void)
{
	return platform_driver_register(&userspace_cdev_driver);
}
module_init(userspace_cdev_init);

static void __exit userspace_cdev_exit(void)
{
	platform_driver_unregister(&userspace_cdev_driver);
}
module_exit(userspace_cdev_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Userspace cooling device driver");
