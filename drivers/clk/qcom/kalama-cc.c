// SPDX-License-Identifier: GPL-2.0-only
/* Runtime power and clock handoff for the Kalama multimedia controllers. */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include "clk-alpha-pll.h"
#include "clk-regmap-vdd.h"
#include "common.h"
#include "kalama-cc.h"

struct kalama_cc {
	struct clk *iface;
	struct regulator_bulk_data *supplies;
	unsigned int count;
	struct qcom_clk_boot_vote *boot[];
};

static int kalama_cc_resume(struct device *dev)
{
	struct kalama_cc *cc = dev_get_drvdata(dev);
	int i, ret;

	for (i = 0; i < cc->count; i++) {
		ret = regulator_set_voltage(cc->supplies[i].consumer, RPMH_REGULATOR_LEVEL_MIN_SVS,
					    INT_MAX);
		if (ret)
			goto unvote;
	}
	ret = regulator_bulk_enable(cc->count, cc->supplies);
	if (ret)
		goto unvote;
	ret = clk_prepare_enable(cc->iface);
	if (!ret)
		return 0;
	regulator_bulk_disable(cc->count, cc->supplies);
unvote:
	while (i--)
		regulator_set_voltage(cc->supplies[i].consumer, 0, INT_MAX);
	return ret;
}

static int kalama_cc_suspend(struct device *dev)
{
	struct kalama_cc *cc = dev_get_drvdata(dev);
	int i, ret;

	clk_disable_unprepare(cc->iface);
	ret = regulator_bulk_disable(cc->count, cc->supplies);
	if (ret) {
		if (clk_prepare_enable(cc->iface))
			dev_err(dev, "failed to restore interface clock\n");
		return ret;
	}
	for (i = 0; i < cc->count; i++)
		if (regulator_set_voltage(cc->supplies[i].consumer, 0, INT_MAX))
			dev_warn_ratelimited(dev, "failed to drop %s corner\n", cc->supplies[i].supply);
	return 0;
}
DEFINE_RUNTIME_DEV_PM_OPS(kalama_cc_pm_ops, kalama_cc_suspend, kalama_cc_resume, NULL);
EXPORT_SYMBOL_GPL(kalama_cc_pm_ops);

int kalama_cc_configure_pll(struct regmap *map, struct clk_alpha_pll *pll, bool rivian)
{
	unsigned int mode;
	int ret = regmap_read(map, pll->offset, &mode);

	if (ret || (mode & BIT(0)))
		return ret;
	if (rivian)
		clk_rivian_evo_pll_configure(pll, map, pll->config);
	else
		clk_lucid_ole_pll_configure(pll, map, pll->config);
	return 0;
}
EXPORT_SYMBOL_GPL(kalama_cc_configure_pll);

int kalama_cc_probe(struct platform_device *pdev, const struct qcom_cc_desc *desc,
		    const struct kalama_cc_rail *rails, unsigned int count, bool has_iface,
		    int (*init_hw)(struct device *, struct regmap *))
{
	struct device *dev = &pdev->dev;
	struct kalama_cc *cc;
	struct regmap *map;
	int i, ret;

	cc = devm_kzalloc(dev, struct_size(cc, boot, count), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;
	cc->count = count;
	cc->supplies = devm_kcalloc(dev, count, sizeof(*cc->supplies), GFP_KERNEL);
	if (!cc->supplies)
		return -ENOMEM;
	platform_set_drvdata(pdev, cc);
	for (i = 0; i < count; i++)
		cc->supplies[i].supply = rails[i].name;
	ret = devm_regulator_bulk_get(dev, count, cc->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get controller supplies\n");
	if (has_iface) {
		cc->iface = devm_clk_get(dev, "iface");
		if (IS_ERR(cc->iface))
			return dev_err_probe(dev, PTR_ERR(cc->iface), "failed to get interface clock\n");
	}
	for (i = 0; i < count; i++) {
		cc->boot[i] = devm_qcom_clk_vdd_boot_vote(dev, &rails[i].name, 1, rails[i].boot_corner);
		if (IS_ERR(cc->boot[i]))
			return PTR_ERR(cc->boot[i]);
	}
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;
	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;
	map = qcom_cc_map(pdev, desc);
	ret = IS_ERR(map) ? PTR_ERR(map) : init_hw(dev, map);
	if (!ret)
		ret = qcom_cc_really_probe(dev, desc, map);
	pm_runtime_put_sync(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(kalama_cc_probe);

void kalama_cc_sync_state(struct device *dev)
{
	struct kalama_cc *cc = dev_get_drvdata(dev);
	int i;

	clk_sync_state(dev);
	for (i = 0; i < cc->count; i++)
		qcom_clk_vdd_drop_boot_vote(cc->boot[i]);
}
EXPORT_SYMBOL_GPL(kalama_cc_sync_state);
