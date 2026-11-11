// SPDX-License-Identifier: GPL-2.0-only
/* Export a live, coherent ETR ring to the Flyme qdss_sw IN endpoint. */
#include <linux/coresight.h>
#include <linux/dma-map-ops.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/usb/usb_qdss.h>
#include <linux/workqueue.h>
#include "coresight-tmc.h"

#define QDSS_DEPTH 8
#define QDSS_PACKET 4096
#define QDSS_DEFAULT_BLOCK SZ_64K

struct tmc_usb_stream {
	struct tmc_drvdata *tmc;
	struct mutex mode_lock;
	struct usb_qdss_ch *channel;
	struct delayed_work pump;
	atomic64_t interrupts;
	bool online, selected, running;
	u32 origin, step, block;
	u64 consumed;
};

struct tmc_packet {
	struct qdss_request req;
	char data[QDSS_PACKET];
};

static irqreturn_t tmc_qdss_irq(int irq, void *data)
{
	struct tmc_usb_stream *usb = data;

	if (READ_ONCE(usb->running)) {
		atomic64_inc(&usb->interrupts);
		mod_delayed_work(system_wq, &usb->pump, 0);
	}
	return IRQ_HANDLED;
}

/* The ETR mode, buffer, and cursor are protected by drvdata->spinlock. */
void tmc_qdss_start(struct tmc_drvdata *tmc)
{
	struct tmc_usb_stream *usb = tmc->usb;
	long offset;

	if (!usb || !usb->selected || !tmc->etr_buf || usb->running)
		return;
	offset = tmc_etr_live_offset(tmc);
	if (offset < 0)
		return;
	usb->origin = offset;
	usb->consumed = 0;
	usb->step = round_down(min_t(u32, usb->block, tmc->etr_buf->size / 4), 8);
	if (!usb->step)
		return;
	atomic64_set(&usb->interrupts, 0);
	WRITE_ONCE(usb->running, true);
	tmc_kalama_byte_count(tmc, usb->step);
	mod_delayed_work(system_wq, &usb->pump, 0);
}

void tmc_qdss_stop(struct tmc_drvdata *tmc)
{
	if (tmc->usb && tmc->usb->running) {
		tmc_kalama_byte_count(tmc, 0);
		WRITE_ONCE(tmc->usb->running, false);
	}
}

static ssize_t tmc_qdss_copy(struct tmc_usb_stream *usb, char *dst, u64 *next)
{
	struct tmc_drvdata *tmc = usb->tmc;
	struct etr_buf *buf = tmc->etr_buf;
	u64 ticks, produced, read;
	unsigned long size, pos;
	char *src;
	ssize_t n;
	long rwp;

	if (!usb->running || !usb->online || !buf)
		return 0;
	rwp = tmc_etr_live_offset(tmc);
	if (rwp < 0)
		return rwp;
	size = buf->size;
	/* Choose the write-pointer epoch closest to the CSR byte count. */
	ticks = atomic64_read(&usb->interrupts) * usb->step;
	pos = (rwp + size - usb->origin) % size;
	produced = ticks - ticks % size + pos;
	if (produced + size / 2 < ticks)
		produced += size;
	else if (produced >= size && produced > ticks + size / 2)
		produced -= size;
	read = usb->consumed;
	if (produced <= read)
		return 0;
	/* If the writer lapped USB, discard overwritten bytes and retain a
	 * block of separation from the DMA engine during the CPU copy.
	 */
	if (produced - read > size - usb->step)
		read = produced - (size - usb->step);
	n = tmc_etr_buf_get_data(buf, (usb->origin + read) % size,
				 min_t(u64, QDSS_PACKET, produced - read), &src);
	if (n > 0) {
		dma_rmb();
		memcpy(dst, src, n);
		*next = read + n;
	}
	return n;
}

static void tmc_qdss_pump(struct work_struct *work)
{
	struct tmc_usb_stream *usb = container_of(to_delayed_work(work),
						  struct tmc_usb_stream, pump);
	struct tmc_drvdata *tmc = usb->tmc;
	int i;

	for (i = 0; i < QDSS_DEPTH; i++) {
		struct tmc_packet *packet;
		struct usb_qdss_ch *channel = READ_ONCE(usb->channel);
		unsigned long flags;
		u64 next = 0, previous;
		ssize_t n;

		if (!channel || !READ_ONCE(usb->online) || !READ_ONCE(usb->running))
			return;
		packet = kmalloc(sizeof(*packet), GFP_KERNEL);
		if (!packet)
			break;
		raw_spin_lock_irqsave(&tmc->spinlock, flags);
		n = tmc_qdss_copy(usb, packet->data, &next);
		previous = usb->consumed;
		if (n > 0)
			usb->consumed = next;
		raw_spin_unlock_irqrestore(&tmc->spinlock, flags);
		if (n <= 0) {
			kfree(packet);
			break;
		}
		packet->req = (struct qdss_request) { .buf = packet->data, .length = n };
		if (usb_qdss_write(channel, &packet->req)) {
			raw_spin_lock_irqsave(&tmc->spinlock, flags);
			if (usb->consumed == next)
				usb->consumed = previous;
			raw_spin_unlock_irqrestore(&tmc->spinlock, flags);
			kfree(packet);
			break;
		}
	}
	/* Drain the final fragment even when there is no full-block IRQ. */
	if (READ_ONCE(usb->online) && READ_ONCE(usb->running))
		queue_delayed_work(system_wq, &usb->pump, msecs_to_jiffies(20));
}

static void tmc_qdss_notify(void *data, unsigned int event,
			    struct qdss_request *req, struct usb_qdss_ch *channel)
{
	struct tmc_usb_stream *usb = data;

	switch (event) {
	case USB_QDSS_CONNECT:
		if (!READ_ONCE(usb->online) && !usb_qdss_alloc_req(channel, QDSS_DEPTH)) {
			WRITE_ONCE(usb->online, true);
			mod_delayed_work(system_wq, &usb->pump, 0);
		}
		break;
	case USB_QDSS_DISCONNECT:
		WRITE_ONCE(usb->online, false);
		cancel_delayed_work_sync(&usb->pump);
		channel = READ_ONCE(usb->channel);
		if (channel)
			usb_qdss_free_req(channel);
		break;
	case USB_QDSS_DATA_WRITE_DONE:
		kfree(container_of(req, struct tmc_packet, req));
		if (READ_ONCE(usb->online) && READ_ONCE(usb->running))
			mod_delayed_work(system_wq, &usb->pump, 0);
		break;
	}
}

static void tmc_qdss_close(struct tmc_usb_stream *usb)
{
	struct tmc_drvdata *tmc = usb->tmc;
	struct usb_qdss_ch *channel;
	unsigned long flags;

	raw_spin_lock_irqsave(&tmc->spinlock, flags);
	usb->selected = false;
	tmc_qdss_stop(tmc);
	raw_spin_unlock_irqrestore(&tmc->spinlock, flags);
	channel = xchg(&usb->channel, NULL);
	cancel_delayed_work_sync(&usb->pump);
	if (channel)
		usb_qdss_close(channel);
	WRITE_ONCE(usb->online, false);
	cancel_delayed_work_sync(&usb->pump);
}

static ssize_t out_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct tmc_drvdata *tmc = dev_get_drvdata(dev->parent);

	return sysfs_emit(buf, "%s\n", READ_ONCE(tmc->usb->selected) ? "usb" : "mem");
}

static ssize_t out_mode_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct tmc_drvdata *tmc = dev_get_drvdata(dev->parent);
	struct tmc_usb_stream *usb = tmc->usb;
	struct usb_qdss_ch *channel;
	unsigned long flags;

	guard(mutex)(&usb->mode_lock);
	if (sysfs_streq(buf, "mem")) {
		tmc_qdss_close(usb);
		return count;
	}
	if (!sysfs_streq(buf, "usb"))
		return -EINVAL;
	if (usb->selected)
		return count;
	channel = usb_qdss_open(USB_QDSS_CH_SW, usb, tmc_qdss_notify);
	if (IS_ERR_OR_NULL(channel))
		return channel ? PTR_ERR(channel) : -ENODEV;
	WRITE_ONCE(usb->channel, channel);
	raw_spin_lock_irqsave(&tmc->spinlock, flags);
	usb->selected = true;
	if (coresight_get_mode(tmc->csdev) == CS_MODE_SYSFS && !tmc->reading)
		tmc_qdss_start(tmc);
	raw_spin_unlock_irqrestore(&tmc->spinlock, flags);
	return count;
}
static DEVICE_ATTR_RW(out_mode);

static ssize_t block_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct tmc_drvdata *tmc = dev_get_drvdata(dev->parent);

	return sysfs_emit(buf, "%u\n", READ_ONCE(tmc->usb->block));
}

static ssize_t block_size_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct tmc_drvdata *tmc = dev_get_drvdata(dev->parent);
	unsigned long flags;
	u32 size;
	int ret;

	ret = kstrtou32(buf, 0, &size);
	if (ret || !IS_ALIGNED(size, 8) || (size && size < SZ_4K))
		return -EINVAL;
	raw_spin_lock_irqsave(&tmc->spinlock, flags);
	if (tmc->usb->running)
		ret = -EBUSY;
	else
		tmc->usb->block = size ?: QDSS_DEFAULT_BLOCK;
	raw_spin_unlock_irqrestore(&tmc->spinlock, flags);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(block_size);

static struct attribute *tmc_qdss_attrs[] = {
	&dev_attr_out_mode.attr, &dev_attr_block_size.attr, NULL
};
ATTRIBUTE_GROUPS(tmc_qdss);

int tmc_qdss_init(struct tmc_drvdata *tmc)
{
	struct device *dev = tmc->csdev->dev.parent;
	struct tmc_usb_stream *usb;
	int irq, ret;

	if (!device_property_read_bool(dev, "qcom,sw-usb"))
		return 0;
	if (!tmc->csr || !dev_is_dma_coherent(dev))
		return -EINVAL;
	irq = of_irq_get_byname(dev->of_node, "byte-cntr-irq");
	if (irq <= 0)
		return irq ?: -ENODEV;
	usb = devm_kzalloc(dev, sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return -ENOMEM;
	usb->tmc = tmc;
	usb->block = QDSS_DEFAULT_BLOCK;
	mutex_init(&usb->mode_lock);
	INIT_DELAYED_WORK(&usb->pump, tmc_qdss_pump);
	tmc->usb = usb;
	ret = devm_request_irq(dev, irq, tmc_qdss_irq, 0, dev_name(dev), usb);
	if (ret)
		return ret;
	return sysfs_create_groups(&tmc->csdev->dev.kobj, tmc_qdss_groups);
}

void tmc_qdss_remove(struct tmc_drvdata *tmc)
{
	if (!tmc->usb)
		return;
	sysfs_remove_groups(&tmc->csdev->dev.kobj, tmc_qdss_groups);
	guard(mutex)(&tmc->usb->mode_lock);
	tmc_qdss_close(tmc->usb);
}
