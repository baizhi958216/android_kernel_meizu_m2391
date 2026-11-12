// SPDX-License-Identifier: GPL-2.0-only
/* Export a live, coherent ETR ring to the Flyme qdss_sw IN endpoint. */
#include <linux/coresight.h>
#include <linux/dma-map-ops.h>
#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
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
	struct kref ref;
	struct miscdevice bytes;
	wait_queue_head_t wait;
	atomic64_t generation;
	struct etr_buf *final_buf;
	u64 final_pos;
	bool reader, removed, usb_capable;
	struct usb_qdss_ch *channel;
	struct delayed_work pump;
	atomic64_t interrupts;
	bool online, selected, running;
	u32 origin, step, block, session;
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
		atomic64_inc(&usb->generation);
		wake_up_interruptible(&usb->wait);
		if (READ_ONCE(usb->online))
			mod_delayed_work(system_wq, &usb->pump, 0);
	}
	return IRQ_HANDLED;
}

/* The ETR mode, buffer, and cursor are protected by drvdata->spinlock. */
void tmc_qdss_start(struct tmc_drvdata *tmc)
{
	struct tmc_usb_stream *usb = tmc->usb;
	long offset;

	if (!usb || (!usb->selected && !usb->reader) || !tmc->etr_buf || usb->running)
		return;
	offset = tmc_etr_live_offset(tmc);
	if (offset < 0)
		return;
	usb->final_buf = NULL;
	usb->origin = offset;
	usb->consumed = 0;
	usb->step = round_down(min_t(u32, usb->block ?: QDSS_DEFAULT_BLOCK, tmc->etr_buf->size / 4), 8);
	if (!usb->step)
		return;
	usb->session++;
	atomic64_set(&usb->interrupts, 0);
	WRITE_ONCE(usb->running, true);
	tmc_kalama_byte_count(tmc, usb->step);
	if (usb->online)
		mod_delayed_work(system_wq, &usb->pump, 0);
}

static s64 tmc_qdss_produced(struct tmc_usb_stream *usb)
{
	struct tmc_drvdata *tmc = usb->tmc;
	struct etr_buf *buf = tmc->etr_buf;
	u64 ticks, produced;
	unsigned long pos, size = buf->size;
	long rwp = tmc_etr_live_offset(tmc);

	if (rwp < 0)
		return rwp;
	ticks = atomic64_read(&usb->interrupts) * usb->step;
	pos = (rwp + size - usb->origin) % size;
	produced = ticks - ticks % size + pos;
	if (produced + size / 2 < ticks)
		produced += size;
	else if (produced >= size && produced > ticks + size / 2)
		produced -= size;
	return produced;
}

void tmc_qdss_stop(struct tmc_drvdata *tmc)
{
	struct tmc_usb_stream *usb = tmc->usb;
	s64 produced;

	if (!usb || !usb->running)
		return;
	tmc_kalama_byte_count(tmc, 0);
	WRITE_ONCE(usb->running, false);
	if (usb->reader && tmc->etr_buf) {
		produced = tmc_qdss_produced(usb);
		if (produced >= 0) {
			usb->final_buf = tmc->etr_buf;
			usb->final_pos = produced;
		}
	}
	atomic64_inc(&usb->generation);
	wake_up_interruptible(&usb->wait);
}

static ssize_t tmc_qdss_copy(struct tmc_usb_stream *usb, char *dst, size_t limit, u64 *next)
{
	struct tmc_drvdata *tmc = usb->tmc;
	struct etr_buf *buf;
	unsigned long size;
	u64 read;
	s64 produced;
	char *src;
	ssize_t n;

	if (usb->running && tmc->etr_buf) {
		buf = tmc->etr_buf;
		produced = tmc_qdss_produced(usb);
		if (produced < 0)
			return produced;
	} else if (usb->reader && usb->final_buf && tmc->sysfs_buf == usb->final_buf) {
		buf = tmc->sysfs_buf;
		produced = usb->final_pos;
	} else {
		return 0;
	}
	size = buf->size;
	read = usb->consumed;
	if (produced <= read)
		return 0;
	if (produced - read > size - usb->step)
		read = produced - (size - usb->step);
	n = tmc_etr_buf_get_data(buf, (usb->origin + read) % size,
				 min_t(u64, limit, produced - read), &src);
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
		u32 session;
		ssize_t n;

		if (!channel || !READ_ONCE(usb->online) || !READ_ONCE(usb->running))
			return;
		packet = kmalloc(sizeof(*packet), GFP_KERNEL);
		if (!packet)
			break;
		raw_spin_lock_irqsave(&tmc->spinlock, flags);
		n = tmc_qdss_copy(usb, packet->data, QDSS_PACKET, &next);
		previous = usb->consumed;
		session = usb->session;
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
			if (usb->session == session && usb->consumed == next)
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
		if (usb->selected)
			tmc_qdss_close(usb);
		return count;
	}
	if (!usb->usb_capable || !sysfs_streq(buf, "usb"))
		return -EINVAL;
	if (usb->reader)
		return -EBUSY;
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
		tmc->usb->block = size;
	raw_spin_unlock_irqrestore(&tmc->spinlock, flags);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(block_size);

static struct attribute *tmc_qdss_attrs[] = {
	&dev_attr_out_mode.attr, &dev_attr_block_size.attr, NULL
};
ATTRIBUTE_GROUPS(tmc_qdss);

static void tmc_bytes_free(struct kref *ref)
{
	kfree(container_of(ref, struct tmc_usb_stream, ref));
}

static void tmc_bytes_put(void *data)
{
	struct tmc_usb_stream *usb = data;

	kref_put(&usb->ref, tmc_bytes_free);
}

static int tmc_bytes_open(struct inode *inode, struct file *file)
{
	struct tmc_usb_stream *usb = container_of(file->private_data, struct tmc_usb_stream, bytes);
	struct tmc_drvdata *tmc;
	unsigned long flags;
	int ret = 0;

	guard(mutex)(&usb->mode_lock);
	if (usb->removed)
		return -ENODEV;
	if (usb->selected || usb->reader)
		return -EBUSY;
	tmc = usb->tmc;
	raw_spin_lock_irqsave(&tmc->spinlock, flags);
	if (coresight_get_mode(tmc->csdev) != CS_MODE_SYSFS || tmc->reading || !usb->block) {
		ret = -EINVAL;
	} else {
		usb->reader = true;
		tmc_qdss_start(tmc);
		if (!usb->running) {
			usb->reader = false;
			ret = -EIO;
		}
	}
	raw_spin_unlock_irqrestore(&tmc->spinlock, flags);
	if (ret)
		return ret;
	kref_get(&usb->ref);
	file->private_data = usb;
	return nonseekable_open(inode, file);
}

static ssize_t tmc_bytes_read(struct file *file, char __user *dst, size_t count, loff_t *pos)
{
	struct tmc_usb_stream *usb = file->private_data;
	struct tmc_drvdata *tmc;
	unsigned long flags;
	u64 next = 0, generation;
	u32 session;
	size_t limit = min_t(size_t, count, QDSS_PACKET);
	char *buffer;
	ssize_t ret;

	if (!count)
		return 0;
	buffer = kmalloc(limit, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	for (;;) {
		ret = mutex_lock_interruptible(&usb->mode_lock);
		if (ret)
			break;
		tmc = usb->tmc;
		if (usb->removed) {
			ret = -ENODEV;
			goto unlock;
		}
		raw_spin_lock_irqsave(&tmc->spinlock, flags);
		generation = atomic64_read(&usb->generation);
		session = usb->session;
		ret = tmc_qdss_copy(usb, buffer, limit, &next);
		raw_spin_unlock_irqrestore(&tmc->spinlock, flags);
		if (ret > 0) {
			if (copy_to_user(dst, buffer, ret)) {
				ret = -EFAULT;
			} else {
				raw_spin_lock_irqsave(&tmc->spinlock, flags);
				if (usb->session == session)
					usb->consumed = next;
				raw_spin_unlock_irqrestore(&tmc->spinlock, flags);
			}
			goto unlock;
		}
		if (ret || !READ_ONCE(usb->running))
			goto unlock;
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto unlock;
		}
		mutex_unlock(&usb->mode_lock);
		ret = wait_event_interruptible(usb->wait, READ_ONCE(usb->removed) ||
					       atomic64_read(&usb->generation) != generation);
		if (ret)
			break;
		continue;
unlock:
		mutex_unlock(&usb->mode_lock);
		break;
	}
	kfree(buffer);
	return ret;
}

static int tmc_bytes_release(struct inode *inode, struct file *file)
{
	struct tmc_usb_stream *usb = file->private_data;
	unsigned long flags;

	mutex_lock(&usb->mode_lock);
	if (!usb->removed) {
		raw_spin_lock_irqsave(&usb->tmc->spinlock, flags);
		usb->reader = false;
		tmc_qdss_stop(usb->tmc);
		usb->final_buf = NULL;
		raw_spin_unlock_irqrestore(&usb->tmc->spinlock, flags);
	}
	mutex_unlock(&usb->mode_lock);
	tmc_bytes_put(usb);
	return 0;
}

static const struct file_operations tmc_bytes_fops = {
	.owner = THIS_MODULE,
	.open = tmc_bytes_open,
	.read = tmc_bytes_read,
	.release = tmc_bytes_release,
};

int tmc_qdss_init(struct tmc_drvdata *tmc)
{
	struct device *dev = tmc->csdev->dev.parent;
	struct tmc_usb_stream *usb;
	int irq, ret;
	const char *name;

	if (device_property_read_string(dev, "byte-cntr-name", &name))
		return 0;
	if (!tmc->csr || !dev_is_dma_coherent(dev))
		return -EINVAL;
	irq = of_irq_get_byname(dev->of_node, "byte-cntr-irq");
	if (irq <= 0)
		return irq ?: -ENODEV;
	usb = kzalloc(sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return -ENOMEM;
	usb->tmc = tmc;
	usb->usb_capable = device_property_read_bool(dev, "qcom,sw-usb");
	kref_init(&usb->ref);
	ret = devm_add_action_or_reset(dev, tmc_bytes_put, usb);
	if (ret)
		return ret;
	init_waitqueue_head(&usb->wait);
	mutex_init(&usb->mode_lock);
	INIT_DELAYED_WORK(&usb->pump, tmc_qdss_pump);
	tmc->usb = usb;
	ret = devm_request_irq(dev, irq, tmc_qdss_irq, 0, dev_name(dev), usb);
	if (ret)
		return ret;
	ret = sysfs_create_groups(&tmc->csdev->dev.kobj, tmc_qdss_groups);
	if (ret)
		return ret;
	usb->bytes = (struct miscdevice) {
		.name = name, .parent = dev, .minor = MISC_DYNAMIC_MINOR, .fops = &tmc_bytes_fops,
	};
	ret = misc_register(&usb->bytes);
	if (ret)
		sysfs_remove_groups(&tmc->csdev->dev.kobj, tmc_qdss_groups);
	return ret;
}

void tmc_qdss_remove(struct tmc_drvdata *tmc)
{
	if (!tmc->usb)
		return;
	misc_deregister(&tmc->usb->bytes);
	sysfs_remove_groups(&tmc->csdev->dev.kobj, tmc_qdss_groups);
	guard(mutex)(&tmc->usb->mode_lock);
	WRITE_ONCE(tmc->usb->removed, true);
	tmc_qdss_close(tmc->usb);
	wake_up_interruptible(&tmc->usb->wait);
}
