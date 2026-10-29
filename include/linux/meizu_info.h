/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_DEVICE_INFO_H
#define _MEIZU_DEVICE_INFO_H
#include <linux/types.h>
const char *meizu_get_sn(void);
const char *meizu_get_chipid(void);
int bootloader_is_unlock(void);
int is_secure_chip(void);
u32 nfc_read_rf_id(void);
bool is_minidump(void);
unsigned int cs_get_lcd_id(void);
bool is_white_lcd(void);
#endif
