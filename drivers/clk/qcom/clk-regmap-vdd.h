/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __QCOM_CLK_REGMAP_VDD_H__
#define __QCOM_CLK_REGMAP_VDD_H__

#include <linux/types.h>
#include <dt-bindings/regulator/qcom,rpmh-regulator-levels.h>

struct clk_regmap;
struct device;
struct clk_vdd_state;
struct qcom_clk_boot_vote;

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
	bool autonomous_rate;
	struct clk_vdd_state *state;
};

#define QCOM_RPMH_CORNER(_rate, _level) { (_rate), RPMH_REGULATOR_LEVEL_##_level }
#define QCOM_CLK_VDD(_rails, ...) (&(struct clk_regmap_vdd) {			\
	.supplies = (_rails), .num_supplies = ARRAY_SIZE(_rails),		\
	.rates = (const struct clk_vdd_rate[]) { __VA_ARGS__ },			\
	.num_rates = sizeof((const struct clk_vdd_rate[]) { __VA_ARGS__ }) /	\
		     sizeof(struct clk_vdd_rate),					\
})

struct qcom_clk_boot_vote *devm_qcom_clk_vdd_boot_vote(struct device *dev,
			const char * const *supplies, unsigned int count, int corner);
void qcom_clk_vdd_drop_boot_vote(struct qcom_clk_boot_vote *vote);

int devm_clk_register_regmap_vdd(struct device *dev, struct clk_regmap *clkr);

#endif
