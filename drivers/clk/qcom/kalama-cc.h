/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __QCOM_KALAMA_CC_H
#define __QCOM_KALAMA_CC_H

#include <linux/pm.h>

struct clk_alpha_pll;
struct platform_device;
struct qcom_cc_desc;
struct regmap;

struct kalama_cc_rail {
	const char *name;
	int boot_corner;
};

int kalama_cc_probe(struct platform_device *pdev, const struct qcom_cc_desc *desc,
		    const struct kalama_cc_rail *rails, unsigned int count, bool has_iface,
		    int (*init_hw)(struct device *, struct regmap *));
int kalama_cc_configure_pll(struct regmap *map, struct clk_alpha_pll *pll, bool rivian);
void kalama_cc_sync_state(struct device *dev);
extern const struct dev_pm_ops kalama_cc_pm_ops;

#endif
