// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s " fmt, KBUILD_MODNAME

#include <linux/atomic.h>
#include <linux/cpu_pm.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <clocksource/arm_arch_timer.h>
#include <soc/qcom/cmd-db.h>
#include <soc/qcom/tcs.h>
#include <soc/qcom/rpmh.h>
#include <dt-bindings/soc/qcom,rpmh-rsc.h>

#include "rpmh-internal.h"

#define CREATE_TRACE_POINTS
#include "trace-rpmh.h"


/* RSC v3 hardware-channel interface, relative to each DRV. */
#define RSC_CH_COMPLETE			0x494
#define RSC_CH_UPDATE			0x498
#define RSC_CH_BUSY			0x49c
#define RSC_CH_ENABLE			0x4a0
#define RSC_CH_CLEAR_STATUS		BIT(31)
#define RSC_CH_TCS_STRIDE		336

#define RSC_DRV_ID			0

#define MAJOR_VER_MASK			0xFF
#define MAJOR_VER_SHIFT			16
#define MINOR_VER_MASK			0xFF
#define MINOR_VER_SHIFT			8

enum {
	RSC_DRV_TCS_OFFSET,
	RSC_DRV_CMD_OFFSET,
	DRV_SOLVER_CONFIG,
	DRV_PRNT_CHLD_CONFIG,
	RSC_DRV_IRQ_ENABLE,
	RSC_DRV_IRQ_STATUS,
	RSC_DRV_IRQ_CLEAR,
	RSC_DRV_CMD_WAIT_FOR_CMPL,
	RSC_DRV_CONTROL,
	RSC_DRV_STATUS,
	RSC_DRV_CMD_ENABLE,
	RSC_DRV_CMD_MSGID,
	RSC_DRV_CMD_ADDR,
	RSC_DRV_CMD_DATA,
	RSC_DRV_CMD_STATUS,
	RSC_DRV_CMD_RESP_DATA,
};

/* DRV HW Solver Configuration Information Register */
#define DRV_HW_SOLVER_MASK		1
#define DRV_HW_SOLVER_SHIFT		24

/* DRV TCS Configuration Information Register */
#define DRV_NUM_TCS_MASK		0x3F
#define DRV_NUM_TCS_SHIFT		6
#define DRV_NCPT_MASK			0x1F
#define DRV_NCPT_SHIFT			27

/* Offsets for CONTROL TCS Registers */
#define RSC_DRV_CTL_TCS_DATA_HI		0x38
#define RSC_DRV_CTL_TCS_DATA_HI_MASK	0xFFFFFF
#define RSC_DRV_CTL_TCS_DATA_HI_VALID	BIT(31)
#define RSC_DRV_CTL_TCS_DATA_LO		0x40
#define RSC_DRV_CTL_TCS_DATA_LO_MASK	0xFFFFFFFF
#define RSC_DRV_CTL_TCS_DATA_SIZE	32

#define TCS_AMC_MODE_ENABLE		BIT(16)
#define TCS_AMC_MODE_TRIGGER		BIT(24)

/* TCS CMD register bit mask */
#define CMD_MSGID_LEN			8
#define CMD_MSGID_RESP_REQ		BIT(8)
#define CMD_MSGID_WRITE			BIT(16)
#define CMD_STATUS_ISSUED		BIT(8)
#define CMD_STATUS_COMPL		BIT(16)

/*
 * Here's a high level overview of how all the registers in RPMH work
 * together:
 *
 * - The main rpmh-rsc address is the base of a register space that can
 *   be used to find overall configuration of the hardware
 *   (DRV_PRNT_CHLD_CONFIG). Also found within the rpmh-rsc register
 *   space are all the TCS blocks. The offset of the TCS blocks is
 *   specified in the device tree by "qcom,tcs-offset" and used to
 *   compute tcs_base.
 * - TCS blocks come one after another. Type, count, and order are
 *   specified by the device tree as "qcom,tcs-config".
 * - Each TCS block has some registers, then space for up to 16 commands.
 *   Note that though address space is reserved for 16 commands, fewer
 *   might be present. See ncpt (num cmds per TCS).
 *
 * Here's a picture:
 *
 *  +---------------------------------------------------+
 *  |RSC                                                |
 *  | ctrl                                              |
 *  |                                                   |
 *  | Drvs:                                             |
 *  | +-----------------------------------------------+ |
 *  | |DRV0                                           | |
 *  | | ctrl/config                                   | |
 *  | | IRQ                                           | |
 *  | |                                               | |
 *  | | TCSes:                                        | |
 *  | | +------------------------------------------+  | |
 *  | | |TCS0  |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | | ctrl | 0| 1| 2| 3| 4| 5| .| .| .| .|14|15|  | |
 *  | | |      |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | +------------------------------------------+  | |
 *  | | +------------------------------------------+  | |
 *  | | |TCS1  |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | | ctrl | 0| 1| 2| 3| 4| 5| .| .| .| .|14|15|  | |
 *  | | |      |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | +------------------------------------------+  | |
 *  | | +------------------------------------------+  | |
 *  | | |TCS2  |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | | ctrl | 0| 1| 2| 3| 4| 5| .| .| .| .|14|15|  | |
 *  | | |      |  |  |  |  |  |  |  |  |  |  |  |  |  | |
 *  | | +------------------------------------------+  | |
 *  | |                    ......                     | |
 *  | +-----------------------------------------------+ |
 *  | +-----------------------------------------------+ |
 *  | |DRV1                                           | |
 *  | | (same as DRV0)                                | |
 *  | +-----------------------------------------------+ |
 *  |                      ......                       |
 *  +---------------------------------------------------+
 */

#define USECS_TO_CYCLES(time_usecs)			\
	xloops_to_cycles((time_usecs) * 0x10C7UL)

static inline unsigned long xloops_to_cycles(u64 xloops)
{
	return (xloops * loops_per_jiffy * HZ) >> 32;
}

static u32 rpmh_rsc_reg_offset_ver_2_7[] = {
	[RSC_DRV_TCS_OFFSET]		= 672,
	[RSC_DRV_CMD_OFFSET]		= 20,
	[DRV_SOLVER_CONFIG]		= 0x04,
	[DRV_PRNT_CHLD_CONFIG]		= 0x0C,
	[RSC_DRV_IRQ_ENABLE]		= 0x00,
	[RSC_DRV_IRQ_STATUS]		= 0x04,
	[RSC_DRV_IRQ_CLEAR]		= 0x08,
	[RSC_DRV_CMD_WAIT_FOR_CMPL]	= 0x10,
	[RSC_DRV_CONTROL]		= 0x14,
	[RSC_DRV_STATUS]		= 0x18,
	[RSC_DRV_CMD_ENABLE]		= 0x1C,
	[RSC_DRV_CMD_MSGID]		= 0x30,
	[RSC_DRV_CMD_ADDR]		= 0x34,
	[RSC_DRV_CMD_DATA]		= 0x38,
	[RSC_DRV_CMD_STATUS]		= 0x3C,
	[RSC_DRV_CMD_RESP_DATA]		= 0x40,
};

static u32 rpmh_rsc_reg_offset_ver_3_0[] = {
	[RSC_DRV_TCS_OFFSET]		= 672,
	[RSC_DRV_CMD_OFFSET]		= 24,
	[DRV_SOLVER_CONFIG]		= 0x04,
	[DRV_PRNT_CHLD_CONFIG]		= 0x0C,
	[RSC_DRV_IRQ_ENABLE]		= 0x00,
	[RSC_DRV_IRQ_STATUS]		= 0x04,
	[RSC_DRV_IRQ_CLEAR]		= 0x08,
	[RSC_DRV_CMD_WAIT_FOR_CMPL]	= 0x20,
	[RSC_DRV_CONTROL]		= 0x24,
	[RSC_DRV_STATUS]		= 0x28,
	[RSC_DRV_CMD_ENABLE]		= 0x2C,
	[RSC_DRV_CMD_MSGID]		= 0x34,
	[RSC_DRV_CMD_ADDR]		= 0x38,
	[RSC_DRV_CMD_DATA]		= 0x3C,
	[RSC_DRV_CMD_STATUS]		= 0x40,
	[RSC_DRV_CMD_RESP_DATA]		= 0x44,
};

static inline void __iomem *
tcs_reg_addr(const struct rsc_drv *drv, int reg, int tcs_id)
{
	u32 stride = drv->second_tcs ? RSC_CH_TCS_STRIDE : drv->regs[RSC_DRV_TCS_OFFSET];

	return drv->tcs_base + stride * tcs_id + reg;
}

static inline void __iomem *
tcs_cmd_addr(const struct rsc_drv *drv, int reg, int tcs_id, int cmd_id)
{
	return tcs_reg_addr(drv, reg, tcs_id) + drv->regs[RSC_DRV_CMD_OFFSET] * cmd_id;
}

static u32 read_tcs_cmd(const struct rsc_drv *drv, int reg, int tcs_id,
			int cmd_id)
{
	return readl_relaxed(tcs_cmd_addr(drv, reg, tcs_id, cmd_id));
}

static u32 read_tcs_reg(const struct rsc_drv *drv, int reg, int tcs_id)
{
	return readl_relaxed(tcs_reg_addr(drv, reg, tcs_id));
}

static void write_tcs_cmd(const struct rsc_drv *drv, int reg, int tcs_id,
			  int cmd_id, u32 data)
{
	writel_relaxed(data, tcs_cmd_addr(drv, reg, tcs_id, cmd_id));
}

static void write_tcs_reg(const struct rsc_drv *drv, int reg, int tcs_id,
			  u32 data)
{
	writel_relaxed(data, tcs_reg_addr(drv, reg, tcs_id));
}

static void write_tcs_reg_sync(const struct rsc_drv *drv, int reg, int tcs_id,
			       u32 data)
{
	int i;

	writel(data, tcs_reg_addr(drv, reg, tcs_id));

	/*
	 * Wait until we read back the same value.  Use a counter rather than
	 * ktime for timeout since this may be called after timekeeping stops.
	 */
	for (i = 0; i < USEC_PER_SEC; i++) {
		if (readl(tcs_reg_addr(drv, reg, tcs_id)) == data)
			return;
		udelay(1);
	}
	pr_err("%s: error writing %#x to %d:%#x\n", drv->name,
	       data, tcs_id, reg);
}

/**
 * tcs_invalidate() - Invalidate all TCSes of the given type (sleep or wake).
 * @drv:  The RSC controller.
 * @type: SLEEP_TCS or WAKE_TCS
 *
 * This will clear the "slots" variable of the given tcs_group and also
 * tell the hardware to forget about all entries.
 *
 * The caller must ensure that no other RPMH actions are happening when this
 * function is called, since otherwise the device may immediately become
 * used again even before this function exits.
 */
static void tcs_invalidate(struct rsc_drv *drv, int type)
{
	int m;
	struct tcs_group *tcs = drv->program_second ? &drv->second_tcs[type] : &drv->tcs[type];

	/* Caller ensures nobody else is running so no lock */
	if (!drv->second_tcs && bitmap_empty(tcs->slots, MAX_TCS_SLOTS))
		return;

	for (m = tcs->offset; m < tcs->offset + tcs->num_tcs; m++) {
		write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CMD_ENABLE], m, 0);
		if (drv->second_tcs)
			write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CMD_WAIT_FOR_CMPL], m, 0);
	}

	bitmap_zero(tcs->slots, MAX_TCS_SLOTS);
}

/**
 * rpmh_rsc_invalidate() - Invalidate sleep and wake TCSes.
 * @drv: The RSC controller.
 *
 * The caller must ensure that no other RPMH actions are happening when this
 * function is called, since otherwise the device may immediately become
 * used again even before this function exits.
 */
void rpmh_rsc_invalidate(struct rsc_drv *drv)
{
	tcs_invalidate(drv, SLEEP_TCS);
	tcs_invalidate(drv, WAKE_TCS);
}

/**
 * get_tcs_for_msg() - Get the tcs_group used to send the given message.
 * @drv: The RSC controller.
 * @msg: The message we want to send.
 *
 * This is normally pretty straightforward except if we are trying to send
 * an ACTIVE_ONLY message but don't have any active_only TCSes.
 *
 * Return: A pointer to a tcs_group or an ERR_PTR.
 */
static struct tcs_group *get_tcs_for_msg(struct rsc_drv *drv,
					 const struct tcs_request *msg)
{
	int type;
	struct tcs_group *tcs;

	switch (msg->state) {
	case RPMH_ACTIVE_ONLY_STATE:
		type = ACTIVE_TCS;
		break;
	case RPMH_WAKE_ONLY_STATE:
		type = WAKE_TCS;
		break;
	case RPMH_SLEEP_STATE:
		type = SLEEP_TCS;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	/*
	 * If we are making an active request on a RSC that does not have a
	 * dedicated TCS for active state use, then re-purpose a wake TCS to
	 * send active votes. This is safe because we ensure any active-only
	 * transfers have finished before we use it (maybe by running from
	 * the last CPU in PM code).
	 */
	if (drv->second_tcs && msg->state == RPMH_ACTIVE_ONLY_STATE)
		return ERR_PTR(-EOPNOTSUPP);
	tcs = drv->program_second ? &drv->second_tcs[type] : &drv->tcs[type];
	if (msg->state == RPMH_ACTIVE_ONLY_STATE && !tcs->num_tcs)
		tcs = &drv->tcs[WAKE_TCS];

	return tcs;
}

/**
 * get_req_from_tcs() - Get a stashed request that was xfering on the given TCS.
 * @drv:    The RSC controller.
 * @tcs_id: The global ID of this TCS.
 *
 * For ACTIVE_ONLY transfers we want to call back into the client when the
 * transfer finishes. To do this we need the "request" that the client
 * originally provided us. This function grabs the request that we stashed
 * when we started the transfer.
 *
 * This only makes sense for ACTIVE_ONLY transfers since those are the only
 * ones we track sending (the only ones we enable interrupts for and the only
 * ones we call back to the client for).
 *
 * Return: The stashed request.
 */
static const struct tcs_request *get_req_from_tcs(struct rsc_drv *drv,
						  int tcs_id)
{
	struct tcs_group *tcs;
	int i;

	for (i = 0; i < TCS_TYPE_NR; i++) {
		tcs = &drv->tcs[i];
		if (tcs->mask & BIT(tcs_id))
			return tcs->req[tcs_id - tcs->offset];
	}

	return NULL;
}

/**
 * __tcs_set_trigger() - Start xfer on a TCS or unset trigger on a borrowed TCS
 * @drv:     The controller.
 * @tcs_id:  The global ID of this TCS.
 * @trigger: If true then untrigger/retrigger. If false then just untrigger.
 *
 * In the normal case we only ever call with "trigger=true" to start a
 * transfer. That will un-trigger/disable the TCS from the last transfer
 * then trigger/enable for this transfer.
 *
 * If we borrowed a wake TCS for an active-only transfer we'll also call
 * this function with "trigger=false" to just do the un-trigger/disable
 * before using the TCS for wake purposes again.
 *
 * Note that the AP is only in charge of triggering active-only transfers.
 * The AP never triggers sleep/wake values using this function.
 */
static void __tcs_set_trigger(struct rsc_drv *drv, int tcs_id, bool trigger)
{
	u32 enable;
	u32 reg = drv->regs[RSC_DRV_CONTROL];

	/*
	 * HW req: Clear the DRV_CONTROL and enable TCS again
	 * While clearing ensure that the AMC mode trigger is cleared
	 * and then the mode enable is cleared.
	 */
	enable = read_tcs_reg(drv, reg, tcs_id);
	enable &= ~TCS_AMC_MODE_TRIGGER;
	write_tcs_reg_sync(drv, reg, tcs_id, enable);
	enable &= ~TCS_AMC_MODE_ENABLE;
	write_tcs_reg_sync(drv, reg, tcs_id, enable);

	if (trigger) {
		/* Enable the AMC mode on the TCS and then trigger the TCS */
		enable = TCS_AMC_MODE_ENABLE;
		write_tcs_reg_sync(drv, reg, tcs_id, enable);
		enable |= TCS_AMC_MODE_TRIGGER;
		write_tcs_reg(drv, reg, tcs_id, enable);
	}
}

/**
 * enable_tcs_irq() - Enable or disable interrupts on the given TCS.
 * @drv:     The controller.
 * @tcs_id:  The global ID of this TCS.
 * @enable:  If true then enable; if false then disable
 *
 * We only ever call this when we borrow a wake TCS for an active-only
 * transfer. For active-only TCSes interrupts are always left enabled.
 */
static void enable_tcs_irq(struct rsc_drv *drv, int tcs_id, bool enable)
{
	u32 data;
	u32 reg = drv->regs[RSC_DRV_IRQ_ENABLE];

	data = readl_relaxed(drv->tcs_base + reg);
	if (enable)
		data |= BIT(tcs_id);
	else
		data &= ~BIT(tcs_id);
	writel_relaxed(data, drv->tcs_base + reg);
}

/**
 * tcs_tx_done() - TX Done interrupt handler.
 * @irq: The IRQ number (ignored).
 * @p:   Pointer to "struct rsc_drv".
 *
 * Called for ACTIVE_ONLY transfers (those are the only ones we enable the
 * IRQ for) when a transfer is done.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t tcs_tx_done(int irq, void *p)
{
	struct rsc_drv *drv = p;
	int i;
	unsigned long irq_status;
	const struct tcs_request *req;

	irq_status = readl_relaxed(drv->tcs_base + drv->regs[RSC_DRV_IRQ_STATUS]);

	for_each_set_bit(i, &irq_status, BITS_PER_TYPE(u32)) {
		req = get_req_from_tcs(drv, i);
		if (WARN_ON(!req))
			goto skip;

		trace_rpmh_tx_done(drv, i, req);

		/* Clear AMC trigger & enable modes and
		 * disable interrupt for this TCS
		 */
		__tcs_set_trigger(drv, i, false);
skip:
		/* Reclaim the TCS */
		write_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], i, 0);
		writel_relaxed(BIT(i), drv->tcs_base + drv->regs[RSC_DRV_IRQ_CLEAR]);
		spin_lock(&drv->lock);
		clear_bit(i, drv->tcs_in_use);
		/*
		 * Disable interrupt for WAKE TCS to avoid being
		 * spammed with interrupts coming when the solver
		 * sends its wake votes.
		 */
		if (!drv->tcs[ACTIVE_TCS].num_tcs)
			enable_tcs_irq(drv, i, false);
		spin_unlock(&drv->lock);
		wake_up(&drv->tcs_wait);
		if (req) {
			rpmh_tx_done(req);
			if (drv->iface)
				pm_runtime_put(drv->dev);
		}
	}

	return IRQ_HANDLED;
}

/**
 * __tcs_buffer_write() - Write to TCS hardware from a request; don't trigger.
 * @drv:    The controller.
 * @tcs_id: The global ID of this TCS.
 * @cmd_id: The index within the TCS to start writing.
 * @msg:    The message we want to send, which will contain several addr/data
 *          pairs to program (but few enough that they all fit in one TCS).
 *
 * This is used for all types of transfers (active, sleep, and wake).
 */
static void __tcs_buffer_write(struct rsc_drv *drv, int tcs_id, int cmd_id,
			       const struct tcs_request *msg)
{
	u32 msgid;
	u32 cmd_msgid = CMD_MSGID_LEN | CMD_MSGID_WRITE;
	u32 cmd_enable = 0, complete = 0;
	struct tcs_cmd *cmd;
	int i, j;

	/* Convert all commands to RR when the request has wait_for_compl set */
	cmd_msgid |= msg->wait_for_compl ? CMD_MSGID_RESP_REQ : 0;

	for (i = 0, j = cmd_id; i < msg->num_cmds; i++, j++) {
		cmd = &msg->cmds[i];
		cmd_enable |= BIT(j);
		if (cmd->wait)
			complete |= BIT(j);
		msgid = cmd_msgid;
		/*
		 * Additionally, if the cmd->wait is set, make the command
		 * response reqd even if the overall request was fire-n-forget.
		 */
		msgid |= cmd->wait ? CMD_MSGID_RESP_REQ : 0;

		write_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_MSGID], tcs_id, j, msgid);
		write_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_ADDR], tcs_id, j, cmd->addr);
		write_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_DATA], tcs_id, j, cmd->data);
		trace_rpmh_send_msg(drv, tcs_id, msg->state, j, msgid, cmd);
	}

	if (drv->second_tcs) {
		complete |= read_tcs_reg(drv, drv->regs[RSC_DRV_CMD_WAIT_FOR_CMPL], tcs_id);
		write_tcs_reg(drv, drv->regs[RSC_DRV_CMD_WAIT_FOR_CMPL], tcs_id, complete);
	}
	cmd_enable |= read_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], tcs_id);
	write_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], tcs_id, cmd_enable);
}

/* Kalama channel 0 reserves the TCS immediately following the normal pool
 * for low-latency DDR/LLCC votes. This slot never generates an AMC IRQ.
 */
int rpmh_init_fast_path(const struct device *dev, struct tcs_cmd *cmds, u32 count)
{
	struct rsc_drv *drv = dev_get_drvdata(dev->parent);
	struct tcs_request req = { .state = RPMH_ACTIVE_ONLY_STATE, .cmds = cmds, .num_cmds = count };
	unsigned long flags;
	int id;

	if (!drv || !drv->fast_cmd_slots)
		return -ENODEV;
	if (!cmds || !count || count > min(drv->fast_cmd_slots, 32U))
		return -EINVAL;
	id = drv->num_tcs;
	spin_lock_irqsave(&drv->lock, flags);
	if (drv->fast_cmd_count && !read_tcs_reg(drv, drv->regs[RSC_DRV_STATUS], id)) {
		spin_unlock_irqrestore(&drv->lock, flags);
		return -EBUSY;
	}
	enable_tcs_irq(drv, id, false);
	write_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], id, 0);
	__tcs_buffer_write(drv, id, 0, &req);
	drv->fast_cmd_count = count;
	spin_unlock_irqrestore(&drv->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(rpmh_init_fast_path);

int rpmh_update_fast_path(const struct device *dev, struct tcs_cmd *cmds, u32 count, u32 mask)
{
	struct rsc_drv *drv = dev_get_drvdata(dev->parent);
	unsigned long flags;
	int i, id, ret = 0;

	if (!drv || !drv->fast_cmd_slots)
		return -ENODEV;
	if (!cmds || !count || count > 32 || !mask || (mask & ~GENMASK(count - 1, 0)))
		return -EINVAL;
	id = drv->num_tcs;
	spin_lock_irqsave(&drv->lock, flags);
	if (count != drv->fast_cmd_count) {
		ret = -EINVAL;
		goto unlock;
	}
	for (i = 0; i < 5; i++) {
		if (read_tcs_reg(drv, drv->regs[RSC_DRV_STATUS], id))
			break;
		udelay(1);
	}
	if (i == 5) {
		ret = -EBUSY;
		goto unlock;
	}
	for (i = 0; i < count; i++) {
		if (cmds[i].addr != read_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_ADDR], id, i)) {
			ret = -EINVAL;
			goto unlock;
		}
	}
	for (i = 0; i < count; i++)
		if (mask & BIT(i))
			write_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_DATA], id, i, cmds[i].data);
	__tcs_set_trigger(drv, id, true);
unlock:
	spin_unlock_irqrestore(&drv->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(rpmh_update_fast_path);

/**
 * check_for_req_inflight() - Look to see if conflicting cmds are in flight.
 * @drv: The controller.
 * @tcs: A pointer to the tcs_group used for ACTIVE_ONLY transfers.
 * @msg: The message we want to send, which will contain several addr/data
 *       pairs to program (but few enough that they all fit in one TCS).
 *
 * This will walk through the TCSes in the group and check if any of them
 * appear to be sending to addresses referenced in the message. If it finds
 * one it'll return -EBUSY.
 *
 * Only for use for active-only transfers.
 *
 * Must be called with the drv->lock held since that protects tcs_in_use.
 *
 * Return: 0 if nothing in flight or -EBUSY if we should try again later.
 *         The caller must re-enable interrupts between tries since that's
 *         the only way tcs_in_use will ever be updated and the only way
 *         RSC_DRV_CMD_ENABLE will ever be cleared.
 */
static int check_for_req_inflight(struct rsc_drv *drv, struct tcs_group *tcs,
				  const struct tcs_request *msg)
{
	unsigned long curr_enabled;
	u32 addr;
	int j, k;
	int i = tcs->offset;

	for_each_set_bit_from(i, drv->tcs_in_use, tcs->offset + tcs->num_tcs) {
		curr_enabled = read_tcs_reg(drv, drv->regs[RSC_DRV_CMD_ENABLE], i);

		for_each_set_bit(j, &curr_enabled, MAX_CMDS_PER_TCS) {
			addr = read_tcs_cmd(drv, drv->regs[RSC_DRV_CMD_ADDR], i, j);
			for (k = 0; k < msg->num_cmds; k++) {
				if (cmd_db_match_resource_addr(msg->cmds[k].addr, addr))
					return -EBUSY;
			}
		}
	}

	return 0;
}

/**
 * find_free_tcs() - Find free tcs in the given tcs_group; only for active.
 * @tcs: A pointer to the active-only tcs_group (or the wake tcs_group if
 *       we borrowed it because there are zero active-only ones).
 *
 * Must be called with the drv->lock held since that protects tcs_in_use.
 *
 * Return: The first tcs that's free or -EBUSY if all in use.
 */
static int find_free_tcs(struct tcs_group *tcs)
{
	const struct rsc_drv *drv = tcs->drv;
	unsigned long i;
	unsigned long max = tcs->offset + tcs->num_tcs;

	i = find_next_zero_bit(drv->tcs_in_use, max, tcs->offset);
	if (i >= max)
		return -EBUSY;

	return i;
}

/**
 * claim_tcs_for_req() - Claim a tcs in the given tcs_group; only for active.
 * @drv: The controller.
 * @tcs: The tcs_group used for ACTIVE_ONLY transfers.
 * @msg: The data to be sent.
 *
 * Claims a tcs in the given tcs_group while making sure that no existing cmd
 * is in flight that would conflict with the one in @msg.
 *
 * Context: Must be called with the drv->lock held since that protects
 * tcs_in_use.
 *
 * Return: The id of the claimed tcs or -EBUSY if a matching msg is in flight
 * or the tcs_group is full.
 */
static int claim_tcs_for_req(struct rsc_drv *drv, struct tcs_group *tcs,
			     const struct tcs_request *msg)
{
	int ret;

	/*
	 * The h/w does not like if we send a request to the same address,
	 * when one is already in-flight or being processed.
	 */
	ret = check_for_req_inflight(drv, tcs, msg);
	if (ret)
		return ret;

	return find_free_tcs(tcs);
}

/**
 * rpmh_rsc_send_data() - Write / trigger active-only message.
 * @drv: The controller.
 * @msg: The data to be sent.
 *
 * NOTES:
 * - This is only used for "ACTIVE_ONLY" since the limitations of this
 *   function don't make sense for sleep/wake cases.
 * - To do the transfer, we will grab a whole TCS for ourselves--we don't
 *   try to share. If there are none available we'll wait indefinitely
 *   for a free one.
 * - This function will not wait for the commands to be finished, only for
 *   data to be programmed into the RPMh. See rpmh_tx_done() which will
 *   be called when the transfer is fully complete.
 * - This function must be called with interrupts enabled. If the hardware
 *   is busy doing someone else's transfer we need that transfer to fully
 *   finish so that we can have the hardware, and to fully finish it needs
 *   the interrupt handler to run. If the interrupts is set to run on the
 *   active CPU this can never happen if interrupts are disabled.
 *
 * Return: 0 on success, -EINVAL on error.
 */
int rpmh_rsc_send_data(struct rsc_drv *drv, const struct tcs_request *msg)
{
	struct tcs_group *tcs;
	int tcs_id = 0, ret;

	might_sleep();

	tcs = get_tcs_for_msg(drv, msg);
	if (IS_ERR(tcs))
		return PTR_ERR(tcs);

	if (drv->iface) {
		ret = pm_runtime_resume_and_get(drv->dev);
		if (ret < 0)
			return ret;
	}

	spin_lock_irq(&drv->lock);

	/* Wait forever for a free tcs. It better be there eventually! */
	wait_event_lock_irq(drv->tcs_wait,
			    drv->in_solver ||
			    (tcs_id = claim_tcs_for_req(drv, tcs, msg)) >= 0,
			    drv->lock);

	if (drv->in_solver) {
		spin_unlock_irq(&drv->lock);
		if (drv->iface)
			pm_runtime_put(drv->dev);
		return -EBUSY;
	}

	tcs->req[tcs_id - tcs->offset] = msg;
	set_bit(tcs_id, drv->tcs_in_use);
	if (msg->state == RPMH_ACTIVE_ONLY_STATE && tcs->type != ACTIVE_TCS) {
		/*
		 * Clear previously programmed WAKE commands in selected
		 * repurposed TCS to avoid triggering them. tcs->slots will be
		 * cleaned from rpmh_flush() by invoking rpmh_rsc_invalidate()
		 */
		write_tcs_reg_sync(drv, drv->regs[RSC_DRV_CMD_ENABLE], tcs_id, 0);
		spin_lock(&drv->client.cache_lock);
		drv->client.dirty = true;
		spin_unlock(&drv->client.cache_lock);
		enable_tcs_irq(drv, tcs_id, true);
	}
	spin_unlock_irq(&drv->lock);

	/*
	 * These two can be done after the lock is released because:
	 * - We marked "tcs_in_use" under lock.
	 * - Once "tcs_in_use" has been marked nobody else could be writing
	 *   to these registers until the interrupt goes off.
	 * - The interrupt can't go off until we trigger w/ the last line
	 *   of __tcs_set_trigger() below.
	 */
	__tcs_buffer_write(drv, tcs_id, 0, msg);
	__tcs_set_trigger(drv, tcs_id, true);

	return 0;
}

/**
 * find_slots() - Find a place to write the given message.
 * @tcs:    The tcs group to search.
 * @msg:    The message we want to find room for.
 * @tcs_id: If we return 0 from the function, we return the global ID of the
 *          TCS to write to here.
 * @cmd_id: If we return 0 from the function, we return the index of
 *          the command array of the returned TCS where the client should
 *          start writing the message.
 *
 * Only for use on sleep/wake TCSes since those are the only ones we maintain
 * tcs->slots for.
 *
 * Return: -ENOMEM if there was no room, else 0.
 */
static int find_slots(struct tcs_group *tcs, const struct tcs_request *msg,
		      int *tcs_id, int *cmd_id)
{
	int slot, offset;
	int i = 0;

	/* Do over, until we can fit the full payload in a single TCS */
	do {
		slot = bitmap_find_next_zero_area(tcs->slots, MAX_TCS_SLOTS,
						  i, msg->num_cmds, 0);
		if (slot >= tcs->num_tcs * tcs->ncpt)
			return -ENOMEM;
		i += tcs->ncpt;
	} while (slot + msg->num_cmds - 1 >= i);

	bitmap_set(tcs->slots, slot, msg->num_cmds);

	offset = slot / tcs->ncpt;
	*tcs_id = offset + tcs->offset;
	*cmd_id = slot % tcs->ncpt;

	return 0;
}

/**
 * rpmh_rsc_write_ctrl_data() - Write request to controller but don't trigger.
 * @drv: The controller.
 * @msg: The data to be written to the controller.
 *
 * This should only be called for sleep/wake state, never active-only
 * state.
 *
 * The caller must ensure that no other RPMH actions are happening and the
 * controller is idle when this function is called since it runs lockless.
 *
 * Return: 0 if no error; else -error.
 */
int rpmh_rsc_write_ctrl_data(struct rsc_drv *drv, const struct tcs_request *msg)
{
	struct tcs_group *tcs;
	int tcs_id = 0, cmd_id = 0;
	int ret;

	tcs = get_tcs_for_msg(drv, msg);
	if (IS_ERR(tcs))
		return PTR_ERR(tcs);

	/* find the TCS id and the command in the TCS to write to */
	ret = find_slots(tcs, msg, &tcs_id, &cmd_id);
	if (!ret)
		__tcs_buffer_write(drv, tcs_id, cmd_id, msg);

	return ret;
}

/**
 * rpmh_rsc_ctrlr_is_busy() - Check if any of the AMCs are busy.
 * @drv: The controller
 *
 * Checks if any of the AMCs are busy in handling ACTIVE sets.
 * This is called from the last cpu powering down before flushing
 * SLEEP and WAKE sets. If AMCs are busy, controller can not enter
 * power collapse, so deny from the last cpu's pm notification.
 *
 * Context: Must be called with the drv->lock held.
 *
 * Return:
 * * False		- AMCs are idle
 * * True		- AMCs are busy
 */
static bool rpmh_rsc_ctrlr_is_busy(struct rsc_drv *drv)
{
	unsigned long set;
	const struct tcs_group *tcs = &drv->tcs[ACTIVE_TCS];
	unsigned long max;

	/*
	 * If we made an active request on a RSC that does not have a
	 * dedicated TCS for active state use, then re-purposed wake TCSes
	 * should be checked for not busy, because we used wake TCSes for
	 * active requests in this case.
	 */
	if (!tcs->num_tcs)
		tcs = &drv->tcs[WAKE_TCS];

	max = tcs->offset + tcs->num_tcs;
	set = find_next_bit(drv->tcs_in_use, max, tcs->offset);

	return set < max;
}

/* The display coordinator programs the solver before announcing its mode. */
int rpmh_mode_solver_set(const struct device *dev, bool enable)
{
	struct rsc_drv *drv = dev_get_drvdata(dev->parent);
	unsigned long flags;
	int ret = 0;

	if (!drv || !drv->has_solver)
		return -EOPNOTSUPP;
	spin_lock_irqsave(&drv->lock, flags);
	if (enable && rpmh_rsc_ctrlr_is_busy(drv))
		ret = -EBUSY;
	else
		drv->in_solver = enable;
	spin_unlock_irqrestore(&drv->lock, flags);
	wake_up(&drv->tcs_wait);
	return ret;
}
EXPORT_SYMBOL_GPL(rpmh_mode_solver_set);

/* The caller holds channel_lock and keeps the RSC interface clock on. */
static int rpmh_program_channel(struct rsc_drv *drv, bool start)
{
	u32 channel = 0, status;
	unsigned long flags;
	int ret;

	if (!start) {
		u32 selected = readl(drv->base + RSC_CH_UPDATE);

		if (selected != BIT(0) && selected != BIT(1))
			return -EBUSY;
		ret = readl_poll_timeout(drv->base + RSC_CH_BUSY, status,
					 status == selected, 1, 100);
		if (ret)
			return ret;
		channel = selected == BIT(0);
	}

	spin_lock_irqsave(&drv->client.cache_lock, flags);
	drv->program_second = channel;
	/* This bank can still contain the votes from two frames ago. */
	drv->client.dirty = true;
	ret = rpmh_flush_locked(&drv->client);
	spin_unlock_irqrestore(&drv->client.cache_lock, flags);
	if (ret)
		return ret;

	writel(RSC_CH_CLEAR_STATUS, drv->base + RSC_CH_COMPLETE);
	if (start)
		writel(1, drv->base + RSC_CH_ENABLE);
	writel(BIT(channel), drv->base + RSC_CH_UPDATE);
	ret = readl_poll_timeout(drv->base + RSC_CH_COMPLETE, status,
				 status & BIT(2 * channel), 1, 100);
	if (!ret)
		writel(RSC_CH_CLEAR_STATUS, drv->base + RSC_CH_COMPLETE);
	return ret;
}

int rpmh_hw_channel_start(const struct device *voter)
{
	struct rsc_drv *drv = dev_get_drvdata(voter->parent);
	int ret;

	if (!drv || !drv->second_tcs)
		return -EOPNOTSUPP;
	guard(mutex)(&drv->channel_lock);
	if (drv->channel_running)
		return -EALREADY;
	ret = pm_runtime_resume_and_get(drv->dev);
	if (ret < 0)
		return ret;
	ret = rpmh_program_channel(drv, true);
	if (ret) {
		writel(0, drv->base + RSC_CH_UPDATE);
		writel(0, drv->base + RSC_CH_ENABLE);
		pm_runtime_put(drv->dev);
		return ret;
	}
	drv->channel_running = true;
	return 0;
}
EXPORT_SYMBOL_GPL(rpmh_hw_channel_start);

int rpmh_hw_channel_stop(const struct device *voter)
{
	struct rsc_drv *drv = dev_get_drvdata(voter->parent);
	int ret;

	if (!drv || !drv->second_tcs)
		return -EOPNOTSUPP;
	guard(mutex)(&drv->channel_lock);
	if (!drv->channel_running)
		return -EINVAL;
	ret = rpmh_program_channel(drv, false);
	if (ret)
		return ret;
	writel(0, drv->base + RSC_CH_UPDATE);
	writel(0, drv->base + RSC_CH_ENABLE);
	drv->channel_running = false;
	pm_runtime_put(drv->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(rpmh_hw_channel_stop);

int rpmh_write_sleep_and_wake(const struct device *dev)
{
	struct rsc_drv *drv = dev_get_drvdata(dev->parent);
	unsigned long flags;
	int ret;

	if (drv && drv->second_tcs) {
		guard(mutex)(&drv->channel_lock);
		if (!drv->channel_running)
			return -EINVAL;
		return rpmh_program_channel(drv, false);
	}
	if (!drv || !drv->has_solver)
		return -EOPNOTSUPP;
	if (drv->iface) {
		ret = pm_runtime_resume_and_get(drv->dev);
		if (ret < 0)
			return ret;
	}
	spin_lock_irqsave(&drv->lock, flags);
	if (rpmh_rsc_ctrlr_is_busy(drv)) {
		ret = -EBUSY;
	} else {
		spin_lock(&drv->client.cache_lock);
		ret = rpmh_flush_locked(&drv->client);
		spin_unlock(&drv->client.cache_lock);
	}
	spin_unlock_irqrestore(&drv->lock, flags);
	if (drv->iface)
		pm_runtime_put(drv->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(rpmh_write_sleep_and_wake);

/**
 * rpmh_rsc_write_next_wakeup() - Write next wakeup in CONTROL_TCS.
 * @drv: The controller
 *
 * Writes maximum wakeup cycles when called from suspend.
 * Writes earliest hrtimer wakeup when called from idle.
 */
void rpmh_rsc_write_next_wakeup(struct rsc_drv *drv)
{
	ktime_t now, wakeup;
	u64 wakeup_us, wakeup_cycles = ~0;
	u32 lo, hi;

	if (!drv->tcs[CONTROL_TCS].num_tcs || !drv->genpd_nb.notifier_call)
		return;

	/* Set highest time when system (timekeeping) is suspended */
	if (system_state == SYSTEM_SUSPEND)
		goto exit;

	/* Find the earliest hrtimer wakeup from online cpus */
	wakeup = dev_pm_genpd_get_next_hrtimer(drv->dev);

	/* Find the relative wakeup in kernel time scale */
	now = ktime_get();
	wakeup = ktime_sub(wakeup, now);
	wakeup_us = ktime_to_us(wakeup);

	/* Convert the wakeup to arch timer scale */
	wakeup_cycles = USECS_TO_CYCLES(wakeup_us);
	wakeup_cycles += arch_timer_read_counter();

exit:
	lo = wakeup_cycles & RSC_DRV_CTL_TCS_DATA_LO_MASK;
	hi = wakeup_cycles >> RSC_DRV_CTL_TCS_DATA_SIZE;
	hi &= RSC_DRV_CTL_TCS_DATA_HI_MASK;
	hi |= RSC_DRV_CTL_TCS_DATA_HI_VALID;

	writel_relaxed(lo, drv->base + RSC_DRV_CTL_TCS_DATA_LO);
	writel_relaxed(hi, drv->base + RSC_DRV_CTL_TCS_DATA_HI);
}

/**
 * rpmh_rsc_cpu_pm_callback() - Check if any of the AMCs are busy.
 * @nfb:    Pointer to the notifier block in struct rsc_drv.
 * @action: CPU_PM_ENTER, CPU_PM_ENTER_FAILED, or CPU_PM_EXIT.
 * @v:      Unused
 *
 * This function is given to cpu_pm_register_notifier so we can be informed
 * about when CPUs go down. When all CPUs go down we know no more active
 * transfers will be started so we write sleep/wake sets. This function gets
 * called from cpuidle code paths and also at system suspend time.
 *
 * If its last CPU going down and AMCs are not busy then writes cached sleep
 * and wake messages to TCSes. The firmware then takes care of triggering
 * them when entering deepest low power modes.
 *
 * Return: See cpu_pm_register_notifier()
 */
static int rpmh_rsc_cpu_pm_callback(struct notifier_block *nfb,
				    unsigned long action, void *v)
{
	struct rsc_drv *drv = container_of(nfb, struct rsc_drv, rsc_pm);
	int ret = NOTIFY_OK;
	int cpus_in_pm;

	switch (action) {
	case CPU_PM_ENTER:
		cpus_in_pm = atomic_inc_return(&drv->cpus_in_pm);
		/*
		 * NOTE: comments for num_online_cpus() point out that it's
		 * only a snapshot so we need to be careful. It should be OK
		 * for us to use, though.  It's important for us not to miss
		 * if we're the last CPU going down so it would only be a
		 * problem if a CPU went offline right after we did the check
		 * AND that CPU was not idle AND that CPU was the last non-idle
		 * CPU. That can't happen. CPUs would have to come out of idle
		 * before the CPU could go offline.
		 */
		if (cpus_in_pm < num_online_cpus())
			return NOTIFY_OK;
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		atomic_dec(&drv->cpus_in_pm);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}

	/*
	 * It's likely we're on the last CPU. Grab the drv->lock and write
	 * out the sleep/wake commands to RPMH hardware. Grabbing the lock
	 * means that if we race with another CPU coming up we are still
	 * guaranteed to be safe. If another CPU came up just after we checked
	 * and has grabbed the lock or started an active transfer then we'll
	 * notice we're busy and abort. If another CPU comes up after we start
	 * flushing it will be blocked from starting an active transfer until
	 * we're done flushing. If another CPU starts an active transfer after
	 * we release the lock we're still OK because we're no longer the last
	 * CPU.
	 */
	if (spin_trylock(&drv->lock)) {
		if (rpmh_rsc_ctrlr_is_busy(drv) || rpmh_flush(&drv->client))
			ret = NOTIFY_BAD;
		spin_unlock(&drv->lock);
	} else {
		/* Another CPU must be up */
		return NOTIFY_OK;
	}

	if (ret == NOTIFY_BAD) {
		/* Double-check if we're here because someone else is up */
		if (cpus_in_pm < num_online_cpus())
			ret = NOTIFY_OK;
		else
			/* We won't be called w/ CPU_PM_ENTER_FAILED */
			atomic_dec(&drv->cpus_in_pm);
	}

	return ret;
}

/**
 * rpmh_rsc_pd_callback() - Check if any of the AMCs are busy.
 * @nfb:    Pointer to the genpd notifier block in struct rsc_drv.
 * @action: GENPD_NOTIFY_PRE_OFF, GENPD_NOTIFY_OFF, GENPD_NOTIFY_PRE_ON or GENPD_NOTIFY_ON.
 * @v:      Unused
 *
 * This function is given to dev_pm_genpd_add_notifier() so we can be informed
 * about when cluster-pd is going down. When cluster go down we know no more active
 * transfers will be started so we write sleep/wake sets. This function gets
 * called from cpuidle code paths and also at system suspend time.
 *
 * If AMCs are not busy then writes cached sleep and wake messages to TCSes.
 * The firmware then takes care of triggering them when entering deepest low power modes.
 *
 * Return:
 * * NOTIFY_OK          - success
 * * NOTIFY_BAD         - failure
 */
static int rpmh_rsc_pd_callback(struct notifier_block *nfb,
				unsigned long action, void *v)
{
	struct rsc_drv *drv = container_of(nfb, struct rsc_drv, genpd_nb);

	/* We don't need to lock as genpd on/off are serialized */
	if ((action == GENPD_NOTIFY_PRE_OFF) &&
	    (rpmh_rsc_ctrlr_is_busy(drv) || rpmh_flush(&drv->client)))
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static int rpmh_rsc_pd_attach(struct rsc_drv *drv, struct device *dev)
{
	int ret;

	pm_runtime_enable(dev);
	drv->genpd_nb.notifier_call = rpmh_rsc_pd_callback;
	ret = dev_pm_genpd_add_notifier(dev, &drv->genpd_nb);
	if (ret)
		pm_runtime_disable(dev);

	return ret;
}

static int rpmh_probe_tcs_config(struct device_node *dn, struct device_node *channel,
				 struct rsc_drv *drv, struct tcs_group *groups)
{
	struct tcs_type_config {
		u32 type;
		u32 n;
	} tcs_cfg[TCS_TYPE_NR] = { { 0 } };
	struct device_node *cfg = channel ?: dn;
	u32 fast_type, fast_count = 0;
	u32 config, max_tcs, ncpt, offset;
	int i, ret, n, st = 0;
	struct tcs_group *tcs;

	ret = of_property_read_u32(dn, "qcom,tcs-offset", &offset);
	if (ret)
		return ret;
	drv->tcs_base = drv->base + offset;

	config = readl_relaxed(drv->base + drv->regs[DRV_PRNT_CHLD_CONFIG]);

	max_tcs = config;
	max_tcs &= DRV_NUM_TCS_MASK << (DRV_NUM_TCS_SHIFT * drv->id);
	max_tcs = max_tcs >> (DRV_NUM_TCS_SHIFT * drv->id);

	ncpt = config & (DRV_NCPT_MASK << DRV_NCPT_SHIFT);
	ncpt = ncpt >> DRV_NCPT_SHIFT;

	if (!ncpt || ncpt > MAX_CMDS_PER_TCS)
		return -EINVAL;

	n = of_property_count_u32_elems(cfg, "qcom,tcs-config");
	if (n != 2 * (TCS_TYPE_NR + !!channel))
		return -EINVAL;

	for (i = 0; i < TCS_TYPE_NR; i++) {
		ret = of_property_read_u32_index(cfg, "qcom,tcs-config",
						 i * 2, &tcs_cfg[i].type);
		if (ret)
			return ret;
		if (tcs_cfg[i].type >= TCS_TYPE_NR)
			return -EINVAL;

		ret = of_property_read_u32_index(cfg, "qcom,tcs-config",
						 i * 2 + 1, &tcs_cfg[i].n);
		if (ret)
			return ret;
		if (tcs_cfg[i].n > MAX_TCS_PER_TYPE)
			return -EINVAL;
	}

	for (i = 0; i < TCS_TYPE_NR; i++) {
		tcs = &groups[tcs_cfg[i].type];
		if (tcs->drv)
			return -EINVAL;
		tcs->drv = drv;
		tcs->type = tcs_cfg[i].type;
		tcs->num_tcs = tcs_cfg[i].n;
		tcs->ncpt = ncpt;

		if (drv->second_tcs && tcs->num_tcs !=
		    (tcs->type == SLEEP_TCS || tcs->type == WAKE_TCS))
			return -EINVAL;
		if (!tcs->num_tcs || tcs->type == CONTROL_TCS)
			continue;

		if (st + tcs->num_tcs > max_tcs ||
		    drv->num_tcs + st + tcs->num_tcs >= BITS_PER_BYTE * sizeof(tcs->mask))
			return -EINVAL;

		tcs->offset = drv->num_tcs + st;
		tcs->mask = ((1 << tcs->num_tcs) - 1) << tcs->offset;
		st += tcs->num_tcs;
	}

	/*
	 * The single-channel AP binding puts the dedicated DCVS slot last.
	 * Leave it outside the pool used for ordinary RPMh transactions.
	 */
	if (channel) {
		ret = of_property_read_u32_index(cfg, "qcom,tcs-config",
						 2 * TCS_TYPE_NR, &fast_type);
		if (ret || fast_type != FAST_PATH_TCS)
			return -EINVAL;
		ret = of_property_read_u32_index(cfg, "qcom,tcs-config",
						 2 * TCS_TYPE_NR + 1, &fast_count);
		if (ret || fast_count > !drv->second_tcs || st + fast_count > max_tcs)
			return -EINVAL;
		drv->fast_cmd_slots = fast_count ? ncpt : 0;
	}

	drv->num_tcs += st;

	return 0;
}

static void rpmh_unregister_drv(void *data)
{
	struct platform_device *child = data;

	of_platform_device_destroy(&child->dev, NULL);
}

static void rpmh_remove_pd_notifier(void *data)
{
	struct device *dev = data;

	dev_pm_genpd_remove_notifier(dev);
	pm_runtime_disable(dev);
}

static void rpmh_remove_cpu_notifier(void *data)
{
	cpu_pm_unregister_notifier(data);
}

static int rpmh_rsc_runtime_resume(struct device *dev)
{
	struct rsc_drv *drv = dev_get_drvdata(dev);

	return clk_prepare_enable(drv->iface);
}

static int rpmh_rsc_runtime_suspend(struct device *dev)
{
	struct rsc_drv *drv = dev_get_drvdata(dev);

	clk_disable_unprepare(drv->iface);
	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(rpmh_rsc_pm_ops, rpmh_rsc_runtime_suspend,
				 rpmh_rsc_runtime_resume, NULL);

DEFINE_FREE(rpmh_put_probe_vote, struct device *, if (_T) pm_runtime_put_sync(_T));

static int rpmh_rsc_probe_drv(struct platform_device *pdev, struct device_node *nested,
			      struct rsc_drv *drv, bool display, bool camera)
{
	struct device_node *dn = pdev->dev.of_node;
	struct device_node *channel __free(device_node) = NULL;
	struct device_node *second __free(device_node) = NULL;
	struct device *client_parent = &pdev->dev;
	char drv_id[10];
	u32 solver_config, rsc_id;
	int irq, ret;

	if (nested) {
		for_each_available_child_of_node_scoped(nested, child) {
			const char *unit = kbasename(child->full_name);

			if (!of_node_name_eq(child, "channel"))
				continue;
			if (!strcmp(unit, "channel@0") && !channel)
				channel = of_node_get(child);
			else if (camera && !strcmp(unit, "channel@1") && !second)
				second = of_node_get(child);
			else
				return -EINVAL;
		}
		if (!channel || (camera && !second))
			return -EINVAL;
	}

	ret = of_property_read_u32(nested ?: dn, "qcom,drv-id", &drv->id);
	if (ret)
		return ret;

	if (nested && !camera && drv->id != (display ? 0 : 2))
		return -EINVAL;

	drv->name = of_get_property(dn, "label", NULL);
	if (!drv->name)
		drv->name = dev_name(&pdev->dev);

	snprintf(drv_id, ARRAY_SIZE(drv_id), "drv-%d", drv->id);
	drv->base = devm_platform_ioremap_resource_byname(pdev, drv_id);
	if (IS_ERR(drv->base))
		return PTR_ERR(drv->base);

	rsc_id = readl_relaxed(drv->base + RSC_DRV_ID);
	drv->ver.major = rsc_id & (MAJOR_VER_MASK << MAJOR_VER_SHIFT);
	drv->ver.major >>= MAJOR_VER_SHIFT;
	drv->ver.minor = rsc_id & (MINOR_VER_MASK << MINOR_VER_SHIFT);
	drv->ver.minor >>= MINOR_VER_SHIFT;

	if (drv->ver.major >= 3)
		drv->regs = rpmh_rsc_reg_offset_ver_3_0;
	else
		drv->regs = rpmh_rsc_reg_offset_ver_2_7;

	if (camera) {
		if (drv->ver.major != 3)
			return -ENODEV;
		drv->second_tcs = devm_kcalloc(&pdev->dev, TCS_TYPE_NR,
					       sizeof(*drv->second_tcs), GFP_KERNEL);
		if (!drv->second_tcs)
			return -ENOMEM;
		mutex_init(&drv->channel_lock);
	}

	ret = rpmh_probe_tcs_config(nested ?: dn, channel, drv, drv->tcs);
	if (ret)
		return ret;
	if (camera) {
		ret = rpmh_probe_tcs_config(nested, second, drv, drv->second_tcs);
		if (ret)
			return ret;
	}

	spin_lock_init(&drv->lock);
	init_waitqueue_head(&drv->tcs_wait);
	bitmap_zero(drv->tcs_in_use, MAX_TCS_NR);

	irq = platform_get_irq(pdev, drv->id);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, tcs_tx_done,
			       IRQF_TRIGGER_HIGH | IRQF_NO_SUSPEND,
			       drv->name, drv);
	if (ret)
		return ret;

	drv->dev = &pdev->dev;
	spin_lock_init(&drv->client.cache_lock);
	INIT_LIST_HEAD(&drv->client.cache);
	INIT_LIST_HEAD(&drv->client.batch_cache);

	/*
	 * CPU PM/genpd notification are not required for controllers that support
	 * 'HW solver' mode where they can be in autonomous mode executing low
	 * power mode to power down.
	 */
	solver_config = readl_relaxed(drv->base + drv->regs[DRV_SOLVER_CONFIG]);
	solver_config &= DRV_HW_SOLVER_MASK << DRV_HW_SOLVER_SHIFT;
	solver_config = solver_config >> DRV_HW_SOLVER_SHIFT;
	drv->has_solver = solver_config;
	if (display && !drv->has_solver)
		return dev_err_probe(&pdev->dev, -ENODEV, "display solver unavailable\n");
	if (!camera && (!solver_config || (nested && pdev->dev.pm_domain))) {
		if (pdev->dev.pm_domain) {
			ret = rpmh_rsc_pd_attach(drv, &pdev->dev);
			if (!ret)
				ret = devm_add_action_or_reset(&pdev->dev,
						rpmh_remove_pd_notifier, &pdev->dev);
		} else {
			drv->rsc_pm.notifier_call = rpmh_rsc_cpu_pm_callback;
			ret = cpu_pm_register_notifier(&drv->rsc_pm);
			if (!ret)
				ret = devm_add_action_or_reset(&pdev->dev,
						rpmh_remove_cpu_notifier, &drv->rsc_pm);
		}
		if (ret)
			return ret;
	}

	/* Enable the active TCS to send requests immediately */
	writel_relaxed(drv->tcs[ACTIVE_TCS].mask,
		       drv->tcs_base + drv->regs[RSC_DRV_IRQ_ENABLE]);

	if (nested) {
		struct platform_device *child;

		child = of_platform_device_create(nested, NULL, &pdev->dev);
		if (!child)
			return -ENODEV;
		ret = devm_add_action_or_reset(&pdev->dev, rpmh_unregister_drv, child);
		if (ret)
			return ret;
		client_parent = &child->dev;
	}

	dev_set_drvdata(client_parent, drv);
	if (nested)
		return of_platform_populate(nested, NULL, NULL, client_parent);
	return devm_of_platform_populate(client_parent);
}

static int rpmh_rsc_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct device *probe_vote __free(rpmh_put_probe_vote) = NULL;
	bool camera = of_property_present(dn, "qcom,hw-channel");
	bool display = of_property_match_string(dn, "label", "disp_rsc") >= 0;
	struct rsc_drv *drvs;
	unsigned long seen = 0;
	u32 count = 1;
	int ret;

	ret = cmd_db_ready();
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Command DB not available\n");

	if (of_property_present(dn, "qcom,drv-count")) {
		if (of_property_read_u32(dn, "qcom,drv-count", &count) ||
		    count != (display ? 1 : 3) ||
		    of_get_available_child_count(dn) != (camera ? count : 1))
			return -EINVAL;
		if (camera) {
			if (of_property_match_string(dn, "label", "cam_rsc") < 0)
				return -EOPNOTSUPP;
		} else if (!display && of_property_match_string(dn, "label", "apps_rsc") < 0) {
			return -EOPNOTSUPP;
		}
	} else if (camera) {
		return -EINVAL;
	}

	drvs = devm_kcalloc(&pdev->dev, count, sizeof(*drvs), GFP_KERNEL);
	if (!drvs)
		return -ENOMEM;
	platform_set_drvdata(pdev, drvs);
	drvs[0].dev = &pdev->dev;
	if (display || camera) {
		drvs[0].iface = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(drvs[0].iface))
			return dev_err_probe(&pdev->dev, PTR_ERR(drvs[0].iface), "RSC bus clock\n");
		ret = devm_pm_runtime_enable(&pdev->dev);
		if (ret)
			return ret;
		ret = pm_runtime_resume_and_get(&pdev->dev);
		if (ret < 0)
			return ret;
		probe_vote = &pdev->dev;
	}

	if (!of_property_present(dn, "qcom,drv-count"))
		return rpmh_rsc_probe_drv(pdev, NULL, drvs, false, false);

	for_each_available_child_of_node_scoped(dn, child) {
		u32 id;

		if (!of_node_name_eq(child, "drv") ||
		    of_property_read_u32(child, "qcom,drv-id", &id) ||
		    id >= count || test_and_set_bit(id, &seen))
			return -EINVAL;
		/* Single-DRV bindings keep the root as their controller. */
		ret = rpmh_rsc_probe_drv(pdev, child, &drvs[camera ? id : 0], display, camera);
		if (ret)
			return ret;
	}
	return 0;
}

static const struct of_device_id rpmh_drv_match[] = {
	{ .compatible = "qcom,rpmh-rsc", },
	{ }
};
MODULE_DEVICE_TABLE(of, rpmh_drv_match);

static struct platform_driver rpmh_driver = {
	.probe = rpmh_rsc_probe,
	.driver = {
		  .name = "rpmh",
		  .of_match_table = rpmh_drv_match,
		  .pm = pm_ptr(&rpmh_rsc_pm_ops),
		  .suppress_bind_attrs = true,
	},
};

static int __init rpmh_driver_init(void)
{
	return platform_driver_register(&rpmh_driver);
}
core_initcall(rpmh_driver_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. RPMh Driver");
MODULE_LICENSE("GPL v2");
