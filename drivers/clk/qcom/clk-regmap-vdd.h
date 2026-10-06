/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __QCOM_CLK_REGMAP_VDD_H__
#define __QCOM_CLK_REGMAP_VDD_H__

#include <linux/types.h>

struct clk_regmap;
struct device;
struct clk_vdd_state;

struct clk_vdd_rate {
	unsigned long max_rate;
	int corner;
};

/* The regulator framework aggregates the independent vote of each clock. */
struct clk_regmap_vdd {
	const char * const *supplies;
	unsigned int num_supplies;
	const struct clk_vdd_rate *rates;
	unsigned int num_rates;
	struct clk_vdd_state *state;
};

int devm_clk_register_regmap_vdd(struct device *dev, struct clk_regmap *clkr);

#endif
