/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#ifndef __LINUX_USB_DWC3_MSM_H
#define __LINUX_USB_DWC3_MSM_H

#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/usb/gadget.h>

/* Operations codes for GSI enabled EPs */
enum gsi_ep_op {
	GSI_EP_OP_CONFIG = 0,
	GSI_EP_OP_STARTXFER,
	GSI_EP_OP_STORE_DBL_INFO,
	GSI_EP_OP_ENABLE_GSI,
	GSI_EP_OP_UPDATEXFER,
	GSI_EP_OP_RING_DB,
	GSI_EP_OP_ENDXFER,
	GSI_EP_OP_GET_CH_INFO,
	GSI_EP_OP_GET_XFER_IDX,
	GSI_EP_OP_PREPARE_TRBS,
	GSI_EP_OP_FREE_TRBS,
	GSI_EP_OP_SET_CLR_BLOCK_DBL,
	GSI_EP_OP_CHECK_FOR_SUSPEND,
	GSI_EP_OP_DISABLE,
};

enum usb_hw_ep_mode {
	USB_EP_NONE,
	USB_EP_BAM,
	USB_EP_GSI,
	USB_EP_EBC,
};

/*
 * @buf_base_addr: Base pointer to buffer allocated for each GSI enabled EP.
 *	TRBs point to buffers that are split from this pool. The size of the
 *	buffer is num_bufs times buf_len. num_bufs and buf_len are determined
	based on desired performance and aggregation size.
 * @dma: DMA address corresponding to buf_base_addr.
 * @num_bufs: Number of buffers associated with the GSI enabled EP. This
 *	corresponds to the number of non-zlp TRBs allocated for the EP.
 *	The value is determined based on desired performance for the EP.
 * @buf_len: Size of each individual buffer is determined based on aggregation
 *	negotiated as per the protocol. In case of no aggregation supported by
 *	the protocol, we use default values.
 * @db_reg_phs_addr_lsb: IPA channel doorbell register's physical address LSB
 * @mapped_db_reg_phs_addr_lsb: doorbell LSB IOVA address mapped with IOMMU
 * @db_reg_phs_addr_msb: IPA channel doorbell register's physical address MSB
 * @ep_intr_num: Interrupter number for EP.
 */
struct usb_gsi_request {
	void *buf_base_addr;
	dma_addr_t dma;
	size_t num_bufs;
	size_t buf_len;
	u32 db_reg_phs_addr_lsb;
	dma_addr_t mapped_db_reg_phs_addr_lsb;
	u32 db_reg_phs_addr_msb;
	u8 ep_intr_num;
	struct sg_table sgt_trb_xfer_ring;
	struct sg_table sgt_data_buff;
	struct dwc3_trb *ring;
	dma_addr_t ring_dma;
	unsigned int ring_size;
	void __iomem *doorbell_lo;
	void __iomem *doorbell_hi;
};

/*
 * @last_trb_addr: Address (LSB - based on alignment restrictions) of
 *	last TRB in queue. Used to identify rollover case.
 * @const_buffer_size: TRB buffer size in KB (similar to IPA aggregation
 *	configuration). Must be aligned to Max USB Packet Size.
 *	Should be 1 <= const_buffer_size <= 31.
 * @depcmd_low_addr: Used by GSI hardware to write "Update Transfer" cmd
 * @depcmd_hi_addr: Used to write "Update Transfer" command.
 * @gevntcount_low_addr: GEVNCOUNT low address for GSI hardware to read and
 *	clear processed events.
 * @gevntcount_hi_addr:	GEVNCOUNT high address.
 * @xfer_ring_len: length of transfer ring in bytes (must be integral
 *	multiple of TRB size - 16B for xDCI).
 * @xfer_ring_base_addr: physical base address of transfer ring. Address must
 *	be aligned to xfer_ring_len rounded to power of two.
 * @ch_req: Used to pass request specific info for certain operations on GSI EP
 */
struct gsi_channel_info {
	u16 last_trb_addr;
	u8 const_buffer_size;
	u32 depcmd_low_addr;
	u8 depcmd_hi_addr;
	u32 gevntcount_low_addr;
	u8 gevntcount_hi_addr;
	u16 xfer_ring_len;
	u64 xfer_ring_base_addr;
	struct usb_gsi_request *ch_req;
};

/**
 * usb_gadget_autopm_get - increment PM-usage counter of usb gadget's parent
 * device.
 * @gadget: usb gadget whose parent device counter is incremented
 *
 * This routine should be called by function driver when it wants to use
 * gadget's parent device and needs to guarantee that it is not suspended. In
 * addition, the routine prevents subsequent autosuspends of gadget's parent
 * device. However if the autoresume fails then the counter is re-decremented.
 *
 * This routine can run only in process context.
 */
static inline int usb_gadget_autopm_get(struct usb_gadget *gadget)
{
	int status = -ENODEV;

	if (!gadget || !gadget->dev.parent)
		return status;

	status = pm_runtime_get_sync(gadget->dev.parent);
	if (status < 0)
		pm_runtime_put_sync(gadget->dev.parent);

	if (status > 0)
		status = 0;
	return status;
}

/**
 * usb_gadget_autopm_get_async - increment PM-usage counter of usb gadget's
 * parent device.
 * @gadget: usb gadget whose parent device counter is incremented
 *
 * This routine increments @gadget parent device PM usage counter and queue an
 * autoresume request if the device is suspended. It does not autoresume device
 * directly (it only queues a request). After a successful call, the device may
 * not yet be resumed.
 *
 * This routine can run in atomic context.
 */
static inline int usb_gadget_autopm_get_async(struct usb_gadget *gadget)
{
	int status = -ENODEV;

	if (!gadget || !gadget->dev.parent)
		return status;

	status = pm_runtime_get(gadget->dev.parent);
	if (status < 0 && status != -EINPROGRESS)
		pm_runtime_put_noidle(gadget->dev.parent);

	if (status > 0 || status == -EINPROGRESS)
		status = 0;
	return status;
}

/**
 * usb_gadget_autopm_get_noresume - increment PM-usage counter of usb gadget's
 * parent device.
 * @gadget: usb gadget whose parent device counter is incremented
 *
 * This routine increments PM-usage count of @gadget parent device but does not
 * carry out an autoresume.
 *
 * This routine can run in atomic context.
 */
static inline void usb_gadget_autopm_get_noresume(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_get_noresume(gadget->dev.parent);
}

/**
 * usb_gadget_autopm_put - decrement PM-usage counter of usb gadget's parent
 * device.
 * @gadget: usb gadget whose parent device counter is decremented.
 *
 * This routine should be called by function driver when it is finished using
 * @gadget parent device and wants to allow it to autosuspend. It decrements
 * PM-usage counter of @gadget parent device, when the counter reaches 0, a
 * delayed autosuspend request is attempted.
 *
 * This routine can run only in process context.
 */
static inline void usb_gadget_autopm_put(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_put_sync(gadget->dev.parent);
}

/**
 * usb_gadget_autopm_put_async - decrement PM-usage counter of usb gadget's
 * parent device.
 * @gadget: usb gadget whose parent device counter is decremented.
 *
 * This routine decrements PM-usage counter of @gadget parent device and
 * schedules a delayed autosuspend request if the counter is <= 0.
 *
 * This routine can run in atomic context.
 */
static inline void usb_gadget_autopm_put_async(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_put(gadget->dev.parent);
}

int usb_gsi_ep_op(struct usb_ep *ep, void *data, enum gsi_ep_op op);
int msm_ep_set_mode(struct usb_ep *ep, enum usb_hw_ep_mode mode);
#endif
