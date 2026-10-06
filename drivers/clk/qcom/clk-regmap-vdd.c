// SPDX-License-Identifier: GPL-2.0-only
/* RPMh ARC corner votes for regmap clocks with board-specific Fmax tables. */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include "clk-regmap.h"
#include "clk-regmap-vdd.h"

struct clk_vdd_state {
	struct clk_regmap *clkr;
	const struct clk_init_data *original;
	struct clk_init_data init;
	struct clk_ops ops;
	struct notifier_block notifier;
	struct regulator_bulk_data *supplies;
	bool enabled;
	int *corners;
};

static int clk_vdd_find_corner(const struct clk_regmap_vdd *vdd, unsigned long rate)
{
	int i;

	/* Fractional PLLs can report a few Hz above the nominal rate. */
	for (i = 0; i < vdd->num_rates; i++)
		if (DIV_ROUND_CLOSEST(rate, 1000) <= DIV_ROUND_CLOSEST(vdd->rates[i].max_rate, 1000))
			return vdd->rates[i].corner;

	return -ERANGE;
}

static int clk_vdd_set_corner(struct clk_vdd_state *s, int corner, bool raise_only)
{
	int i, ret, err = 0;

	/*
	 * Remember each successful vote. On a partial failure the caller can
	 * unwind only the votes that changed, even if a regulator needs a retry.
	 */
	for (i = 0; i < s->clkr->vdd->num_supplies; i++) {
		if (s->corners[i] == corner || (raise_only && s->corners[i] > corner))
			continue;
		ret = regulator_set_voltage(s->supplies[i].consumer, corner, INT_MAX);
		if (ret)
			err = ret;
		else
			s->corners[i] = corner;
	}
	return err;
}

static void clk_vdd_release_vote(struct clk_vdd_state *s)
{
	if (s->enabled) {
		if (regulator_bulk_disable(s->clkr->vdd->num_supplies, s->supplies))
			return;
		s->enabled = false;
	}
	clk_vdd_set_corner(s, 0, false);
}

static int clk_vdd_prepare(struct clk_hw *hw)
{
	struct clk_regmap_vdd *vdd = to_clk_regmap(hw)->vdd;
	struct clk_vdd_state *s = vdd->state;
	int ret, corner;

	corner = clk_vdd_find_corner(vdd, clk_hw_get_rate(hw));
	if (corner < 0)
		return corner;
	ret = clk_vdd_set_corner(s, corner, false);
	if (ret) {
		clk_vdd_release_vote(s);
		return ret;
	}
	if (!s->enabled) {
		ret = regulator_bulk_enable(vdd->num_supplies, s->supplies);
		if (ret) {
			clk_vdd_set_corner(s, 0, false);
			return ret;
		}
		s->enabled = true;
	}
	if (s->original->ops->prepare) {
		ret = s->original->ops->prepare(hw);
		if (ret)
			clk_vdd_release_vote(s);
	}
	return ret;
}

static void clk_vdd_unprepare(struct clk_hw *hw)
{
	struct clk_vdd_state *s = to_clk_regmap(hw)->vdd->state;

	if (s->original->ops->unprepare)
		s->original->ops->unprepare(hw);
	clk_vdd_release_vote(s);
}

/* CCF serializes prepare, unprepare and rate notifications. */
static int clk_vdd_rate_change(struct notifier_block *nb, unsigned long event, void *data)
{
	struct clk_vdd_state *s = container_of(nb, struct clk_vdd_state, notifier);
	struct clk_notifier_data *change = data;
	unsigned long rate = event == ABORT_RATE_CHANGE ? change->old_rate : change->new_rate;
	int corner;

	corner = clk_vdd_find_corner(s->clkr->vdd, rate);
	if (corner < 0)
		return notifier_from_errno(corner);
	if (!s->enabled)
		return NOTIFY_OK;
	return notifier_from_errno(clk_vdd_set_corner(s, corner, event == PRE_RATE_CHANGE));
}

static void clk_vdd_cleanup(void *data)
{
	struct clk_vdd_state *s = data;

	clk_vdd_release_vote(s);
	s->clkr->hw.init = s->original;
	s->clkr->vdd->state = NULL;
}

int devm_clk_register_regmap_vdd(struct device *dev, struct clk_regmap *clkr)
{
	struct clk_regmap_vdd *vdd = clkr->vdd;
	struct clk_vdd_state *s;
	struct clk *clk;
	int i, ret;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->supplies = devm_kcalloc(dev, vdd->num_supplies, sizeof(*s->supplies), GFP_KERNEL);
	s->corners = devm_kcalloc(dev, vdd->num_supplies, sizeof(*s->corners), GFP_KERNEL);
	if (!s->supplies || !s->corners)
		return -ENOMEM;
	for (i = 0; i < vdd->num_supplies; i++) {
		s->supplies[i].supply = vdd->supplies[i];
		s->supplies[i].consumer = devm_regulator_get_optional(dev, vdd->supplies[i]);
		if (IS_ERR(s->supplies[i].consumer))
			return dev_err_probe(dev, PTR_ERR(s->supplies[i].consumer),
					     "failed to get %s for %s\n", vdd->supplies[i],
					     clkr->hw.init->name);
	}

	s->clkr = clkr;
	s->original = clkr->hw.init;
	s->init = *clkr->hw.init;
	s->ops = *s->init.ops;
	s->ops.prepare = clk_vdd_prepare;
	s->ops.unprepare = clk_vdd_unprepare;
	s->init.ops = &s->ops;
	clkr->hw.init = &s->init;
	vdd->state = s;
	ret = devm_add_action_or_reset(dev, clk_vdd_cleanup, s);
	if (ret)
		return ret;

	ret = devm_clk_hw_register(dev, &clkr->hw);
	if (ret)
		return ret;
	clk = devm_clk_hw_get_clk(dev, &clkr->hw, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	s->notifier.notifier_call = clk_vdd_rate_change;
	return devm_clk_notifier_register(dev, clk, &s->notifier);
}
