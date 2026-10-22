/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_MEIZU_POWER_H
#define _LINUX_MEIZU_POWER_H
#include <linux/types.h>

/* Wire values recovered from xcharge_notify_subsys.ko, not power_supply enums. */
enum xcharge_notify_subsys_plug_type {
	USB_PLUG,
	WLS_PLUG,
	PLUG_TYPE_MAX
};


enum mbattery_plug_in_type {
	USB_IN,
	WLS_IN,
	MBATTERY_PLUG_TYPE_MAX
};

void xcharge_notify_subsys_set_plug_status(
		enum xcharge_notify_subsys_plug_type type, bool plugged);
void xcharge_mbattery_logger_update_plug_in_status(enum mbattery_plug_in_type type,
		int status);
int mpower_print(char *format, ...);
void xcharge_thermal_update_tbat(unsigned int tbat);
void xcharge_thermal_update_tusb(unsigned int tusb);
void xcharge_wls_status_get_handle(void);
void xcharge_chg_interface_register_cd(void);
#endif
