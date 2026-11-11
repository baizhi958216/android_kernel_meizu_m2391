// SPDX-License-Identifier: GPL-2.0-only
/* Keep the parser-visible prefix of the Kalama TSENS and ADC crash records. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/soc/qcom/apss_minidump.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

#include "thermal_minidump.h"

#define THERMAL_SAMPLES	50

struct thermal_history {
	char types[THERMAL_SAMPLES][THERMAL_NAME_LENGTH];
	s32 temperatures[THERMAL_SAMPLES];
	u32 count;
};
static_assert(offsetof(struct thermal_history, temperatures) == 1000);
static_assert(offsetof(struct thermal_history, count) == 1200);

struct thermal_minidump {
	struct thermal_history history;
	spinlock_t lock;
};

struct thermal_minidump *devm_thermal_minidump_register(struct device *dev, const char *name)
{
	struct thermal_minidump *md;
	int ret;

	md = devm_kzalloc(dev, sizeof(*md), GFP_KERNEL);
	if (!md)
		return NULL;
	spin_lock_init(&md->lock);
	ret = qcom_devm_minidump_add(dev, name, &md->history, sizeof(md->history));
	if (ret) {
		dev_warn(dev, "cannot register thermal crash samples: %d\n", ret);
		devm_kfree(dev, md);
		return NULL;
	}
	return md;
}
EXPORT_SYMBOL_GPL(devm_thermal_minidump_register);

void thermal_minidump_sample(struct thermal_minidump *md, const char *type, int temp)
{
	struct thermal_history *history;
	unsigned long flags;
	unsigned int i;

	if (!md)
		return;
	history = &md->history;
	spin_lock_irqsave(&md->lock, flags);
	i = history->count % THERMAL_SAMPLES;
	strscpy_pad(history->types[i], type);
	history->temperatures[i] = temp;
	smp_store_release(&history->count, i + 1);
	spin_unlock_irqrestore(&md->lock, flags);
}
EXPORT_SYMBOL_GPL(thermal_minidump_sample);

MODULE_DESCRIPTION("Qualcomm Kalama thermal crash history");
MODULE_LICENSE("GPL");
