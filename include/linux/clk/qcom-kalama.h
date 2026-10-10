/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_CLK_QCOM_KALAMA_H
#define _LINUX_CLK_QCOM_KALAMA_H

struct clk;

/* Return the RPMh ARC corner required for this rate. */
int qcom_clk_get_voltage(struct clk *clk, unsigned long rate);

#endif
