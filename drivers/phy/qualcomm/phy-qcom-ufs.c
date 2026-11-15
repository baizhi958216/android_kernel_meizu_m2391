// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2021, Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "phy-qcom-ufs-i.h"
#include <linux/sched/mm.h>
#include <linux/pm_runtime.h>

#define MAX_PROP_NAME              32
#define VDDA_PHY_MIN_UV            880000
#define VDDA_PHY_MAX_UV            925000
#define VDDA_PLL_MIN_UV            1200000
#define VDDA_PLL_MAX_UV            1800000
#define VDDP_REF_CLK_MIN_UV        1200000
#define VDDP_REF_CLK_MAX_UV        1200000
#define VDDA_QREF_MIN_UV	   880000
#define VDDA_QREF_MAX_UV	   912000

#define UFS_PHY_DEFAULT_LANES_PER_DIRECTION	1

/**
 * struct ufs_qcom_phy_regs - record the info of ufs qcom phy register domain.
 * @list_head: the list to find all ufs phy register domins.
 * @prefix: the name of this register domain.
 * @ptr: the pointer to memory address which save the register value.
 * @len: the size of this register domain.
 */
struct ufs_qcom_phy_regs {
	struct list_head list;
	const char *prefix;
	u32 *ptr;
	size_t len;
};

static int ufs_qcom_phy_start_serdes(struct ufs_qcom_phy *ufs_qcom_phy);
static int ufs_qcom_phy_is_pcs_ready(struct ufs_qcom_phy *ufs_qcom_phy);

void ufs_qcom_phy_write_tbl(struct ufs_qcom_phy *ufs_qcom_phy,
			    struct ufs_qcom_phy_calibration *tbl,
			    int tbl_size)
{
	int i;

	for (i = 0; i < tbl_size; i++)
		writel_relaxed(tbl[i].cfg_value,
			       ufs_qcom_phy->mmio + tbl[i].reg_offset);
}
EXPORT_SYMBOL(ufs_qcom_phy_write_tbl);

int ufs_qcom_phy_calibrate(struct ufs_qcom_phy *ufs_qcom_phy,
			   struct ufs_qcom_phy_calibration *tbl_A,
			   int tbl_size_A,
			   struct ufs_qcom_phy_calibration *tbl_B,
			   int tbl_size_B, bool is_rate_B)
{
	struct device *dev = ufs_qcom_phy->dev;
	int ret = 0;

	ret = reset_control_assert(ufs_qcom_phy->ufs_reset);
	if (ret) {
		dev_err(dev, "Failed to assert UFS PHY reset %d\n", ret);
		goto out;
	}

	if (!tbl_A) {
		dev_err(dev, "%s: tbl_A is NULL\n", __func__);
		ret = EINVAL;
		goto out;
	}

	ufs_qcom_phy_write_tbl(ufs_qcom_phy, tbl_A, tbl_size_A);

	/*
	 * In case we would like to work in rate B, we need
	 * to override a registers that were configured in rate A table
	 * with registers of rate B table.
	 * table.
	 */
	if (is_rate_B) {
		if (!tbl_B) {
			dev_err(dev, "%s: tbl_B is NULL\n",
				__func__);
			ret = EINVAL;
			goto out;
		}

		ufs_qcom_phy_write_tbl(ufs_qcom_phy, tbl_B, tbl_size_B);
	}

	/* flush buffered writes */
	mb();

	ret = reset_control_deassert(ufs_qcom_phy->ufs_reset);
	if (ret)
		dev_err(dev, "Failed to deassert UFS PHY reset %d\n", ret);

	ret = ufs_qcom_phy_start_serdes(ufs_qcom_phy);
	if (ret)
		goto out;

	ret = ufs_qcom_phy_is_pcs_ready(ufs_qcom_phy);

out:
	return ret;
}
EXPORT_SYMBOL(ufs_qcom_phy_calibrate);

/*
 * This assumes the embedded phy structure inside generic_phy is of type
 * struct ufs_qcom_phy. In order to function properly it's crucial
 * to keep the embedded struct "struct ufs_qcom_phy common_cfg"
 * as the first inside generic_phy.
 */
struct ufs_qcom_phy *get_ufs_qcom_phy(struct phy *generic_phy)
{
	return (struct ufs_qcom_phy *)phy_get_drvdata(generic_phy);
}
EXPORT_SYMBOL(get_ufs_qcom_phy);

static
int ufs_qcom_phy_base_init(struct platform_device *pdev,
			   struct ufs_qcom_phy *phy_common)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int err = 0;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_mem");
	phy_common->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR((void const *)phy_common->mmio)) {
		err = PTR_ERR((void const *)phy_common->mmio);
		phy_common->mmio = NULL;
		dev_err(dev, "%s: ioremap for phy_mem resource failed %d\n",
			__func__, err);
		return err;
	}

	return 0;
}

struct phy *ufs_qcom_phy_generic_probe(struct platform_device *pdev,
				struct ufs_qcom_phy *common_cfg,
				const struct phy_ops *ufs_qcom_phy_gen_ops,
				struct ufs_qcom_phy_specific_ops *phy_spec_ops)
{
	int err;
	struct device *dev = &pdev->dev;
	struct phy *generic_phy = NULL;
	struct phy_provider *phy_provider;

	err = ufs_qcom_phy_base_init(pdev, common_cfg);
	if (err)
		return ERR_PTR(err);

	generic_phy = devm_phy_create(dev, NULL, ufs_qcom_phy_gen_ops);
	if (IS_ERR(generic_phy))
		return generic_phy;

	if (of_property_read_u32(dev->of_node, "lanes-per-direction",
				 &common_cfg->lanes_per_direction))
		common_cfg->lanes_per_direction =
			UFS_PHY_DEFAULT_LANES_PER_DIRECTION;

	/*
	 * UFS PHY power management is managed by its parent (UFS host
	 * controller) hence set the no runtime PM callbacks flag
	 * on UFS PHY device to avoid any accidental attempt to call the
	 * PM callbacks for PHY device.
	 */
	pm_runtime_no_callbacks(&generic_phy->dev);

	common_cfg->phy_spec_ops = phy_spec_ops;
	common_cfg->dev = dev;
	err = ufs_qcom_phy_init_clks(common_cfg);
	if (err)
		return ERR_PTR(err);
	err = ufs_qcom_phy_init_vregulators(common_cfg);
	if (err)
		return ERR_PTR(err);

	/*
	 * Init PHY register domain list. We use it to manage the memory space which be used
	 * to save UFS PHY register value.
	 */
	INIT_LIST_HEAD(&common_cfg->regs_list_head);

	phy_set_drvdata(generic_phy, common_cfg);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return ERR_CAST(phy_provider);
	return generic_phy;
}
EXPORT_SYMBOL(ufs_qcom_phy_generic_probe);

int ufs_qcom_phy_get_reset(struct ufs_qcom_phy *phy_common)
{
	struct reset_control *reset;

	if (phy_common->ufs_reset)
		return 0;

	reset = devm_reset_control_get_optional_exclusive(phy_common->dev, NULL);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	phy_common->ufs_reset = reset;
	return 0;
}
EXPORT_SYMBOL(ufs_qcom_phy_get_reset);

static int ufs_qcom_phy_init_symbol_clks(struct ufs_qcom_phy *phy)
{
	static const struct {
		const char *mux;
		const char *signal;
	} clocks[] = {
		{ "rx_sym0_mux_clk", "ufs_phy_rx_symbol_0_clk" },
		{ "rx_sym1_mux_clk", "ufs_phy_rx_symbol_1_clk" },
		{ "tx_sym0_mux_clk", "ufs_phy_tx_symbol_0_clk" },
	};
	struct device *dev = phy->dev;
	struct of_phandle_args gcc;
	int index, ret, i;

	/*
	 * The Kalama PHY node names the branch outputs downstream of each
	 * mux. Read the actual signal and XO inputs from the GCC node.
	 * ref_clk_src on the PHY is the separate, voted CXO pad clock.
	 */
	index = of_property_match_string(dev->of_node, "clock-names", clocks[0].mux);
	if (index < 0)
		return index;
	ret = of_parse_phandle_with_args(dev->of_node, "clocks", "#clock-cells",
					 index, &gcc);
	if (ret)
		return ret;

	phy->symbol_xo = devm_get_clk_from_child(dev, gcc.np, "bi_tcxo");
	if (IS_ERR(phy->symbol_xo)) {
		ret = dev_err_probe(dev, PTR_ERR(phy->symbol_xo), "missing symbol clock XO\n");
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(clocks); i++) {
		struct clk *clk = devm_clk_get(dev, clocks[i].mux);

		if (IS_ERR(clk)) {
			ret = dev_err_probe(dev, PTR_ERR(clk), "missing %s\n", clocks[i].mux);
			break;
		}
		phy->symbol[i].mux = clk;
		clk = devm_get_clk_from_child(dev, gcc.np, clocks[i].signal);
		if (IS_ERR(clk)) {
			ret = dev_err_probe(dev, PTR_ERR(clk), "missing %s\n", clocks[i].signal);
			break;
		}
		phy->symbol[i].signal = clk;
	}
out:
	of_node_put(gcc.np);
	return ret;
}

int ufs_qcom_phy_init_clks(struct ufs_qcom_phy *phy)
{
	struct {
		const char *name;
		struct clk **clk;
	} clocks[] = {
		{ "ref_clk_src", &phy->ref_clk_src },
		{ "ref_aux_clk", &phy->ref_aux_clk },
		{ "qref_clk", &phy->qref_clk },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(clocks); i++) {
		struct clk *clk = devm_clk_get(phy->dev, clocks[i].name);

		if (IS_ERR(clk))
			return dev_err_probe(phy->dev, PTR_ERR(clk),
					     "failed to get %s\n", clocks[i].name);
		*clocks[i].clk = clk;
	}

	return ufs_qcom_phy_init_symbol_clks(phy);
}
EXPORT_SYMBOL(ufs_qcom_phy_init_clks);

static int ufs_qcom_phy_init_vreg(struct device *dev,
				  struct ufs_qcom_phy_vreg *vreg,
				  const char *name)
{
	int err = 0;

	char prop_name[MAX_PROP_NAME];

	if (dev->of_node) {
		snprintf(prop_name, MAX_PROP_NAME, "%s-supply", name);
		if (!of_property_present(dev->of_node, prop_name)) {
			dev_dbg(dev, "No vreg data found for %s\n", prop_name);
			return -ENODATA;
		}
	}

	vreg->name = name;
	vreg->reg = devm_regulator_get(dev, name);
	if (IS_ERR(vreg->reg)) {
		err = PTR_ERR(vreg->reg);
		dev_err(dev, "failed to get %s, %d\n", name, err);
		goto out;
	}

	if (dev->of_node) {
		snprintf(prop_name, MAX_PROP_NAME, "%s-max-microamp", name);
		err = of_property_read_u32(dev->of_node,
					prop_name, &vreg->max_uA);
		if (err && err != -EINVAL) {
			dev_err(dev, "%s: failed to read %s\n",
					__func__, prop_name);
			goto out;
		} else if (err == -EINVAL || !vreg->max_uA) {
			if (!vreg->max_uA) {
				err = 0;
			} else if (regulator_count_voltages(vreg->reg) > 0) {
				dev_err(dev, "%s: %s is mandatory\n",
						__func__, prop_name);
				goto out;
			}
			err = 0;
		}
	}

	if (!strcmp(name, "vdda-pll")) {
		vreg->max_uV = VDDA_PLL_MAX_UV;
		vreg->min_uV = VDDA_PLL_MIN_UV;
	} else if (!strcmp(name, "vdda-phy")) {
		vreg->max_uV = VDDA_PHY_MAX_UV;
		vreg->min_uV = VDDA_PHY_MIN_UV;
	} else if (!strcmp(name, "vddp-ref-clk")) {
		vreg->max_uV = VDDP_REF_CLK_MAX_UV;
		vreg->min_uV = VDDP_REF_CLK_MIN_UV;
	} else if (!strcmp(name, "vdda-qref")) {
		vreg->max_uV = VDDA_QREF_MAX_UV;
		vreg->min_uV = VDDA_QREF_MIN_UV;
	}

out:
	return err;
}

int ufs_qcom_phy_init_vregulators(struct ufs_qcom_phy *phy)
{
	struct {
		const char *name;
		struct ufs_qcom_phy_vreg *vreg;
		bool optional;
	} supplies[] = {
		{ "vdda-pll", &phy->vdda_pll },
		{ "vdda-phy", &phy->vdda_phy },
		{ "vddp-ref-clk", &phy->vddp_ref_clk, true },
		{ "vdd-phy-gdsc", &phy->vdd_phy_gdsc, true },
		{ "vdda-qref", &phy->vdda_qref, true },
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(supplies); i++) {
		ret = ufs_qcom_phy_init_vreg(phy->dev, supplies[i].vreg,
					     supplies[i].name);
		if (ret == -ENODATA && supplies[i].optional)
			continue;
		if (ret)
			return dev_err_probe(phy->dev, ret, "failed to get %s\n",
					     supplies[i].name);
	}
	return 0;
}
EXPORT_SYMBOL(ufs_qcom_phy_init_vregulators);

static int ufs_qcom_phy_cfg_vreg(struct device *dev,
			  struct ufs_qcom_phy_vreg *vreg, bool on)
{
	int ret = 0;
	struct regulator *reg = vreg->reg;
	const char *name = vreg->name;
	int min_uV;
	int uA_load;

	if (regulator_count_voltages(reg) > 0) {
		min_uV = on ? vreg->min_uV : 0;
		ret = regulator_set_voltage(reg, min_uV, vreg->max_uV);
		if (ret) {
			dev_err(dev, "%s: %s set voltage failed, err=%d\n",
					__func__, name, ret);
			goto out;
		}
		uA_load = on ? vreg->max_uA : 0;
		ret = regulator_set_load(reg, uA_load);
		if (ret >= 0) {
			/*
			 * regulator_set_load() returns new regulator
			 * mode upon success.
			 */
			ret = 0;
		} else {
			dev_err(dev, "%s: %s set optimum mode(uA_load=%d) failed, err=%d\n",
					__func__, name, uA_load, ret);
			goto out;
		}
	}
out:
	return ret;
}

static int ufs_qcom_phy_enable_vreg(struct device *dev,
			     struct ufs_qcom_phy_vreg *vreg)
{
	int ret = 0;

	if (!vreg || vreg->enabled)
		goto out;

	ret = ufs_qcom_phy_cfg_vreg(dev, vreg, true);
	if (ret) {
		dev_err(dev, "%s: ufs_qcom_phy_cfg_vreg() failed, err=%d\n",
			__func__, ret);
		goto out;
	}

	ret = regulator_enable(vreg->reg);
	if (ret) {
		dev_err(dev, "%s: enable failed, err=%d\n",
				__func__, ret);
		goto out;
	}

	vreg->enabled = true;
out:
	return ret;
}

static int ufs_qcom_phy_enable_ref_clk(struct ufs_qcom_phy *phy)
{
	int ret = 0;

	if (phy->is_ref_clk_enabled)
		goto out;

	/* qref clk signal is optional */
	ret = clk_prepare_enable(phy->qref_clk);
	if (ret)
		return ret;
	/*
	 * reference clock is propagated in a daisy-chained manner from
	 * source to phy, so ungate them at each stage.
	 */
	ret = clk_prepare_enable(phy->ref_clk_src);
	if (ret) {
		dev_err(phy->dev, "%s: ref_clk_src enable failed %d\n",
				__func__, ret);
		goto out_disable_qref;
	}

	/*
	 * "ref_clk_parent" is optional clock hence make sure that clk reference
	 * is available before trying to enable the clock.
	 */
	if (phy->ref_clk_parent) {
		ret = clk_prepare_enable(phy->ref_clk_parent);
		if (ret) {
			dev_err(phy->dev, "%s: ref_clk_parent enable failed %d\n",
					__func__, ret);
			goto out_disable_src;
		}
	}

	/*
	 * "ref_clk" is optional clock hence make sure that clk reference
	 * is available before trying to enable the clock.
	 */
	if (phy->ref_clk) {
		ret = clk_prepare_enable(phy->ref_clk);
		if (ret) {
			dev_err(phy->dev, "%s: ref_clk enable failed %d\n",
					__func__, ret);
			goto out_disable_parent;
		}
	}

	/*
	 * "ref_aux_clk" is optional clock and only supported by certain
	 * phy versions, hence make sure that clk reference is available
	 * before trying to enable the clock.
	 */
	if (phy->ref_aux_clk) {
		ret = clk_prepare_enable(phy->ref_aux_clk);
		if (ret) {
			dev_err(phy->dev, "%s: ref_aux_clk enable failed %d\n",
					__func__, ret);
			goto out_disable_ref;
		}
	}

	phy->is_ref_clk_enabled = true;
	goto out;

out_disable_ref:
	if (phy->ref_clk)
		clk_disable_unprepare(phy->ref_clk);
out_disable_parent:
	if (phy->ref_clk_parent)
		clk_disable_unprepare(phy->ref_clk_parent);
out_disable_src:
	clk_disable_unprepare(phy->ref_clk_src);
out_disable_qref:
	clk_disable_unprepare(phy->qref_clk);
out:
	return ret;
}

static int ufs_qcom_phy_disable_vreg(struct device *dev,
			      struct ufs_qcom_phy_vreg *vreg)
{
	int ret = 0;

	if (!vreg || !vreg->enabled)
		goto out;

	ret = regulator_disable(vreg->reg);

	if (!ret) {
		/* ignore errors on applying disable config */
		ufs_qcom_phy_cfg_vreg(dev, vreg, false);
		vreg->enabled = false;
	} else {
		dev_err(dev, "%s: %s disable failed, err=%d\n",
				__func__, vreg->name, ret);
	}
out:
	return ret;
}

static void ufs_qcom_phy_disable_ref_clk(struct ufs_qcom_phy *phy)
{
	if (phy->is_ref_clk_enabled) {
		/*
		 * "ref_aux_clk" is optional clock and only supported by
		 * certain phy versions, hence make sure that clk reference
		 * is available before trying to disable the clock.
		 */
		if (phy->ref_aux_clk)
			clk_disable_unprepare(phy->ref_aux_clk);

		/*
		 * "ref_clk" is optional clock hence make sure that clk
		 * reference is available before trying to disable the clock.
		 */
		if (phy->ref_clk)
			clk_disable_unprepare(phy->ref_clk);

		/*
		 * "ref_clk_parent" is optional clock hence make sure that clk
		 * reference is available before trying to disable the clock.
		 */
		if (phy->ref_clk_parent)
			clk_disable_unprepare(phy->ref_clk_parent);
		clk_disable_unprepare(phy->ref_clk_src);

		/* qref clk signal is optional */
		if (phy->qref_clk)
			clk_disable_unprepare(phy->qref_clk);

		phy->is_ref_clk_enabled = false;
	}
}

/* Turn ON M-PHY RMMI interface clocks */
static int ufs_qcom_phy_enable_iface_clk(struct ufs_qcom_phy *phy)
{
	int ret = 0;

	if (phy->is_iface_clk_enabled)
		goto out;

	if (!phy->tx_iface_clk)
		goto out;

	ret = clk_prepare_enable(phy->tx_iface_clk);
	if (ret) {
		dev_err(phy->dev, "%s: tx_iface_clk enable failed %d\n",
				__func__, ret);
		goto out;
	}
	ret = clk_prepare_enable(phy->rx_iface_clk);
	if (ret) {
		clk_disable_unprepare(phy->tx_iface_clk);
		dev_err(phy->dev, "%s: rx_iface_clk enable failed %d. disabling also tx_iface_clk\n",
				__func__, ret);
		goto out;
	}
	phy->is_iface_clk_enabled = true;

out:
	return ret;
}

/* Turn OFF M-PHY RMMI interface clocks */
static void ufs_qcom_phy_disable_iface_clk(struct ufs_qcom_phy *phy)
{
	if (!phy->tx_iface_clk)
		return;

	if (phy->is_iface_clk_enabled) {
		clk_disable_unprepare(phy->tx_iface_clk);
		clk_disable_unprepare(phy->rx_iface_clk);
		phy->is_iface_clk_enabled = false;
	}
}

static int ufs_qcom_phy_start_serdes(struct ufs_qcom_phy *ufs_qcom_phy)
{
	int ret = 0;

	if (!ufs_qcom_phy->phy_spec_ops->start_serdes) {
		dev_err(ufs_qcom_phy->dev, "%s: start_serdes() callback is not supported\n",
			__func__);
		ret = -EOPNOTSUPP;
	} else {
		ufs_qcom_phy->phy_spec_ops->start_serdes(ufs_qcom_phy);
	}

	return ret;
}

void ufs_qcom_phy_set_tx_lane_enable(struct phy *generic_phy, u32 tx_lanes)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (ufs_qcom_phy->phy_spec_ops->set_tx_lane_enable)
		ufs_qcom_phy->phy_spec_ops->set_tx_lane_enable(ufs_qcom_phy,
							       tx_lanes);
}
EXPORT_SYMBOL(ufs_qcom_phy_set_tx_lane_enable);

int ufs_qcom_phy_save_controller_version(struct phy *generic_phy,
					  u8 major, u16 minor, u16 step)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (!ufs_qcom_phy)
		return -EPROBE_DEFER;

	ufs_qcom_phy->host_ctrl_rev_major = major;
	ufs_qcom_phy->host_ctrl_rev_minor = minor;
	ufs_qcom_phy->host_ctrl_rev_step = step;
	return 0;
}
EXPORT_SYMBOL(ufs_qcom_phy_save_controller_version);

static int ufs_qcom_phy_switch_symbol_clks(struct ufs_qcom_phy *phy, bool use_xo)
{
	struct clk *previous[ARRAY_SIZE(phy->symbol)];
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(phy->symbol); i++) {
		struct clk *mux = phy->symbol[i].mux;
		struct clk *parent = use_xo ? phy->symbol_xo : phy->symbol[i].signal;

		previous[i] = clk_get_parent(mux);
		if (!previous[i]) {
			ret = -ENODEV;
			goto restore;
		}
		ret = clk_set_parent(mux, parent);
		if (ret)
			goto restore;
	}
	return 0;

restore:
	dev_err(phy->dev, "failed to switch symbol mux %d to %s: %d\n",
		i, use_xo ? "XO" : "PHY", ret);
	while (i-- > 0) {
		if (clk_set_parent(phy->symbol[i].mux, previous[i]))
			dev_err(phy->dev, "failed to restore symbol mux %d\n", i);
	}
	return ret;
}

int ufs_qcom_phy_set_src_clk_h8_enter(struct phy *generic_phy)
{
	return ufs_qcom_phy_switch_symbol_clks(get_ufs_qcom_phy(generic_phy), true);
}
EXPORT_SYMBOL(ufs_qcom_phy_set_src_clk_h8_enter);

int ufs_qcom_phy_set_src_clk_h8_exit(struct phy *generic_phy)
{
	return ufs_qcom_phy_switch_symbol_clks(get_ufs_qcom_phy(generic_phy), false);
}
EXPORT_SYMBOL(ufs_qcom_phy_set_src_clk_h8_exit);

static int ufs_qcom_phy_is_pcs_ready(struct ufs_qcom_phy *ufs_qcom_phy)
{
	if (!ufs_qcom_phy->phy_spec_ops->is_physical_coding_sublayer_ready) {
		dev_err(ufs_qcom_phy->dev, "%s: is_physical_coding_sublayer_ready() callback is not supported\n",
			__func__);
		return -EOPNOTSUPP;
	}

	return ufs_qcom_phy->phy_spec_ops->is_physical_coding_sublayer_ready(ufs_qcom_phy);
}

int ufs_qcom_phy_power_on(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy_common = get_ufs_qcom_phy(generic_phy);
	struct device *dev = phy_common->dev;
	int err;

	if (phy_common->vdd_phy_gdsc.reg) {
		err = ufs_qcom_phy_enable_vreg(dev, &phy_common->vdd_phy_gdsc);
		if (err) {
			dev_err(dev, "%s enable phy_gdsc failed, err=%d\n",
				__func__, err);
			goto out;
		}
	}

	if (phy_common->vdda_qref.reg) {
		err = ufs_qcom_phy_enable_vreg(dev, &phy_common->vdda_qref);
		if (err) {
			dev_err(dev, "%s enable vdda_qref failed, err=%d\n",
				__func__, err);
			goto out_disable_gdsc;
		}
	}

	err = ufs_qcom_phy_enable_vreg(dev, &phy_common->vdda_phy);
	if (err) {
		dev_err(dev, "%s enable vdda_phy failed, err=%d\n",
			__func__, err);
		goto out_disable_qref;
	}

	phy_common->phy_spec_ops->power_control(phy_common, true);

	/* vdda_pll also enables ref clock LDOs so enable it first */
	err = ufs_qcom_phy_enable_vreg(dev, &phy_common->vdda_pll);
	if (err) {
		dev_err(dev, "%s enable vdda_pll failed, err=%d\n",
			__func__, err);
		goto out_disable_phy;
	}

	err = ufs_qcom_phy_enable_iface_clk(phy_common);
	if (err) {
		dev_err(dev, "%s enable phy iface clock failed, err=%d\n",
			__func__, err);
		goto out_disable_pll;
	}

	err = ufs_qcom_phy_enable_ref_clk(phy_common);
	if (err) {
		dev_err(dev, "%s enable phy ref clock failed, err=%d\n",
			__func__, err);
		goto out_disable_iface_clk;
	}

	/* enable device PHY ref_clk pad rail */
	if (phy_common->vddp_ref_clk.reg) {
		err = ufs_qcom_phy_enable_vreg(dev,
					       &phy_common->vddp_ref_clk);
		if (err) {
			dev_err(dev, "%s enable vddp_ref_clk failed, err=%d\n",
				__func__, err);
			goto out_disable_ref_clk;
		}
	}

	goto out;

out_disable_ref_clk:
	ufs_qcom_phy_disable_ref_clk(phy_common);
out_disable_iface_clk:
	ufs_qcom_phy_disable_iface_clk(phy_common);
out_disable_pll:
	ufs_qcom_phy_disable_vreg(dev, &phy_common->vdda_pll);
out_disable_phy:
	phy_common->phy_spec_ops->power_control(phy_common, false);
	ufs_qcom_phy_disable_vreg(dev, &phy_common->vdda_phy);
out_disable_qref:
	ufs_qcom_phy_disable_vreg(dev, &phy_common->vdda_qref);
out_disable_gdsc:
	ufs_qcom_phy_disable_vreg(dev, &phy_common->vdd_phy_gdsc);
out:
	return err;
}
EXPORT_SYMBOL(ufs_qcom_phy_power_on);

int ufs_qcom_phy_power_off(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy = get_ufs_qcom_phy(generic_phy);
	struct ufs_qcom_phy_vreg *supplies[] = {
		&phy->vdda_pll, &phy->vdda_phy, &phy->vdda_qref, &phy->vdd_phy_gdsc,
	};
	int i, ret, restore;

	phy->phy_spec_ops->power_control(phy, false);

	ret = ufs_qcom_phy_disable_vreg(phy->dev, &phy->vddp_ref_clk);
	if (ret)
		goto restore_power;
	ufs_qcom_phy_disable_ref_clk(phy);
	ufs_qcom_phy_disable_iface_clk(phy);

	for (i = 0; i < ARRAY_SIZE(supplies); i++) {
		ret = ufs_qcom_phy_disable_vreg(phy->dev, supplies[i]);
		if (ret)
			goto restore_power;
	}
	return 0;

restore_power:
	/* The PHY core keeps its power reference when this callback fails. */
	restore = ufs_qcom_phy_power_on(generic_phy);
	if (restore)
		dev_err(phy->dev, "failed to restore PHY power after rail error: %d\n",
			restore);
	return ret;
}
EXPORT_SYMBOL(ufs_qcom_phy_power_off);

void ufs_qcom_phy_ctrl_rx_linecfg(struct phy *generic_phy, bool ctrl)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (ufs_qcom_phy->phy_spec_ops->ctrl_rx_linecfg)
		ufs_qcom_phy->phy_spec_ops->ctrl_rx_linecfg(ufs_qcom_phy, ctrl);
}
EXPORT_SYMBOL(ufs_qcom_phy_ctrl_rx_linecfg);

int ufs_qcom_phy_dump_regs(struct ufs_qcom_phy *phy, int offset,
		int len, char *prefix)
{
	u32 *regs;
	size_t pos;

	if (offset % 4 != 0 || len % 4 != 0) /* keep readl happy */
		return -EINVAL;

	regs = kzalloc(len, GFP_KERNEL);
	if (!regs)
		return -ENOMEM;

	for (pos = 0; pos < len; pos += 4)
		regs[pos / 4] = readl_relaxed(phy->mmio + offset + pos);

	print_hex_dump(KERN_ERR, prefix,
			len > 4 ? DUMP_PREFIX_OFFSET : DUMP_PREFIX_NONE,
			16, 4, regs, len, false);
	kfree(regs);

	return 0;
}
EXPORT_SYMBOL(ufs_qcom_phy_dump_regs);

/**
 * ufs_qcom_phy_save_regs - save specified domain of ufs phy registers to memory
 * @phy - pointer to ufs qcom phy
 * @offset - register address offest
 * @len - size of this domain
 * @prefix - name of this domain
 */
int ufs_qcom_phy_save_regs(struct ufs_qcom_phy *phy, int offset,
		int len, char *prefix)
{
	struct ufs_qcom_phy_regs *regs = NULL;
	struct list_head *head = &phy->regs_list_head;
	size_t pos;
	unsigned int noio_flag;

	if (offset % 4 != 0 || len % 4 != 0)
		return -EINVAL;

	/* find the node if this register domain has been saved before */
	list_for_each_entry(regs, head, list)
		if (regs->prefix && !strcmp(regs->prefix, prefix))
			break;

	/* create a new node and add it to list if this domain never been written */
	if (&regs->list == head) {
		/*
		 * use memalloc_noio_save() here as GFP_ATOMIC should not be invoked
		 * in an IO error context
		 */
		noio_flag = memalloc_noio_save();
		regs = devm_kzalloc(phy->dev, sizeof(*regs), GFP_ATOMIC);
		if (!regs)
			goto out;
		regs->ptr = devm_kzalloc(phy->dev, len, GFP_ATOMIC);
		if (!regs->ptr)
			goto out;
		memalloc_noio_restore(noio_flag);
		regs->prefix = prefix;
		regs->len = len;
		list_add_tail(&regs->list, &phy->regs_list_head);
	}

	for (pos = 0; pos < len; pos += 4)
		regs->ptr[pos / 4] = readl_relaxed(phy->mmio + offset + pos);
	return 0;

out:
	memalloc_noio_restore(noio_flag);
	return -ENOMEM;
}
EXPORT_SYMBOL(ufs_qcom_phy_save_regs);

void ufs_qcom_phy_dbg_register_dump(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (ufs_qcom_phy->phy_spec_ops->dbg_register_dump)
		ufs_qcom_phy->phy_spec_ops->dbg_register_dump(ufs_qcom_phy);
}
EXPORT_SYMBOL(ufs_qcom_phy_dbg_register_dump);

void ufs_qcom_phy_dbg_register_save(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);

	if (ufs_qcom_phy->phy_spec_ops->dbg_register_save)
		ufs_qcom_phy->phy_spec_ops->dbg_register_save(ufs_qcom_phy);

}
EXPORT_SYMBOL(ufs_qcom_phy_dbg_register_save);

MODULE_DESCRIPTION("Universal Flash Storage (UFS) QCOM PHY");
MODULE_LICENSE("GPL v2");
