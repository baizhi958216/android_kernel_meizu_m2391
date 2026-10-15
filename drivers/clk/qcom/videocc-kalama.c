// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include <dt-bindings/clock/qcom,videocc-kalama.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "reset.h"
#include "clk-regmap-vdd.h"
#include "kalama-cc.h"

static const char *const mm[] = { "vdd_mm" };
static const char *const mm_mxc[] = { "vdd_mm", "vdd_mxc" };
static const char *const mxc[] = { "vdd_mxc" };

static const struct kalama_cc_rail rails[] = {
	{ "vdd_mm", RPMH_REGULATOR_LEVEL_TURBO },
	{ "vdd_mxc", RPMH_REGULATOR_LEVEL_TURBO },
};

enum {
	P_BI_TCXO,
	P_SLEEP_CLK,
	P_VIDEO_CC_PLL0_OUT_MAIN,
	P_VIDEO_CC_PLL1_OUT_MAIN,
};

static struct pll_vco lucid_ole_vco[] = {
	{ 249600000, 2000000000, 0 },
};

static struct alpha_pll_config video_cc_pll0_config = {
	.l = 0x1E,
	.alpha = 0x0,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82AA299C,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000005,
};

static struct alpha_pll_config video_cc_pll0_config_kalama_v2 = {
	.l = 0x25,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82AA299C,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000005,
};

static struct clk_alpha_pll video_cc_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.config = &video_cc_pll0_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_reset_lucid_ole_ops,
		},
		.vdd = QCOM_CLK_VDD(mxc,
			QCOM_RPMH_CORNER(615000000, LOW_SVS_D1),
			QCOM_RPMH_CORNER(1100000000, SVS),
			QCOM_RPMH_CORNER(1600000000, SVS_L1),
			QCOM_RPMH_CORNER(2000000000, NOM)),
	},
};

static struct alpha_pll_config video_cc_pll1_config = {
	.l = 0x2B,
	.alpha = 0xC000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82AA299C,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000005,
};

static struct alpha_pll_config video_cc_pll1_config_kalama_v2 = {
	.l = 0x36,
	.alpha = 0xB000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82AA299C,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000005,
};

static struct clk_alpha_pll video_cc_pll1 = {
	.offset = 0x1000,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.config = &video_cc_pll1_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_pll1",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_reset_lucid_ole_ops,
		},
		.vdd = QCOM_CLK_VDD(mxc,
			QCOM_RPMH_CORNER(615000000, LOW_SVS_D1),
			QCOM_RPMH_CORNER(1100000000, SVS),
			QCOM_RPMH_CORNER(1600000000, SVS_L1),
			QCOM_RPMH_CORNER(2000000000, NOM)),
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data video_cc_parent_data_0_ao[] = {
	{ .fw_name = "bi_tcxo_ao" },
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_cc_pll0.clkr.hw },
};

static const struct parent_map video_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL1_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_cc_pll1.clkr.hw },
};

static const struct parent_map video_cc_parent_map_3[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data video_cc_parent_data_3[] = {
	{ .fw_name = "sleep_clk" },
};

static const struct freq_tbl ftbl_video_cc_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_ahb_clk_src = {
	.cmd_rcgr = 0x8030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_ahb_clk_src",
		.parent_data = video_cc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0_ao),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0_clk_src[] = {
	F(576000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(720000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1014000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1098000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1332000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1443000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_video_cc_mvs0_clk_src_kalama_v2[] = {
	F(720000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1014000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1098000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1332000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1600000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs0_clk_src = {
	.cmd_rcgr = 0x8000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_1,
	.freq_tbl = ftbl_video_cc_mvs0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_mvs0_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
	.clkr.vdd = QCOM_CLK_VDD(mm_mxc,
		QCOM_RPMH_CORNER(576000000, LOW_SVS_D1),
		QCOM_RPMH_CORNER(720000000, LOW_SVS),
		QCOM_RPMH_CORNER(1014000000, SVS),
		QCOM_RPMH_CORNER(1098000000, SVS_L1),
		QCOM_RPMH_CORNER(1332000000, NOM),
		QCOM_RPMH_CORNER(1443000000, TURBO)),
};

static const struct freq_tbl ftbl_video_cc_mvs1_clk_src[] = {
	F(840000000, P_VIDEO_CC_PLL1_OUT_MAIN, 1, 0, 0),
	F(1050000000, P_VIDEO_CC_PLL1_OUT_MAIN, 1, 0, 0),
	F(1350000000, P_VIDEO_CC_PLL1_OUT_MAIN, 1, 0, 0),
	F(1500000000, P_VIDEO_CC_PLL1_OUT_MAIN, 1, 0, 0),
	F(1650000000, P_VIDEO_CC_PLL1_OUT_MAIN, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_video_cc_mvs1_clk_src_kalama_v2[] = {
	F(1050000000, P_VIDEO_CC_PLL1_OUT_MAIN, 1, 0, 0),
	F(1350000000, P_VIDEO_CC_PLL1_OUT_MAIN, 1, 0, 0),
	F(1500000000, P_VIDEO_CC_PLL1_OUT_MAIN, 1, 0, 0),
	F(1650000000, P_VIDEO_CC_PLL1_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs1_clk_src = {
	.cmd_rcgr = 0x8018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_2,
	.freq_tbl = ftbl_video_cc_mvs1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_mvs1_clk_src",
		.parent_data = video_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
	.clkr.vdd = QCOM_CLK_VDD(mm_mxc,
		QCOM_RPMH_CORNER(840000000, LOW_SVS_D1),
		QCOM_RPMH_CORNER(1050000000, LOW_SVS),
		QCOM_RPMH_CORNER(1350000000, SVS),
		QCOM_RPMH_CORNER(1500000000, SVS_L1),
		QCOM_RPMH_CORNER(1650000000, NOM)),
};

static const struct freq_tbl ftbl_video_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_sleep_clk_src = {
	.cmd_rcgr = 0x8128,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_3,
	.freq_tbl = ftbl_video_cc_sleep_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_sleep_clk_src",
		.parent_data = video_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd = QCOM_CLK_VDD(mm,
		QCOM_RPMH_CORNER(32000, LOW_SVS_D1)),
};

static struct clk_rcg2 video_cc_xo_clk_src = {
	.cmd_rcgr = 0x810c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_xo_clk_src",
		.parent_data = video_cc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0_ao),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div video_cc_mvs0_div_clk_src = {
	.reg = 0x80c4,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "video_cc_mvs0_div_clk_src",
		.parent_hws = (const struct clk_hw *[]){
			&video_cc_mvs0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div video_cc_mvs0c_div2_div_clk_src = {
	.reg = 0x8070,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "video_cc_mvs0c_div2_div_clk_src",
		.parent_hws = (const struct clk_hw *[]){
			&video_cc_mvs0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div video_cc_mvs1_div_clk_src = {
	.reg = 0x80ec,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "video_cc_mvs1_div_clk_src",
		.parent_hws = (const struct clk_hw *[]){
			&video_cc_mvs1_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div video_cc_mvs1c_div2_div_clk_src = {
	.reg = 0x809c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "video_cc_mvs1c_div2_div_clk_src",
		.parent_hws = (const struct clk_hw *[]){
			&video_cc_mvs1_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch video_cc_mvs0_clk = {
	.halt_reg = 0x80b8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80b8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80b8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvs0_clk",
			.parent_hws = (const struct clk_hw *[]){
				&video_cc_mvs0_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0c_clk = {
	.halt_reg = 0x8064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvs0c_clk",
			.parent_hws = (const struct clk_hw *[]){
				&video_cc_mvs0c_div2_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs1_clk = {
	.halt_reg = 0x80e0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80e0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80e0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvs1_clk",
			.parent_hws = (const struct clk_hw *[]){
				&video_cc_mvs1_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs1c_clk = {
	.halt_reg = 0x8090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvs1c_clk",
			.parent_hws = (const struct clk_hw *[]){
				&video_cc_mvs1c_div2_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_ahb_clk = {
	.halt_reg = 0x80f4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x80f4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "video_cc_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &video_cc_ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_aon_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch video_cc_xo_clk = {
	.halt_reg = 0x8124,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8124,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "video_cc_xo_clk",
			.parent_hws = (const struct clk_hw *[]){ &video_cc_xo_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_aon_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch video_cc_sleep_clk = {
	.halt_reg = 0x8140,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8140,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "video_cc_sleep_clk",
			.parent_hws = (const struct clk_hw *[]){ &video_cc_sleep_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_aon_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_regmap *video_cc_kalama_clocks[] = {
	[VIDEO_CC_AHB_CLK] = &video_cc_ahb_clk.clkr,
	[VIDEO_CC_AHB_CLK_SRC] = &video_cc_ahb_clk_src.clkr,
	[VIDEO_CC_MVS0_CLK] = &video_cc_mvs0_clk.clkr,
	[VIDEO_CC_MVS0_CLK_SRC] = &video_cc_mvs0_clk_src.clkr,
	[VIDEO_CC_MVS0_DIV_CLK_SRC] = &video_cc_mvs0_div_clk_src.clkr,
	[VIDEO_CC_MVS0C_CLK] = &video_cc_mvs0c_clk.clkr,
	[VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC] = &video_cc_mvs0c_div2_div_clk_src.clkr,
	[VIDEO_CC_MVS1_CLK] = &video_cc_mvs1_clk.clkr,
	[VIDEO_CC_MVS1_CLK_SRC] = &video_cc_mvs1_clk_src.clkr,
	[VIDEO_CC_MVS1_DIV_CLK_SRC] = &video_cc_mvs1_div_clk_src.clkr,
	[VIDEO_CC_MVS1C_CLK] = &video_cc_mvs1c_clk.clkr,
	[VIDEO_CC_MVS1C_DIV2_DIV_CLK_SRC] = &video_cc_mvs1c_div2_div_clk_src.clkr,
	[VIDEO_CC_PLL0] = &video_cc_pll0.clkr,
	[VIDEO_CC_PLL1] = &video_cc_pll1.clkr,
	[VIDEO_CC_SLEEP_CLK] = &video_cc_sleep_clk.clkr,
	[VIDEO_CC_SLEEP_CLK_SRC] = &video_cc_sleep_clk_src.clkr,
	[VIDEO_CC_XO_CLK] = &video_cc_xo_clk.clkr,
	[VIDEO_CC_XO_CLK_SRC] = &video_cc_xo_clk_src.clkr,
};

static const struct qcom_reset_map video_cc_kalama_resets[] = {
	[CVP_VIDEO_CC_INTERFACE_BCR] = { 0x80f0 },
	[CVP_VIDEO_CC_MVS0_BCR] = { 0x80a0 },
	[VIDEO_CC_MVS0C_CLK_ARES] = { 0x8064, 2 },
	[CVP_VIDEO_CC_MVS0C_BCR] = { 0x8048 },
	[CVP_VIDEO_CC_MVS1_BCR] = { 0x80c8 },
	[VIDEO_CC_MVS1C_CLK_ARES] = { 0x8090, 2 },
	[CVP_VIDEO_CC_MVS1C_BCR] = { 0x8074 },
};

static const struct regmap_config video_cc_kalama_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9f4c,
	.fast_io = true,
};

static const struct qcom_cc_desc video_cc_kalama_desc = {
	.config = &video_cc_kalama_regmap_config,
	.clks = video_cc_kalama_clocks,
	.num_clks = ARRAY_SIZE(video_cc_kalama_clocks),
	.resets = video_cc_kalama_resets,
	.num_resets = ARRAY_SIZE(video_cc_kalama_resets),
};

static const struct of_device_id video_cc_kalama_match_table[] = {
	{ .compatible = "qcom,kalama-videocc" },
	{ .compatible = "qcom,kalama-videocc-v2" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_kalama_match_table);

static const struct clk_vdd_rate video_cc_mvs0_clk_src_v1[] = {
	QCOM_RPMH_CORNER(576000000, LOW_SVS_D1),
	QCOM_RPMH_CORNER(720000000, LOW_SVS),
	QCOM_RPMH_CORNER(1014000000, SVS),
	QCOM_RPMH_CORNER(1098000000, SVS_L1),
	QCOM_RPMH_CORNER(1332000000, NOM),
	QCOM_RPMH_CORNER(1443000000, TURBO),
};
static const struct clk_vdd_rate video_cc_mvs0_clk_src_v2[] = {
	QCOM_RPMH_CORNER(720000000, LOW_SVS),
	QCOM_RPMH_CORNER(1014000000, SVS),
	QCOM_RPMH_CORNER(1098000000, SVS_L1),
	QCOM_RPMH_CORNER(1332000000, NOM),
	QCOM_RPMH_CORNER(1600000000, TURBO),
};
static const struct clk_vdd_rate video_cc_mvs1_clk_src_v1[] = {
	QCOM_RPMH_CORNER(840000000, LOW_SVS_D1),
	QCOM_RPMH_CORNER(1050000000, LOW_SVS),
	QCOM_RPMH_CORNER(1350000000, SVS),
	QCOM_RPMH_CORNER(1500000000, SVS_L1),
	QCOM_RPMH_CORNER(1650000000, NOM),
};
static const struct clk_vdd_rate video_cc_mvs1_clk_src_v2[] = {
	QCOM_RPMH_CORNER(1050000000, LOW_SVS),
	QCOM_RPMH_CORNER(1350000000, SVS),
	QCOM_RPMH_CORNER(1500000000, SVS_L1),
	QCOM_RPMH_CORNER(1650000000, NOM),
};
static const struct clk_vdd_rate video_cc_sleep_clk_src_v1[] = {
	QCOM_RPMH_CORNER(32000, LOW_SVS_D1),
};
static const struct clk_vdd_rate video_cc_sleep_clk_src_v2[] = {
	QCOM_RPMH_CORNER(32000, LOW_SVS),
};

static int video_cc_init(struct device *dev, struct regmap *map)
{
	bool v2 = of_device_is_compatible(dev->of_node, "qcom,kalama-videocc-v2");
	int ret;

	video_cc_pll0.config = v2 ? &video_cc_pll0_config_kalama_v2 : &video_cc_pll0_config;
	video_cc_pll1.config = v2 ? &video_cc_pll1_config_kalama_v2 : &video_cc_pll1_config;
	video_cc_mvs0_clk_src.freq_tbl = v2 ? ftbl_video_cc_mvs0_clk_src_kalama_v2 : ftbl_video_cc_mvs0_clk_src;
	video_cc_mvs1_clk_src.freq_tbl = v2 ? ftbl_video_cc_mvs1_clk_src_kalama_v2 : ftbl_video_cc_mvs1_clk_src;
	video_cc_mvs0_clk_src.clkr.vdd->rates = v2 ? video_cc_mvs0_clk_src_v2 : video_cc_mvs0_clk_src_v1;
	video_cc_mvs0_clk_src.clkr.vdd->num_rates = v2 ? ARRAY_SIZE(video_cc_mvs0_clk_src_v2) : ARRAY_SIZE(video_cc_mvs0_clk_src_v1);
	video_cc_mvs1_clk_src.clkr.vdd->rates = v2 ? video_cc_mvs1_clk_src_v2 : video_cc_mvs1_clk_src_v1;
	video_cc_mvs1_clk_src.clkr.vdd->num_rates = v2 ? ARRAY_SIZE(video_cc_mvs1_clk_src_v2) : ARRAY_SIZE(video_cc_mvs1_clk_src_v1);
	video_cc_sleep_clk_src.clkr.vdd->rates = v2 ? video_cc_sleep_clk_src_v2 : video_cc_sleep_clk_src_v1;
	video_cc_sleep_clk_src.clkr.vdd->num_rates = v2 ? ARRAY_SIZE(video_cc_sleep_clk_src_v2) : ARRAY_SIZE(video_cc_sleep_clk_src_v1);
	ret = kalama_cc_configure_pll(map, &video_cc_pll0, false);
	if (ret)
		return ret;
	ret = kalama_cc_configure_pll(map, &video_cc_pll1, false);
	if (ret)
		return ret;
	ret = regmap_set_bits(map, 0x80f4, BIT(0));
	if (ret)
		return ret;
	ret = regmap_set_bits(map, 0x8124, BIT(0));
	if (ret)
		return ret;
	ret = regmap_set_bits(map, 0x8140, BIT(0));
	if (ret)
		return ret;
	return 0;
}

static int video_cc_probe(struct platform_device *pdev)
{
	return kalama_cc_probe(pdev, &video_cc_kalama_desc, rails, ARRAY_SIZE(rails), true,
			       video_cc_init);
}

static struct platform_driver video_cc_driver = {
	.probe = video_cc_probe,
	.driver = {
		.name = "videocc-kalama",
		.of_match_table = video_cc_kalama_match_table,
		.pm = pm_ptr(&kalama_cc_pm_ops),
		.sync_state = kalama_cc_sync_state,
	},
};
module_platform_driver(video_cc_driver);

MODULE_DESCRIPTION("Qualcomm Kalama video clock controller");
MODULE_LICENSE("GPL");
