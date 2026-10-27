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

void mhi_controller_set_base(struct mhi_controller *ctrl, phys_addr_t base);
int mhi_controller_get_base(struct mhi_controller *ctrl, phys_addr_t *base);
u32 mhi_controller_get_numeric_id(struct mhi_controller *ctrl);
int mhi_get_channel_db_base(struct mhi_device *dev, phys_addr_t *addr);
int mhi_get_event_ring_db_base(struct mhi_device *dev, phys_addr_t *addr);
int mhi_device_configure(struct mhi_device *dev, enum dma_data_direction dir,
			 struct mhi_buf *cfg, int count);

bool mhi_scan_rddm_cookie(struct mhi_controller *ctrl, u32 cookie);
void mhi_debug_reg_dump(struct mhi_controller *ctrl);
void mhi_dump_sfr(struct mhi_controller *ctrl);
int mhi_force_reset(struct mhi_controller *ctrl);

void mhi_controller_set_bw_scale_cb(struct mhi_controller *ctrl,
				    int (*cb)(struct mhi_controller *, struct mhi_link_info *));

#endif
