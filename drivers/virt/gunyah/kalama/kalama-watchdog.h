/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KALAMA_WATCHDOG_H
#define KALAMA_WATCHDOG_H

#include <linux/platform_device.h>

struct kalama_wdt_ops {
	int (*enable)(bool on);
	int (*set_time)(u16 bark_ms, u16 bite_ms);
	int (*pet)(void);
	int (*status)(bool *enabled, u32 *elapsed);
};

int kalama_wdt_register(struct platform_device *pdev, const struct kalama_wdt_ops *ops);
void kalama_wdt_shutdown(struct platform_device *pdev);
extern const struct dev_pm_ops kalama_wdt_pm;

#endif
