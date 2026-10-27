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

bool mhi_scan_rddm_cookie(struct mhi_controller *ctrl, u32 cookie)
{
	u32 val;
	int ret = -EIO;

	if (!ctrl->rddm_image || !ctrl->bhi || !cookie)
		return false;
	read_lock_bh(&ctrl->pm_lock);
	if (MHI_REG_ACCESS_VALID(ctrl->pm_state))
		ret = mhi_read_reg(ctrl, ctrl->bhi, BHI_ERRDBG2, &val);
	read_unlock_bh(&ctrl->pm_lock);
	return !ret && val == cookie;
}
EXPORT_SYMBOL_GPL(mhi_scan_rddm_cookie);

void mhi_debug_reg_dump(struct mhi_controller *ctrl)
{
	struct device *dev = &ctrl->mhi_dev->dev;
	const struct {
		const char *name;
		void __iomem *base;
		u32 offset;
	} regs[] = {
		{ "BHI_ERRDBG1", ctrl->bhi, BHI_ERRDBG1 },
		{ "BHI_ERRDBG2", ctrl->bhi, BHI_ERRDBG2 },
		{ "BHI_ERRDBG3", ctrl->bhi, BHI_ERRDBG3 },
		{ "BHI_ERRCODE", ctrl->bhi, BHI_ERRCODE },
		{ "BHI_EXECENV", ctrl->bhi, BHI_EXECENV },
		{ "BHI_STATUS", ctrl->bhi, BHI_STATUS },
		{ "MHICTRL", ctrl->regs, MHICTRL },
		{ "MHISTATUS", ctrl->regs, MHISTATUS },
		{ "WAKE_DB", ctrl->wake_db, 0 },
		{ "BHIE_TXVECDB", ctrl->bhie, BHIE_TXVECDB_OFFS },
		{ "BHIE_TXVECSTATUS", ctrl->bhie, BHIE_TXVECSTATUS_OFFS },
		{ "BHIE_RXVECDB", ctrl->bhie, BHIE_RXVECDB_OFFS },
		{ "BHIE_RXVECSTATUS", ctrl->bhie, BHIE_RXVECSTATUS_OFFS },
	};
	u32 val;
	int i;

	dev_err(dev, "PM %s, MHI %s, EE %s\n", to_mhi_pm_state_str(ctrl->pm_state),
		mhi_state_str(ctrl->dev_state), TO_MHI_EXEC_STR(ctrl->ee));
	read_lock_bh(&ctrl->pm_lock);
	if (MHI_REG_ACCESS_VALID(ctrl->pm_state))
		for (i = 0; i < ARRAY_SIZE(regs); i++)
			if (regs[i].base && !mhi_read_reg(ctrl, regs[i].base, regs[i].offset, &val))
				dev_err(dev, "%s %#x\n", regs[i].name, val);
	read_unlock_bh(&ctrl->pm_lock);
}
EXPORT_SYMBOL_GPL(mhi_debug_reg_dump);

int mhi_force_reset(struct mhi_controller *ctrl)
{
	if (!ctrl->rddm_image || !ctrl->bhie)
		return -ENODEV;
	mhi_report_error(ctrl);
	mhi_soc_reset(ctrl);
	return mhi_poll_reg_field(ctrl, ctrl->bhie, BHIE_RXVECSTATUS_OFFS,
				  BHIE_RXVECSTATUS_STATUS_BMSK, BHIE_RXVECSTATUS_STATUS_XFER_COMPL,
				  5000, ctrl->timeout_ms);
}
EXPORT_SYMBOL_GPL(mhi_force_reset);

struct rddm_file {
	__le64 base;
	__le64 phys;
	__le64 size;
	char description[20];
	char name[20];
};

struct rddm_table {
	__le32 version;
	__le32 size;
	struct rddm_file files[8];
};

void mhi_dump_sfr(struct mhi_controller *ctrl)
{
	const struct image_info *image = ctrl->rddm_image;
	const struct rddm_table *table;
	unsigned int n, i, count;
	size_t size, copied;
	u64 offset, file_size;
	char *reason;

	if (!image || image->entries < 2 || !image->mhi_buf[0].buf ||
	    image->mhi_buf[0].len < offsetof(struct rddm_table, files))
		return;
	table = image->mhi_buf[0].buf;
	offset = le32_to_cpu(table->size);
	if (offset < offsetof(struct rddm_table, files) ||
	    offset > min(sizeof(*table), image->mhi_buf[0].len))
		return;
	count = (offset - offsetof(struct rddm_table, files)) / sizeof(*table->files);
	for (n = 0; n < count; n++) {
		file_size = le64_to_cpu(table->files[n].size);
		if (!strncmp(table->files[n].name, "Q6-SFR.bin", sizeof(table->files[n].name)))
			break;
		if (check_add_overflow(offset, file_size, &offset))
			return;
	}
	if (n == count || !file_size)
		return;

	size = min_t(u64, file_size, SZ_4K);
	reason = kmalloc(size + 1, GFP_ATOMIC);
	if (!reason)
		return;
	copied = 0;
	/* Last allocation holds the BHIe vector table, outside the dump file. */
	for (i = 0; i < image->entries - 1 && copied < size; i++) {
		const struct mhi_buf *seg = &image->mhi_buf[i];
		size_t take;

		if (offset >= seg->len) {
			offset -= seg->len;
			continue;
		}
		take = min_t(size_t, size - copied, seg->len - offset);
		memcpy(reason + copied, seg->buf + offset, take);
		copied += take;
		offset = 0;
	}
	if (copied == size) {
		reason[copied] = '\0';
		dev_err(&ctrl->mhi_dev->dev, "Subsystem failure reason: %s\n", reason);
	}
	kfree(reason);
}
EXPORT_SYMBOL_GPL(mhi_dump_sfr);

#define MHI_MISC_CAPABILITIES	0x24
#define MHI_CAP_BW_SCALE	3
#define MHI_BW_DB		126
#define MHI_BW_NACK		0xf

void mhi_controller_set_bw_scale_cb(struct mhi_controller *ctrl,
				    int (*cb)(struct mhi_controller *, struct mhi_link_info *))
{
	ctrl->bw_scale = cb;
}
EXPORT_SYMBOL_GPL(mhi_controller_set_bw_scale_cb);

void mhi_bw_setup(struct mhi_controller *ctrl, u32 channel_db)
{
	u32 cap, offset, ring, limit = min_t(size_t, SZ_4K, ctrl->reg_len);
	DECLARE_BITMAP(visited, SZ_4K / sizeof(u32)) = {};

	ctrl->bw_db = NULL;
	ctrl->bw_response_pending = false;
	if (!ctrl->bw_scale)
		return;
	for (ring = 0; ring < ctrl->total_ev_rings; ring++)
		if (ctrl->mhi_event[ring].data_type == MHI_ER_BW_SCALE)
			break;
	if (ring == ctrl->total_ev_rings || ring > 31)
		return;
	if (mhi_read_reg(ctrl, ctrl->regs, MHI_MISC_CAPABILITIES, &offset))
		return;
	while (offset && IS_ALIGNED(offset, sizeof(u32)) && limit >= 8 && offset <= limit - 8) {
		if (__test_and_set_bit(offset / sizeof(u32), visited) ||
		    mhi_read_reg(ctrl, ctrl->regs, offset, &cap))
			return;
		if (FIELD_GET(GENMASK(31, 24), cap) == MHI_CAP_BW_SCALE) {
			ctrl->bw_db = ctrl->regs + channel_db + 8 * MHI_BW_DB;
			mhi_write_reg(ctrl, ctrl->regs, offset + 4,
				      FIELD_PREP(GENMASK(31, 25), MHI_BW_DB) | BIT(24) |
				      FIELD_PREP(GENMASK(23, 19), ring));
			return;
		}
		offset = FIELD_GET(GENMASK(23, 12), cap);
	}
}

void mhi_bw_work(struct work_struct *work)
{
	struct mhi_event *event = container_of(work, struct mhi_event, bw_work);
	struct mhi_controller *ctrl = event->mhi_cntrl;
	struct mhi_link_info request;
	u32 response;
	int ret;

	spin_lock_bh(&event->lock);
	request = event->bw_request;
	response = event->bw_seq;
	spin_unlock_bh(&event->lock);
	if (!ctrl->bw_db || !MHI_IN_MISSION_MODE(READ_ONCE(ctrl->ee)))
		return;
	ret = mhi_device_get_sync(ctrl->mhi_dev);
	if (ret)
		return;
	ret = ctrl->bw_scale(ctrl, &request);
	write_lock_irq(&ctrl->pm_lock);
	if (ret)
		response |= FIELD_PREP(GENMASK(11, 8), MHI_BW_NACK);
	else
		ctrl->mhi_link_info = request;
	ctrl->bw_response = response;
	ctrl->bw_response_pending = true;
	mhi_bw_send_response(ctrl);
	write_unlock_irq(&ctrl->pm_lock);
	mhi_device_put(ctrl->mhi_dev);
}

/* Called with the controller PM write lock held. */
void mhi_bw_send_response(struct mhi_controller *ctrl)
{
	if (ctrl->bw_response_pending && MHI_DB_ACCESS_VALID(ctrl)) {
		mhi_write_reg(ctrl, ctrl->bw_db, 0, ctrl->bw_response);
		ctrl->bw_response_pending = false;
	}
}
