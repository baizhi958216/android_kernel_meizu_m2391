/* SPDX-License-Identifier: GPL-2.0-only */
/* Qualcomm MHI host extensions for low-power link handoff and offloaded channels. */
#ifndef _LINUX_MHI_MISC_H
#define _LINUX_MHI_MISC_H

#include <linux/mhi.h>

int mhi_pm_fast_suspend(struct mhi_controller *ctrl, bool notify_clients);
int mhi_pm_fast_resume(struct mhi_controller *ctrl, bool notify_clients);
void mhi_set_m2_timeout_ms(struct mhi_controller *ctrl, u32 timeout);
int mhi_device_get_sync_atomic(struct mhi_device *dev, int timeout_us, bool in_panic);
int mhi_report_error(struct mhi_controller *ctrl);

#endif
