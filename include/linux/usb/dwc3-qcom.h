/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_USB_DWC3_QCOM_H
#define __LINUX_USB_DWC3_QCOM_H

#include <linux/usb/typec.h>

/* lanes is zero on detach, or two/four on DisplayPort entry. */
int dwc3_qcom_set_dp_lanes(struct device *dev, unsigned int lanes,
			   enum typec_orientation orientation);
#endif
