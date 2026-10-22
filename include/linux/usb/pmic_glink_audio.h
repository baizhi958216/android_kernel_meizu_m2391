/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_USB_PMIC_GLINK_AUDIO_H
#define _LINUX_USB_PMIC_GLINK_AUDIO_H

struct notifier_block;

/* Reports 1 while the primary connector has an analog audio accessory. */
int pmic_glink_audio_register(struct notifier_block *nb);
int pmic_glink_audio_unregister(struct notifier_block *nb);

#endif
