// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Kalama GDSCs exposed as regulator supplies by the downstream DT.
 * Based on the Qualcomm downstream gdsc-regulator driver.
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/interconnect.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/proxy-consumer.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>

#define GDSCR			0
#define CFG_GDSCR		4
#define PWR_ON			BIT(31)
#define CLK_DIS_WAIT		GENMASK(15, 12)
#define RETAIN_FF		BIT(11)
#define SW_OVERRIDE		BIT(2)
#define HW_CONTROL		BIT(1)
#define SW_COLLAPSE		BIT(0)
#define POWER_UP_COMPLETE	BIT(16)
#define POWER_DOWN_COMPLETE	BIT(15)
#define GMEM_RESET		BIT(4)
#define CLAMP_IO		BIT(0)
#define BCR_RESET		BIT(0)

struct kalama_gdsc {
	struct device *dev;
	struct regulator_desc desc;
	struct regmap *regs;
	struct regmap *hw_ctrl;
	struct regmap *clamp;
	struct regmap *collapse;
	struct regmap **resets;
	unsigned int num_resets;
	u32 collapse_mask;
	u32 timeout_us;
	u32 wait;
	bool has_wait;
	bool cfg_status;
	bool skip_off_poll;
	bool retain;
	bool reset_aon;
	bool hw_trigger;
	bool hw_mode;
	bool enabled;
	struct icc_path *path;
	struct regulator *parent;
	struct work_struct boot_vote_work;
	bool boot_vote;
};

static int gdsc_wait(struct kalama_gdsc *sc, bool on)
{
	struct regmap *map = sc->regs;
	unsigned int reg = GDSCR;
	u32 mask = PWR_ON, expected = on ? PWR_ON : 0, val = 0;
	int ret;

	if (sc->cfg_status) {
		reg = CFG_GDSCR;
		mask = on ? POWER_UP_COMPLETE : POWER_DOWN_COMPLETE;
		expected = mask;
	} else if (sc->hw_ctrl) {
		map = sc->hw_ctrl;
	}

	ret = regmap_read_poll_timeout_atomic(map, reg, val, (val & mask) == expected,
					      1, sc->timeout_us);
	/* An external HW controller can settle one timeout later than GDSCR. */
	if (ret == -ETIMEDOUT && on && sc->hw_ctrl)
		ret = regmap_read_poll_timeout_atomic(map, reg, val, (val & mask) == expected,
						      1, sc->timeout_us);
	if (ret)
		dev_err(sc->dev, "%s transition to %s failed (%d), status %#x\n",
			sc->desc.name, on ? "on" : "off", ret, val);
	return ret;
}

static int gdsc_switch(struct kalama_gdsc *sc, bool on)
{
	u32 val;
	int ret;

	ret = regmap_update_bits(sc->collapse, GDSCR, sc->collapse_mask,
				 on ? 0 : sc->collapse_mask);
	if (ret)
		return ret;

	/* Drain the write, then allow at least eight XO cycles. */
	ret = regmap_read(sc->regs, GDSCR, &val);
	if (ret)
		return ret;
	udelay(1);
	if (!on && sc->skip_off_poll) {
		/* The PCIe vote can be removed while another master stays on. */
		udelay(100);
		return 0;
	}
	return gdsc_wait(sc, on);
}

static int gdsc_prepare_gx(struct kalama_gdsc *sc)
{
	unsigned int i;
	int ret;

	for (i = 0; i < sc->num_resets; i++) {
		ret = regmap_set_bits(sc->resets[i], 0, BCR_RESET);
		if (ret)
			goto unwind;
	}

	if (sc->num_resets) {
		u32 val;

		ret = regmap_read(sc->resets[i - 1], 0, &val);
		if (ret)
			goto unwind;
		udelay(1);
	}

	for (i = 0; i < sc->num_resets; i++) {
		ret = regmap_clear_bits(sc->resets[i], 0, BCR_RESET);
		if (ret) {
			i = sc->num_resets;
			goto unwind;
		}
	}

	if (sc->reset_aon) {
		u32 val;

		ret = regmap_set_bits(sc->clamp, 0, GMEM_RESET);
		if (ret)
			return ret;
		ret = regmap_read(sc->clamp, 0, &val);
		udelay(1);
		if (regmap_clear_bits(sc->clamp, 0, GMEM_RESET))
			return -EIO;
		if (ret)
			return ret;
	}

	if (sc->clamp) {
		u32 val;

		ret = regmap_clear_bits(sc->clamp, 0, CLAMP_IO);
		if (ret)
			return ret;
		return regmap_read(sc->clamp, 0, &val);
	}
	return 0;

unwind:
	while (i)
		regmap_clear_bits(sc->resets[--i], 0, BCR_RESET);
	return ret;
}

static int gdsc_enable(struct regulator_dev *rdev)
{
	struct kalama_gdsc *sc = rdev_get_drvdata(rdev);
	int ret;

	if (sc->hw_mode)
		return -EBUSY;
	ret = icc_set_bw(sc->path, 1, 1);
	if (ret)
		return ret;
	ret = gdsc_prepare_gx(sc);
	if (ret)
		goto unvote;
	ret = gdsc_switch(sc, true);
	if (!ret && sc->retain)
		ret = regmap_set_bits(sc->regs, GDSCR, RETAIN_FF);
	if (ret) {
		if (gdsc_switch(sc, false))
			dev_err(sc->dev, "failed to restore collapse vote\n");
		if (sc->clamp)
			regmap_set_bits(sc->clamp, 0, CLAMP_IO);
		goto unvote;
	}
	/* Clock re-enable and staggered memory power-up each require 1 us. */
	udelay(2);
	sc->enabled = true;
	return 0;
unvote:
	if (icc_set_bw(sc->path, 0, 0))
		dev_err(sc->dev, "failed to release bandwidth vote\n");
	return ret;
}

static void gdsc_drop_boot_vote(void *data)
{
	struct kalama_gdsc *sc = data;

	if (sc->boot_vote) {
		if (regulator_disable(sc->parent))
			dev_err(sc->dev, "failed to release firmware parent vote\n");
		else
			sc->boot_vote = false;
	}
}

static void gdsc_boot_vote_work(struct work_struct *work)
{
	struct kalama_gdsc *sc = container_of(work, struct kalama_gdsc, boot_vote_work);

	gdsc_drop_boot_vote(sc);
}

static int gdsc_disable(struct regulator_dev *rdev)
{
	struct kalama_gdsc *sc = rdev_get_drvdata(rdev);
	int ret;

	udelay(1);
	ret = gdsc_switch(sc, false);
	if (!ret && sc->clamp)
		ret = regmap_set_bits(sc->clamp, 0, CLAMP_IO);
	if (!ret)
		ret = icc_set_bw(sc->path, 0, 0);
	if (ret) {
		if (sc->clamp)
			regmap_clear_bits(sc->clamp, 0, CLAMP_IO);
		if (gdsc_switch(sc, true))
			dev_err(sc->dev, "failed to restore power vote\n");
		return ret;
	}
	sc->enabled = false;
	/* Regulator callbacks can run with the supply tree already locked. */
	schedule_work(&sc->boot_vote_work);
	return 0;
}

static int gdsc_is_enabled(struct regulator_dev *rdev)
{
	struct kalama_gdsc *sc = rdev_get_drvdata(rdev);

	return sc->enabled;
}

static unsigned int gdsc_get_mode(struct regulator_dev *rdev)
{
	struct kalama_gdsc *sc = rdev_get_drvdata(rdev);

	return sc->hw_mode ? REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL;
}

static int gdsc_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct kalama_gdsc *sc = rdev_get_drvdata(rdev);
	bool hw = mode == REGULATOR_MODE_FAST;
	u32 val;
	int ret;

	if (!sc->hw_trigger || (!hw && mode != REGULATOR_MODE_NORMAL))
		return -EINVAL;
	if (!sc->enabled)
		return -EBUSY;
	ret = regmap_update_bits(sc->regs, GDSCR, HW_CONTROL, hw ? HW_CONTROL : 0);
	if (ret)
		return ret;
	ret = regmap_read(sc->regs, GDSCR, &val);
	udelay(1);
	if (!ret && !hw)
		ret = gdsc_wait(sc, true);
	if (ret) {
		regmap_update_bits(sc->regs, GDSCR, HW_CONTROL,
				   sc->hw_mode ? HW_CONTROL : 0);
		return ret;
	}
	sc->hw_mode = hw;
	return 0;
}

static const struct regulator_ops gdsc_ops = {
	.enable = gdsc_enable,
	.disable = gdsc_disable,
	.is_enabled = gdsc_is_enabled,
	.get_mode = gdsc_get_mode,
	.set_mode = gdsc_set_mode,
};

static int gdsc_get_registers(struct platform_device *pdev, struct kalama_gdsc *sc)
{
	struct device_node *np = pdev->dev.of_node;
	struct regmap_config config = {
		.reg_bits = 32, .val_bits = 32, .reg_stride = 4,
	};
	struct device_node *reset;
	struct resource *res;
	void __iomem *base;
	int n, i;
	u32 bit;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res || resource_size(res) < sizeof(u32))
		return -EINVAL;
	/*
	 * These windows overlap the clock controller. The downstream binding
	 * describes GDSCR alone, with the status register at GDSCR + 4 when
	 * qcom,support-cfg-gdscr is present.
	 */
	config.max_register = sc->cfg_status ? CFG_GDSCR : GDSCR;
	base = devm_ioremap(sc->dev, res->start, config.max_register + sizeof(u32));
	if (!base)
		return -ENOMEM;
	sc->regs = devm_regmap_init_mmio(sc->dev, base, &config);
	if (IS_ERR(sc->regs))
		return PTR_ERR(sc->regs);

	sc->collapse = sc->regs;
	sc->collapse_mask = SW_COLLAPSE;
	if (of_property_present(np, "qcom,collapse-vote")) {
		sc->collapse = syscon_regmap_lookup_by_phandle_args(np, "qcom,collapse-vote",
								    1, &bit);
		if (IS_ERR(sc->collapse))
			return PTR_ERR(sc->collapse);
		if (bit >= 32)
			return -EINVAL;
		sc->collapse_mask = BIT(bit);
	}
	if (of_property_present(np, "domain-addr")) {
		sc->clamp = syscon_regmap_lookup_by_phandle(np, "domain-addr");
		if (IS_ERR(sc->clamp))
			return PTR_ERR(sc->clamp);
	}
	if (sc->reset_aon && !sc->clamp)
		return -EINVAL;
	if (of_property_present(np, "hw-ctrl-addr")) {
		sc->hw_ctrl = syscon_regmap_lookup_by_phandle(np, "hw-ctrl-addr");
		if (IS_ERR(sc->hw_ctrl))
			return PTR_ERR(sc->hw_ctrl);
	}

	if (!of_property_present(np, "sw-reset"))
		return 0;
	n = of_count_phandle_with_args(np, "sw-reset", NULL);
	if (n <= 0)
		return -EINVAL;
	sc->resets = devm_kcalloc(sc->dev, n, sizeof(*sc->resets), GFP_KERNEL);
	if (!sc->resets)
		return -ENOMEM;
	for (i = 0; i < n; i++) {
		reset = of_parse_phandle(np, "sw-reset", i);
		if (!reset)
			return -EINVAL;
		sc->resets[i] = syscon_node_to_regmap(reset);
		of_node_put(reset);
		if (IS_ERR(sc->resets[i]))
			return PTR_ERR(sc->resets[i]);
	}
	sc->num_resets = n;
	return 0;
}

static int gdsc_restore(struct device *dev)
{
	struct kalama_gdsc *sc = dev_get_drvdata(dev);
	u32 mask = 0, val = 0;
	int ret;

	if (sc->has_wait) {
		mask |= CLK_DIS_WAIT;
		val |= FIELD_PREP(CLK_DIS_WAIT, sc->wait);
	}
	if (sc->hw_trigger) {
		mask |= HW_CONTROL;
		if (sc->hw_mode)
			val |= HW_CONTROL;
	}
	if (!mask)
		return 0;
	if (sc->parent) {
		ret = regulator_enable(sc->parent);
		if (ret)
			return ret;
	}
	ret = regmap_update_bits(sc->regs, GDSCR, mask, val);
	if (sc->parent && regulator_disable(sc->parent) && !ret)
		ret = -EIO;
	return ret;
}

static int gdsc_resume_early(struct device *dev)
{
	if (pm_suspend_via_firmware())
		return gdsc_restore(dev);
	return 0;
}

static const struct dev_pm_ops gdsc_pm_ops = {
	.resume_early = gdsc_resume_early,
	.restore_early = gdsc_restore,
};

static int gdsc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regulator_config config = { .dev = dev, .of_node = np };
	struct regulator_init_data *init;
	struct regulator_dev *rdev;
	struct kalama_gdsc *sc;
	struct clk_bulk_data *clocks;
	u32 mask = SW_OVERRIDE | HW_CONTROL, val = 0;
	int ret;

	if (!of_machine_is_compatible("qcom,kalama"))
		return -ENODEV;
	sc = devm_kzalloc(dev, sizeof(*sc), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;
	sc->dev = dev;
	sc->timeout_us = 1500;
	if (of_property_present(np, "qcom,gds-timeout")) {
		ret = of_property_read_u32(np, "qcom,gds-timeout", &sc->timeout_us);
		if (ret || !sc->timeout_us)
			return -EINVAL;
	}
	sc->cfg_status = of_property_read_bool(np, "qcom,support-cfg-gdscr");
	sc->retain = of_property_read_bool(np, "qcom,retain-regs");
	sc->hw_trigger = of_property_read_bool(np, "qcom,support-hw-trigger");
	sc->reset_aon = of_property_read_bool(np, "qcom,reset-aon-logic");
	sc->skip_off_poll = of_property_read_bool(np, "qcom,no-status-check-on-disable");
	if (of_property_present(np, "qcom,clk-dis-wait-val")) {
		ret = of_property_read_u32(np, "qcom,clk-dis-wait-val", &sc->wait);
		if (ret || !FIELD_FIT(CLK_DIS_WAIT, sc->wait))
			return -EINVAL;
		sc->has_wait = true;
		mask |= CLK_DIS_WAIT;
		val |= FIELD_PREP(CLK_DIS_WAIT, sc->wait);
	}

	sc->desc.type = REGULATOR_VOLTAGE;
	sc->desc.ops = &gdsc_ops;
	sc->desc.owner = THIS_MODULE;
	ret = of_property_read_string(np, "regulator-name", &sc->desc.name);
	if (ret)
		return ret;
	init = of_get_regulator_init_data(dev, np, &sc->desc);
	if (!init)
		return -ENOMEM;
	if (sc->hw_trigger) {
		init->constraints.valid_ops_mask |= REGULATOR_CHANGE_MODE;
		init->constraints.valid_modes_mask |= REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL;
	}

	ret = gdsc_get_registers(pdev, sc);
	if (ret)
		return dev_err_probe(dev, ret, "cannot obtain GDSC registers\n");
	/* Includes the unnamed dependency on GCC in the UFS/USB nodes. */
	ret = devm_clk_bulk_get_all(dev, &clocks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "cannot obtain interface clocks\n");
	if (of_property_present(np, "interconnects")) {
		sc->path = devm_of_icc_get(dev, "mmnoc");
		if (IS_ERR(sc->path))
			return dev_err_probe(dev, PTR_ERR(sc->path), "cannot obtain mmnoc path\n");
	}

	if (of_property_present(np, "parent-supply")) {
		sc->desc.supply_name = "parent";
		sc->parent = devm_regulator_get_optional(dev, "parent");
		if (IS_ERR(sc->parent))
			return dev_err_probe(dev, PTR_ERR(sc->parent), "cannot obtain parent\n");
		ret = regulator_enable(sc->parent);
		if (ret)
			return dev_err_probe(dev, ret, "cannot power register interface\n");
		sc->boot_vote = true;
		ret = devm_add_action_or_reset(dev, gdsc_drop_boot_vote, sc);
		if (ret)
			return ret;
	}
	ret = devm_work_autocancel(dev, &sc->boot_vote_work, gdsc_boot_vote_work);
	if (ret)
		return ret;

	ret = regmap_update_bits(sc->regs, GDSCR, mask, val);
	if (ret)
		return ret;
	ret = regmap_read(sc->collapse, GDSCR, &val);
	if (ret)
		return ret;
	sc->enabled = !(val & sc->collapse_mask);
	if (sc->enabled) {
		if (sc->retain) {
			ret = regmap_set_bits(sc->regs, GDSCR, RETAIN_FF);
			if (ret)
				return ret;
		}
		ret = icc_set_bw(sc->path, 1, 1);
		if (ret)
			return ret;
	} else {
		gdsc_drop_boot_vote(sc);
	}

	config.driver_data = sc;
	config.regmap = sc->regs;
	config.init_data = init;
	rdev = devm_regulator_register(dev, &sc->desc, &config);
	if (IS_ERR(rdev))
		return dev_err_probe(dev, PTR_ERR(rdev), "cannot register GDSC\n");

	platform_set_drvdata(pdev, sc);
	ret = devm_regulator_proxy_consumer_register(dev, np);
	if (ret)
		return dev_err_probe(dev, ret, "cannot establish boot proxy vote\n");
	return 0;
}

static const struct of_device_id gdsc_match[] = {
	{ .compatible = "qcom,gdsc" },
	{}
};
MODULE_DEVICE_TABLE(of, gdsc_match);

static struct platform_driver gdsc_driver = {
	.probe = gdsc_probe,
	.driver = {
		.name = "kalama-gdsc-regulator",
		.of_match_table = gdsc_match,
		.pm = &gdsc_pm_ops,
		.sync_state = regulator_proxy_consumer_sync_state,
	},
};
module_platform_driver(gdsc_driver);

MODULE_DESCRIPTION("Qualcomm Kalama GDSC regulators");
MODULE_LICENSE("GPL");
