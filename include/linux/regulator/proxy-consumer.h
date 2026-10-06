// SPDX-License-Identifier: GPL-2.0-only
#ifndef _LINUX_REGULATOR_PROXY_CONSUMER_H
#define _LINUX_REGULATOR_PROXY_CONSUMER_H

struct device;
struct device_node;

int devm_regulator_proxy_consumer_register(struct device *dev, struct device_node *node);
void regulator_proxy_consumer_sync_state(struct device *dev);

#endif
