// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Gunyah resource-manager virtual terminals for the primary VM.
 */
#include <linux/gunyah/gh_common.h>
#include <linux/gunyah/gh_msgq.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include "hvc_console.h"

#define GH_VTERM_BASE	0x474e5948
/* RM common header (8) and console header (4) in a single message. */
#define GH_WRITE_SIZE	(GH_MSGQ_MAX_MSG_SIZE_BYTES - 12)

struct gh_terminal {
	struct hvc_struct *hvc;
	spinlock_t lock;
	DECLARE_KFIFO(rx, u8, 1024);
	DECLARE_KFIFO(tx, u8, 1024);
	struct work_struct work;
	gh_vmid_t vmid;
	int tx_status;
	bool opened;
};

static struct gh_terminal terminals[GH_VM_MAX];

static ssize_t gh_hvc_get(u32 vterm, u8 *buf, size_t count)
{
	unsigned int index = vterm - GH_VTERM_BASE;
	struct gh_terminal *term;

	if (index >= GH_VM_MAX)
		return -EINVAL;
	term = &terminals[index];
	return kfifo_out_spinlocked(&term->rx, buf, count, &term->lock);
}

static void gh_hvc_write_work(struct work_struct *work)
{
	struct gh_terminal *term = container_of(work, struct gh_terminal, work);
	unsigned long flags;
	u8 buf[GH_WRITE_SIZE];
	unsigned int len;
	int ret;

	for (;;) {
		spin_lock_irqsave(&term->lock, flags);
		len = kfifo_out_peek(&term->tx, buf, sizeof(buf));
		spin_unlock_irqrestore(&term->lock, flags);
		if (!len)
			break;
		ret = gh_rm_console_write(term->vmid, buf, len);
		WRITE_ONCE(term->tx_status, ret);
		if (ret)
			break;
		spin_lock_irqsave(&term->lock, flags);
		kfifo_skip_count(&term->tx, len);
		spin_unlock_irqrestore(&term->lock, flags);
		hvc_kick();
	}
}

static ssize_t gh_hvc_put(u32 vterm, const u8 *buf, size_t count)
{
	unsigned int index = vterm - GH_VTERM_BASE;
	struct gh_terminal *term;
	unsigned long flags;
	ssize_t accepted;

	if (index >= GH_VM_MAX)
		return -EINVAL;
	term = &terminals[index];
	spin_lock_irqsave(&term->lock, flags);
	if (term->opened) {
		accepted = kfifo_in(&term->tx, buf, count);
		schedule_work(&term->work);
	} else {
		accepted = -ENODEV;
	}
	spin_unlock_irqrestore(&term->lock, flags);
	return accepted;
}

static int gh_hvc_flush(u32 vterm, bool wait)
{
	unsigned int index = vterm - GH_VTERM_BASE;
	struct gh_terminal *term;
	int ret;

	if (index >= GH_VM_MAX)
		return -EINVAL;
	if (!wait)
		return 0;
	term = &terminals[index];
	flush_work(&term->work);
	ret = READ_ONCE(term->tx_status);
	return ret ?: gh_rm_console_flush(term->vmid);
}

static int gh_hvc_open(struct hvc_struct *hp, int index)
{
	struct gh_terminal *term = &terminals[index];
	unsigned long flags;
	gh_vmid_t vmid;
	int ret;

	ret = gh_rm_get_vmid(index, &vmid);
	if (ret)
		return ret;
	ret = gh_rm_console_open(vmid);
	if (ret)
		return ret;
	spin_lock_irqsave(&term->lock, flags);
	term->vmid = vmid;
	term->tx_status = 0;
	term->opened = true;
	spin_unlock_irqrestore(&term->lock, flags);
	return 0;
}

static void gh_hvc_close(struct hvc_struct *hp, int index)
{
	struct gh_terminal *term = &terminals[index];
	unsigned long flags;

	spin_lock_irqsave(&term->lock, flags);
	if (!term->opened) {
		spin_unlock_irqrestore(&term->lock, flags);
		return;
	}
	term->opened = false;
	spin_unlock_irqrestore(&term->lock, flags);
	flush_work(&term->work);
	gh_rm_console_close(term->vmid);
	spin_lock_irqsave(&term->lock, flags);
	kfifo_reset(&term->rx);
	kfifo_reset(&term->tx);
	spin_unlock_irqrestore(&term->lock, flags);
}

static int gh_hvc_notify(struct notifier_block *nb, unsigned long event, void *data)
{
	struct gh_rm_notif_vm_console_chars *msg = data;
	struct gh_terminal *term;
	enum gh_vm_names vm;
	unsigned long flags;

	if (event != GH_RM_NOTIF_VM_CONSOLE_CHARS)
		return NOTIFY_DONE;
	if (gh_rm_get_vm_name(msg->vmid, &vm) || (unsigned int)vm >= GH_VM_MAX)
		return NOTIFY_DONE;
	term = &terminals[vm];
	spin_lock_irqsave(&term->lock, flags);
	if (term->opened)
		kfifo_in(&term->rx, msg->bytes, msg->num_bytes);
	spin_unlock_irqrestore(&term->lock, flags);
	hvc_kick();
	return NOTIFY_OK;
}

static struct notifier_block gh_hvc_nb = {
	.notifier_call = gh_hvc_notify,
};
static const struct hv_ops gh_ops = {
	.get_chars = gh_hvc_get,
	.put_chars = gh_hvc_put,
	.flush = gh_hvc_flush,
	.notifier_add = gh_hvc_open,
	.notifier_del = gh_hvc_close,
	.notifier_hangup = gh_hvc_close,
};

static void gh_hvc_remove(unsigned int count)
{
	while (count) {
		struct gh_terminal *term = &terminals[--count];

		hvc_remove(term->hvc);
		cancel_work_sync(&term->work);
	}
}

static int __init gh_hvc_init(void)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(terminals); i++) {
		struct gh_terminal *term = &terminals[i];

		spin_lock_init(&term->lock);
		INIT_KFIFO(term->rx);
		INIT_KFIFO(term->tx);
		INIT_WORK(&term->work, gh_hvc_write_work);
		term->hvc = hvc_alloc(GH_VTERM_BASE + i, i, &gh_ops, 256);
		if (IS_ERR(term->hvc)) {
			ret = PTR_ERR(term->hvc);
			goto remove;
		}
	}
	ret = gh_rm_register_notifier(&gh_hvc_nb);
	if (!ret)
		return 0;
remove:
	gh_hvc_remove(i);
	return ret;
}
module_init(gh_hvc_init);

static void __exit gh_hvc_exit(void)
{
	gh_rm_unregister_notifier(&gh_hvc_nb);
	gh_hvc_remove(ARRAY_SIZE(terminals));
}
module_exit(gh_hvc_exit);
MODULE_DESCRIPTION("Gunyah resource-manager VM terminals");
MODULE_LICENSE("GPL");
