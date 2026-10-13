/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_PINCTRL_KALAMA_H
#define _LINUX_PINCTRL_KALAMA_H

struct device_node;
struct resource;

int kalama_gpio_get_mmio(struct device_node *np, unsigned int offset, struct resource *res);

#endif
