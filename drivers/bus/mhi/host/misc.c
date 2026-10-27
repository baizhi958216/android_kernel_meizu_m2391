// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm MHI extensions for independently managed PCIe links.
 * Copyright (c) 2019-2021, The Linux Foundation.
 */
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/mhi_misc.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "internal.h"

static void mhi_notify_lpm(struct mhi_controller *ctrl, enum mhi_callback reason)
{
	struct mhi_chan *chan;

	list_for_each_entry(chan, &ctrl->lpm_chans, node) {
		mutex_lock(&chan->mutex);
		if (chan->mhi_dev)
			mhi_notify(chan->mhi_dev, reason);
		mutex_unlock(&chan->mutex);
	}
}

void mhi_set_m2_timeout_ms(struct mhi_controller *ctrl, u32 timeout)
{
	WRITE_ONCE(ctrl->m2_timeout_ms, timeout);
}
EXPORT_SYMBOL_GPL(mhi_set_m2_timeout_ms);

int mhi_pm_fast_suspend(struct mhi_controller *ctrl, bool notify_clients)
{
	struct mhi_event *control = ctrl->mhi_event;
	int ret = 0;

	/* The PCIe low-power controller takes ownership without sending M3 to firmware. */
	wait_event_timeout(ctrl->state_event,
			   READ_ONCE(ctrl->dev_state) == MHI_STATE_M2 ||
			   MHI_PM_IN_ERROR_STATE(READ_ONCE(ctrl->pm_state)),
			   msecs_to_jiffies(READ_ONCE(ctrl->m2_timeout_ms)));

	tasklet_disable(&control->task);
	write_lock_irq(&ctrl->pm_lock);
	if (MHI_PM_IN_ERROR_STATE(ctrl->pm_state)) {
		ret = -EIO;
		goto out;
	}
	if (ctrl->pm_state != MHI_PM_M0 && ctrl->pm_state != MHI_PM_M2) {
		ret = -EINVAL;
		goto out;
	}
	if (atomic_read(&ctrl->pending_pkts) || atomic_read(&ctrl->dev_wake)) {
		ret = -EBUSY;
		goto out;
	}

	ctrl->saved_pm_state = ctrl->pm_state;
	ctrl->saved_dev_state = ctrl->dev_state;
	ctrl->pm_state = MHI_PM_M3;
	ctrl->dev_state = MHI_STATE_M3_FAST;
out:
	write_unlock_irq(&ctrl->pm_lock);
	tasklet_enable(&control->task);
	if (!ret && notify_clients)
		mhi_notify_lpm(ctrl, MHI_CB_LPM_ENTER);
	return ret;
}
EXPORT_SYMBOL_GPL(mhi_pm_fast_suspend);

int mhi_pm_fast_resume(struct mhi_controller *ctrl, bool notify_clients)
{
	struct mhi_event *control = ctrl->mhi_event;
	u32 saved;
	int ret = 0;

	tasklet_disable(&control->task);
	write_lock_irq(&ctrl->pm_lock);
	if (ctrl->pm_state != MHI_PM_M3 || ctrl->dev_state != MHI_STATE_M3_FAST) {
		ret = MHI_PM_IN_ERROR_STATE(ctrl->pm_state) ? -EIO : -EINVAL;
		write_unlock_irq(&ctrl->pm_lock);
		goto out;
	}
	saved = ctrl->saved_pm_state;
	ctrl->pm_state = saved;
	ctrl->dev_state = ctrl->saved_dev_state;
	write_unlock_irq(&ctrl->pm_lock);

	if (ctrl->rddm_image && mhi_get_exec_env(ctrl) == MHI_EE_RDDM) {
		mhi_report_error(ctrl);
		WRITE_ONCE(ctrl->ee, MHI_EE_RDDM);
		ctrl->status_cb(ctrl, MHI_CB_EE_RDDM);
		wake_up_all(&ctrl->state_event);
		goto out;
	}

	if (saved == MHI_PM_M0) {
		ret = mhi_pm_m0_transition(ctrl);
	} else {
		read_lock_bh(&ctrl->pm_lock);
		ctrl->wake_get(ctrl, true);
		ctrl->wake_put(ctrl, true);
		read_unlock_bh(&ctrl->pm_lock);
	}
out:
	tasklet_enable(&control->task);
	if (!ret && !MHI_PM_IN_ERROR_STATE(READ_ONCE(ctrl->pm_state))) {
		mhi_irq_handler(0, control);
		if (notify_clients)
			mhi_notify_lpm(ctrl, MHI_CB_LPM_EXIT);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(mhi_pm_fast_resume);

static bool mhi_atomic_wake_complete(struct mhi_controller *ctrl, bool in_panic)
{
	if (MHI_PM_IN_ERROR_STATE(READ_ONCE(ctrl->pm_state)))
		return true;
	return in_panic ? mhi_get_mhi_state(ctrl) == MHI_STATE_M0 :
			  READ_ONCE(ctrl->pm_state) == MHI_PM_M0;
}

int mhi_device_get_sync_atomic(struct mhi_device *dev, int timeout_us, bool in_panic)
{
	struct mhi_controller *ctrl = dev->mhi_cntrl;
	bool ready;
	int ret = 0;

	if (timeout_us < 0 || MHI_PM_IN_ERROR_STATE(READ_ONCE(ctrl->pm_state)))
		return -EIO;
	mhi_device_get(dev);
	if (timeout_us)
		ret = read_poll_timeout_atomic(mhi_atomic_wake_complete, ready, ready, 5,
					       timeout_us, false, ctrl, in_panic);
	if (MHI_PM_IN_ERROR_STATE(READ_ONCE(ctrl->pm_state)))
		ret = -EIO;
	if (ret)
		mhi_device_put(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(mhi_device_get_sync_atomic);

static int mhi_notify_fatal(struct device *dev, void *unused)
{
	if (dev->bus == &mhi_bus_type)
		mhi_notify(to_mhi_device(dev), MHI_CB_FATAL_ERROR);
	return 0;
}

int mhi_report_error(struct mhi_controller *ctrl)
{
	unsigned long flags;
	u32 state;

	write_lock_irqsave(&ctrl->pm_lock, flags);
	state = mhi_tryset_pm_state(ctrl, MHI_PM_SYS_ERR_DETECT);
	if (state == MHI_PM_SYS_ERR_DETECT)
		ctrl->dev_state = MHI_STATE_SYS_ERR;
	write_unlock_irqrestore(&ctrl->pm_lock, flags);
	if (state != MHI_PM_SYS_ERR_DETECT)
		return -EPERM;
	wake_up_all(&ctrl->state_event);
	return device_for_each_child(&ctrl->mhi_dev->dev, NULL, mhi_notify_fatal);
}
EXPORT_SYMBOL_GPL(mhi_report_error);

void mhi_controller_set_base(struct mhi_controller *ctrl, phys_addr_t base)
{
	WRITE_ONCE(ctrl->phys_base, base);
}
EXPORT_SYMBOL_GPL(mhi_controller_set_base);

int mhi_controller_get_base(struct mhi_controller *ctrl, phys_addr_t *base)
{
	*base = READ_ONCE(ctrl->phys_base);
	return *base ? 0 : -ENODEV;
}
EXPORT_SYMBOL_GPL(mhi_controller_get_base);

u32 mhi_controller_get_numeric_id(struct mhi_controller *ctrl)
{
	struct pci_dev *pdev;

	if (!dev_is_pci(ctrl->cntrl_dev))
		return 0;
	pdev = to_pci_dev(ctrl->cntrl_dev);
	return (u32)pdev->device << 16 | (pci_domain_nr(pdev->bus) & 0xf) << 12 |
	       pdev->bus->number << 4 | (PCI_SLOT(pdev->devfn) & 0xf);
}
EXPORT_SYMBOL_GPL(mhi_controller_get_numeric_id);

static int mhi_get_db_base(struct mhi_device *dev, u32 reg, phys_addr_t *addr)
{
	struct mhi_controller *ctrl = dev->mhi_cntrl;
	phys_addr_t base;
	u32 offset;
	int ret;

	ret = mhi_controller_get_base(ctrl, &base);
	if (ret)
		return ret;
	read_lock_bh(&ctrl->pm_lock);
	if (!MHI_REG_ACCESS_VALID(ctrl->pm_state))
		ret = -EIO;
	else
		ret = mhi_read_reg(ctrl, ctrl->regs, reg, &offset);
	read_unlock_bh(&ctrl->pm_lock);
	if (ret)
		return ret;
	if (offset >= ctrl->reg_len || check_add_overflow(base, offset, addr))
		return -ERANGE;
	return 0;
}

int mhi_get_channel_db_base(struct mhi_device *dev, phys_addr_t *addr)
{
	return mhi_get_db_base(dev, CHDBOFF, addr);
}
EXPORT_SYMBOL_GPL(mhi_get_channel_db_base);

int mhi_get_event_ring_db_base(struct mhi_device *dev, phys_addr_t *addr)
{
	return mhi_get_db_base(dev, ERDBOFF, addr);
}
EXPORT_SYMBOL_GPL(mhi_get_event_ring_db_base);

int mhi_device_configure(struct mhi_device *dev, enum dma_data_direction dir,
			 struct mhi_buf *cfg, int count)
{
	struct mhi_controller *ctrl = dev->mhi_cntrl;
	struct mhi_chan *chan;
	struct mhi_chan_ctxt *ch = NULL;
	struct mhi_event_ctxt *er = NULL;
	int i;

	switch (dir) {
	case DMA_TO_DEVICE:
		chan = dev->ul_chan;
		break;
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
	case DMA_NONE:
		chan = dev->dl_chan;
		break;
	default:
		return -EINVAL;
	}
	if (!chan || !chan->offload_ch || !ctrl->mhi_ctxt || !cfg || count <= 0)
		return -EINVAL;
	for (i = 0; i < count; i++) {
		if (!cfg[i].buf || !cfg[i].name)
			return -EINVAL;
		if (!strcmp(cfg[i].name, "ECA") && cfg[i].len == sizeof(*er))
			er = cfg[i].buf;
		else if (!strcmp(cfg[i].name, "CCA") && cfg[i].len == sizeof(*ch))
			ch = cfg[i].buf;
		else
			return -EINVAL;
	}

	mutex_lock(&chan->mutex);
	if (er)
		memcpy(&ctrl->mhi_ctxt->er_ctxt[chan->er_index], er, sizeof(*er));
	if (ch)
		memcpy(&ctrl->mhi_ctxt->chan_ctxt[chan->chan], ch, sizeof(*ch));
	/* A later channel start publishes the two coherent contexts to the peer. */
	dma_wmb();
	mutex_unlock(&chan->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(mhi_device_configure);
