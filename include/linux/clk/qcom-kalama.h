/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_CLK_QCOM_KALAMA_H
#define _LINUX_CLK_QCOM_KALAMA_H

struct clk;

enum qcom_clk_retention {
	CLKFLAG_RETAIN_PERIPH,
	CLKFLAG_NORETAIN_PERIPH,
	CLKFLAG_RETAIN_MEM,
	CLKFLAG_NORETAIN_MEM,
};

int qcom_clk_set_flags(struct clk *clk, enum qcom_clk_retention flag);

/* Return the RPMh ARC corner required for this rate. */
int qcom_clk_get_voltage(struct clk *clk, unsigned long rate);

#endif
