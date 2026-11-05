// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm USB GSI packet offload, derived from the Qualcomm DWC3 glue. */
/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved. */

#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/usb/dwc3-msm.h>
#include "core.h"
#include "gadget.h"
#include "gadget-gsi.h"
#include "io.h"

enum gsi_register {
	GSI_CFG, GSI_DB_LO, GSI_DB_HI, GSI_RING_LO, GSI_RING_HI, GSI_STATUS,
	GSI_REGISTER_COUNT,
};

struct gsi_event {
	void *buf;
	dma_addr_t dma;
};

struct dwc3_gsi {
	struct device *dev;
	void __iomem *scratch;
	u32 offset[GSI_REGISTER_COUNT];
	unsigned int count;
	unsigned int length;
	void *dummy;
	dma_addr_t dummy_dma;
	struct gsi_event events[];
};

static void gsi_free_events(void *data)
{
	struct dwc3_gsi *gsi = data;
	unsigned int i;

	for (i = 0; i < gsi->count; i++)
		if (gsi->events[i].buf)
			dma_free_coherent(gsi->dev, gsi->length,
					  gsi->events[i].buf, gsi->events[i].dma);
	if (gsi->dummy)
		dma_free_coherent(gsi->dev, sizeof(u32), gsi->dummy, gsi->dummy_dma);
}

int dwc3_gsi_alloc_events(struct dwc3 *dwc, unsigned int length)
{
	struct device_node *np = dwc->dev->parent->of_node;
	void __iomem **scratch = dev_get_platdata(dwc->dev);
	struct dwc3_gsi *gsi;
	u32 count;
	int ret, i;

	if (!of_device_is_compatible(np, "qcom,dwc-usb3-msm") ||
	    !of_property_present(np, "qcom,num-gsi-evt-buffs"))
		return 0;
	ret = of_property_read_u32(np, "qcom,num-gsi-evt-buffs", &count);
	if (ret || !count || count > 31 || !scratch)
		return -EINVAL;

	gsi = devm_kzalloc(dwc->dev, struct_size(gsi, events, count), GFP_KERNEL);
	if (!gsi)
		return -ENOMEM;
	ret = of_property_read_u32_array(np, "qcom,gsi-reg-offset", gsi->offset,
					 GSI_REGISTER_COUNT);
	if (ret)
		return ret;
	for (i = 0; i < GSI_REGISTER_COUNT; i++) {
		unsigned int n = i == GSI_CFG || i == GSI_STATUS ? 1 : count;

		if (!IS_ALIGNED(gsi->offset[i], 4) || gsi->offset[i] > 0x400 - n * 4)
			return -EINVAL;
	}
	gsi->dev = dwc->sysdev;
	gsi->count = count;
	gsi->length = length;
	gsi->scratch = *scratch;
	ret = devm_add_action_or_reset(dwc->dev, gsi_free_events, gsi);
	if (ret)
		return ret;
	gsi->dummy = dma_alloc_coherent(gsi->dev, sizeof(u32), &gsi->dummy_dma,
					GFP_KERNEL);
	if (!gsi->dummy)
		return -ENOMEM;
	for (i = 0; i < count; i++) {
		struct gsi_event *evt = &gsi->events[i];

		evt->buf = dma_alloc_coherent(gsi->dev, length, &evt->dma, GFP_KERNEL);
		if (!evt->buf)
			return -ENOMEM;
	}
	dwc->gsi = gsi;
	return 0;
}

void dwc3_gsi_set_events(struct dwc3 *dwc, bool enable)
{
	struct dwc3_gsi *gsi = dwc->gsi;
	unsigned int i;

	if (!gsi)
		return;
	for (i = 1; i <= gsi->count; i++) {
		struct gsi_event *evt = &gsi->events[i - 1];
		u32 hi = (upper_32_bits(evt->dma) & 0xffff) | (3 << 22) | (i << 16);
		u32 pending = dwc3_readl(dwc, DWC3_GEVNTCOUNT(i));

		if (enable)
			memset(evt->buf, 0, gsi->length);
		dwc3_writel(dwc, DWC3_GEVNTADRLO(i), enable ? lower_32_bits(evt->dma) : 0);
		dwc3_writel(dwc, DWC3_GEVNTADRHI(i), enable ? hi : 0);
		dwc3_writel(dwc, DWC3_GEVNTSIZ(i), DWC3_GEVNTSIZ_INTMASK |
			    DWC3_GEVNTSIZ_SIZE(enable ? gsi->length : 0));
		dwc3_writel(dwc, DWC3_GEVNTCOUNT(i), pending);
	}
}

static void gsi_write_addr(struct dwc3_gsi *gsi, enum gsi_register lo,
			   unsigned int intr, dma_addr_t addr)
{
	unsigned int index = (intr - 1) * sizeof(u32);

	writel(upper_32_bits(addr), gsi->scratch + gsi->offset[lo + 1] + index);
	writel(lower_32_bits(addr), gsi->scratch + gsi->offset[lo] + index);
}

static void gsi_free_ring(struct dwc3 *dwc, struct usb_gsi_request *req)
{
	struct device *dev = dwc->sysdev;

	if (req->doorbell_lo) {
		gsi_write_addr(dwc->gsi, GSI_DB_LO, req->ep_intr_num, dwc->gsi->dummy_dma);
		dma_unmap_resource(dev, req->mapped_db_reg_phs_addr_lsb & PAGE_MASK,
				   PAGE_SIZE, DMA_BIDIRECTIONAL, 0);
		iounmap(req->doorbell_lo);
		iounmap(req->doorbell_hi);
		req->doorbell_lo = NULL;
		req->doorbell_hi = NULL;
		req->mapped_db_reg_phs_addr_lsb = 0;
	}
	if (req->sgt_trb_xfer_ring.sgl)
		sg_free_table(&req->sgt_trb_xfer_ring);
	if (req->ring)
		dma_free_coherent(dev, req->ring_size, req->ring, req->ring_dma);
	req->ring = NULL;
	if (req->sgt_data_buff.sgl)
		sg_free_table(&req->sgt_data_buff);
	if (req->buf_base_addr)
		dma_free_coherent(dev, req->buf_len * req->num_bufs, req->buf_base_addr, req->dma);
	req->buf_base_addr = NULL;
}

static u32 gsi_trb_high(struct usb_gsi_request *req)
{
	return (upper_32_bits(req->ring_dma) & 0xffff) |
		BIT(23) | BIT(21) | (req->ep_intr_num << 16);
}

static int gsi_prepare_ring(struct dwc3_ep *dep, struct usb_gsi_request *req)
{
	struct device *dev = dep->dwc->sysdev;
	struct dwc3_trb *trb;
	dma_addr_t addr;
	size_t length;
	unsigned int count, i;
	int ret;

	if (!req->num_bufs || req->num_bufs > U16_MAX / (2 * sizeof(*trb)) - 1 ||
	    !req->buf_len || !IS_ALIGNED(req->buf_len, SZ_1K) ||
	    req->buf_len / SZ_1K > U8_MAX ||
	    check_mul_overflow(req->buf_len, req->num_bufs, &length))
		return -EINVAL;
	if (req->ring)
		return -EBUSY;
	count = (dep->direction ? 2 : 1) * req->num_bufs + 2;
	req->ring_size = count * sizeof(*trb);
	req->buf_base_addr = dma_alloc_coherent(dev, length, &req->dma, GFP_KERNEL);
	if (!req->buf_base_addr)
		return -ENOMEM;
	ret = dma_get_sgtable(dev, &req->sgt_data_buff, req->buf_base_addr, req->dma, length);
	if (ret)
		goto fail;
	req->ring = dma_alloc_coherent(dev, req->ring_size, &req->ring_dma, GFP_KERNEL);
	if (!req->ring) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = dma_get_sgtable(dev, &req->sgt_trb_xfer_ring, req->ring,
			      req->ring_dma, req->ring_size);
	if (ret)
		goto fail;
	addr = req->dma;
	for (i = 0; i < count - 1; i++) {
		trb = &req->ring[i];
		trb->ctrl = DWC3_TRBCTL_NORMAL | DWC3_TRB_CTRL_IOC;
		if (dep->direction && i <= req->num_bufs)
			continue;
		if (!dep->direction && !i) {
			trb->bpl = lower_32_bits(req->ring_dma + sizeof(*trb));
			trb->bph = upper_32_bits(req->ring_dma + sizeof(*trb));
			trb->ctrl = DWC3_TRBCTL_LINK_TRB;
			continue;
		}
		trb->bpl = lower_32_bits(addr);
		trb->bph = upper_32_bits(addr);
		if (!dep->direction) {
			trb->size = req->buf_len;
			trb->ctrl |= DWC3_TRB_CTRL_CSP | DWC3_TRB_CTRL_ISP_IMI;
		}
		addr += req->buf_len;
	}
	trb = &req->ring[count - 1];
	trb->bpl = lower_32_bits(req->ring_dma);
	trb->bph = gsi_trb_high(req);
	trb->ctrl = DWC3_TRBCTL_LINK_TRB | DWC3_TRB_CTRL_HWO;
	return 0;
fail:
	gsi_free_ring(dep->dwc, req);
	return ret;
}

static int gsi_set_doorbell(struct dwc3 *dwc, struct usb_gsi_request *req)
{
	void __iomem *lo, *hi;
	dma_addr_t dma;

	if (req->doorbell_lo)
		return 0;
	if (!req->db_reg_phs_addr_lsb || !req->db_reg_phs_addr_msb ||
	    !IS_ALIGNED(req->db_reg_phs_addr_lsb | req->db_reg_phs_addr_msb, sizeof(u32)))
		return -EINVAL;
	lo = ioremap(req->db_reg_phs_addr_lsb, sizeof(u32));
	if (!lo)
		return -ENOMEM;
	hi = ioremap(req->db_reg_phs_addr_msb, sizeof(u32));
	if (!hi) {
		iounmap(lo);
		return -ENOMEM;
	}
	dma = dma_map_resource(dwc->sysdev, req->db_reg_phs_addr_lsb & PAGE_MASK,
			       PAGE_SIZE, DMA_BIDIRECTIONAL, 0);
	if (dma_mapping_error(dwc->sysdev, dma)) {
		iounmap(hi);
		iounmap(lo);
		return -EIO;
	}
	req->mapped_db_reg_phs_addr_lsb = dma + offset_in_page(req->db_reg_phs_addr_lsb);
	req->doorbell_lo = lo;
	req->doorbell_hi = hi;
	gsi_write_addr(dwc->gsi, GSI_RING_LO, req->ep_intr_num, req->ring_dma);
	gsi_write_addr(dwc->gsi, GSI_DB_LO, req->ep_intr_num, req->mapped_db_reg_phs_addr_lsb);
	return 0;
}

static void gsi_fill_channel(struct dwc3_ep *dep, struct gsi_channel_info *info)
{
	struct usb_gsi_request *req = info->ch_req;
	phys_addr_t base = dep->dwc->xhci_resources[0].start;
	phys_addr_t cmd = base + DWC3_DEPCMD(dep->number);
	phys_addr_t count = base + DWC3_GEVNTCOUNT(req->ep_intr_num);

	info->depcmd_low_addr = lower_32_bits(cmd);
	info->depcmd_hi_addr = upper_32_bits(cmd);
	info->gevntcount_low_addr = lower_32_bits(count);
	info->gevntcount_hi_addr = upper_32_bits(count);
	info->const_buffer_size = req->buf_len / SZ_1K;
	info->xfer_ring_base_addr = req->ring_dma;
	info->xfer_ring_len = req->ring_size;
	info->last_trb_addr = req->ring_dma + req->ring_size - sizeof(*req->ring);
}

int msm_ep_set_mode(struct usb_ep *ep, enum usb_hw_ep_mode mode)
{
	if (!ep || ep->ops != &dwc3_gadget_ep_ops || !to_dwc3_ep(ep)->dwc->gsi)
		return -EOPNOTSUPP;
	if (mode != USB_EP_NONE && mode != USB_EP_GSI)
		return -EOPNOTSUPP;
	if (mode == USB_EP_NONE)
		to_dwc3_ep(ep)->gsi_intr = 0;
	return 0;
}
EXPORT_SYMBOL_GPL(msm_ep_set_mode);

int usb_gsi_ep_op(struct usb_ep *ep, void *data, enum gsi_ep_op op)
{
	struct dwc3_gadget_ep_cmd_params params = {};
	struct usb_gsi_request *req = data;
	struct dwc3_ep *dep;
	struct dwc3 *dwc;
	struct dwc3_gsi *gsi;
	void __iomem *cfg;
	unsigned long flags;
	dma_addr_t last;
	u32 val, cmd;
	int i, ret = 0;

	if (!ep || ep->ops != &dwc3_gadget_ep_ops)
		return -EOPNOTSUPP;
	dep = to_dwc3_ep(ep);
	dwc = dep->dwc;
	gsi = dwc->gsi;
	if (!gsi)
		return -EOPNOTSUPP;
	if (op != GSI_EP_OP_GET_XFER_IDX && op != GSI_EP_OP_ENDXFER && !data)
		return -EINVAL;
	cfg = gsi->scratch + gsi->offset[GSI_CFG];
	switch (op) {
	case GSI_EP_OP_CONFIG:
		if (!req->ep_intr_num || req->ep_intr_num > gsi->count)
			return -EINVAL;
		if (!dwc->pullups_connected)
			return -ESHUTDOWN;
		dep->gsi_intr = req->ep_intr_num;
		gsi_write_addr(gsi, GSI_DB_LO, dep->gsi_intr, gsi->dummy_dma);
		ret = usb_ep_enable(ep);
		if (ret)
			dep->gsi_intr = 0;
		return ret;
	case GSI_EP_OP_DISABLE:
		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_stop_active_transfer(dep, true, false);
		spin_unlock_irqrestore(&dwc->lock, flags);
		ret = usb_ep_disable(ep);
		dep->gsi_intr = 0;
		return ret;
	case GSI_EP_OP_PREPARE_TRBS:
		return dwc->pullups_connected ? gsi_prepare_ring(dep, req) : -ESHUTDOWN;
	case GSI_EP_OP_FREE_TRBS:
		gsi_free_ring(dwc, req);
		return 0;
	case GSI_EP_OP_STORE_DBL_INFO:
		return gsi_set_doorbell(dwc, req);
	case GSI_EP_OP_GET_CH_INFO:
		gsi_fill_channel(dep, data);
		return 0;
	case GSI_EP_OP_CHECK_FOR_SUSPEND:
		return !readl_poll_timeout_atomic(gsi->scratch + gsi->offset[GSI_STATUS],
						  val, !(val & BIT(15)), 1, 1500);
	case GSI_EP_OP_RING_DB:
		if (!req->doorbell_lo || !dwc->pullups_connected)
			return -ESHUTDOWN;
		last = req->ring_dma + req->ring_size - sizeof(*req->ring);
		writel(lower_32_bits(last), req->doorbell_lo);
		writel(upper_32_bits(last), req->doorbell_hi);
		return 0;
	default:
		break;
	}

	spin_lock_irqsave(&dwc->lock, flags);
	switch (op) {
	case GSI_EP_OP_GET_XFER_IDX:
		ret = dep->resource_index;
		break;
	case GSI_EP_OP_STARTXFER:
		if (!dwc->pullups_connected || !req->ring) {
			ret = -ESHUTDOWN;
			break;
		}
		params.param0 = gsi_trb_high(req);
		params.param1 = lower_32_bits(req->ring_dma);
		dma_wmb();
		ret = dwc3_send_gadget_ep_cmd(dep, DWC3_DEPCMD_STARTTRANSFER, &params);
		break;
	case GSI_EP_OP_UPDATEXFER:
		if (!req->ring) {
			ret = -EINVAL;
			break;
		}
		for (i = 0; i < req->num_bufs; i++)
			req->ring[i].ctrl |= DWC3_TRB_CTRL_HWO;
		dma_wmb();
		cmd = DWC3_DEPCMD_UPDATETRANSFER | DWC3_DEPCMD_PARAM(dep->resource_index);
		ret = dwc3_send_gadget_ep_cmd(dep, cmd, &params);
		break;
	case GSI_EP_OP_ENDXFER:
		dwc3_stop_active_transfer(dep, true, false);
		break;
	case GSI_EP_OP_ENABLE_GSI:
		/* DPL can join a running RMNET or RNDIS composition. Restarting
		 * the shared doorbell pointer here would disturb the live ring.
		 */
		val = readl(cfg);
		if (val & BIT(0))
			break;
		val |= BIT(12);
		writel(val, cfg);
		writel(val | BIT(20), cfg);
		writel(val, cfg);
		writel(val | BIT(0), cfg);
		break;
	case GSI_EP_OP_SET_CLR_BLOCK_DBL:
		val = readl(cfg);
		writel(*(bool *)data ? val | BIT(1) : val & ~BIT(1), cfg);
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&dwc->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(usb_gsi_ep_op);
