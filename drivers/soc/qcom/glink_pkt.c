// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/ipc_logging.h>
#include <linux/refcount.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/skbuff.h>
#include <linux/rpmsg.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/idr.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/termios.h>
#include <linux/compat.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/rpmsg/qcom_glink.h>
#include <linux/string.h>

/* Define IPC Logging Macros */
#define GLINK_PKT_IPC_LOG_PAGE_CNT 32
static void *glink_pkt_ilctxt;

#define GLINK_PKT_INFO(x, ...)						     \
	ipc_log_string(glink_pkt_ilctxt, "[%s]: "x, __func__, ##__VA_ARGS__)

#define GLINK_PKT_ERR(x, ...)						     \
do {									     \
	printk_ratelimited("%s[%s]: " x, KERN_ERR, __func__, ##__VA_ARGS__); \
	ipc_log_string(glink_pkt_ilctxt, "%s[%s]: " x, "", __func__,         \
			##__VA_ARGS__);\
} while (0)

#define GLINK_PKT_IOCTL_MAGIC (0xC3)

#define GLINK_PKT_IOCTL_QUEUE_RX_INTENT \
	_IOW(GLINK_PKT_IOCTL_MAGIC, 0, unsigned int)

struct glink_pkt_zerocopy_receive {
	__u64 address;      /* in: address of mapping */
	__u32 length;       /* out: number of bytes to map/mapped */
	__u32 offset;   /* out: amount of bytes to skip */
};

#define GLINK_PKT_IOCTL_ZC_RECV \
	_IOWR(GLINK_PKT_IOCTL_MAGIC, 1, struct glink_pkt_zerocopy_receive)

#define GLINK_PKT_IOCTL_ZC_DONE \
	_IOWR(GLINK_PKT_IOCTL_MAGIC, 2, struct glink_pkt_zerocopy_receive)

#define MODULE_NAME "glink_pkt"
static dev_t glink_pkt_major;
static struct class *glink_pkt_class;
#define GLINK_PKT_MINORS 256

static DEFINE_IDA(glink_pkt_minor_ida);

/**
 * struct glink_pkt - driver context, relates rpdev to cdev
 * @dev:	glink pkt device
 * @cdev:	cdev for the glink pkt device
 * @lock:	synchronization of @rpdev and @open_tout modifications
 * @ch_open:	wait object for opening the glink channel
 * @refcount:	count how many userspace clients have handles
 * @rpdev:	underlaying rpmsg device
 * @rx_done:	cache whether rpdev can support external rx done
 * @queue_lock:	synchronization of @queue operations
 * @queue:	incoming message queue
 * @readq:	wait object for incoming queue
 * @sig_change:	flag to indicate serial signal change
 * @fragmented_read: set from dt node for partial read
 * @enable_ch_close: set from dt node for unregister driver on close syscall
 * @drv_lock:	lock to protect rpmsg driver variable
 * @drv:	rpmsg driver for registering to rpmsg bus
 * @drv_registered: status of rpmsg driver
 * @dev_name:	/dev/@dev_name for glink_pkt device
 * @ch_name:	glink channel to match to
 * @edge:	glink edge to match to
 * @open_tout:	timeout for open syscall, configurable in sysfs
 * @rskb_read_lock: Lock to protect rskb during read syscalls
 * @rskb:       current skb being read
 * @rdata:      data pointer in current skb
 * @rdata_len:  remaining data to be read from skb
 */
struct glink_pkt_device {
	struct device dev;
	struct cdev cdev;

	struct mutex lock;
	struct completion ch_open;
	refcount_t refcount;
	struct rpmsg_device *rpdev;
	bool removed;

	spinlock_t queue_lock;
	struct sk_buff_head queue;
	wait_queue_head_t readq;
	int sig_change;
	bool fragmented_read;
	bool enable_ch_close;

	struct mutex users_lock;
	struct mutex drv_lock;
	struct rpmsg_driver drv;
	bool drv_registered;

	const char *dev_name;
	const char *ch_name;
	const char *edge;
	int open_tout;

	struct mutex rskb_read_lock;
	struct sk_buff *rskb;
	unsigned char *rdata;
	size_t rdata_len;
};

#define dev_to_gpdev(_dev) container_of(_dev, struct glink_pkt_device, dev)
#define cdev_to_gpdev(_cdev) container_of(_cdev, struct glink_pkt_device, cdev)
#define drv_to_rpdrv(_drv) container_of(_drv, struct rpmsg_driver, drv)
#define rpdrv_to_gpdev(_rdrv) container_of(_rdrv, struct glink_pkt_device, drv)

static ssize_t open_timeout_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t n)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);
	long tmp;

	mutex_lock(&gpdev->lock);
	if (kstrtol(buf, 0, &tmp)) {
		mutex_unlock(&gpdev->lock);
		GLINK_PKT_ERR("unable to convert string to int for /dev/%s\n",
			      gpdev->dev_name);
		return -EINVAL;
	}
	gpdev->open_tout = tmp;
	mutex_unlock(&gpdev->lock);

	return n;
}

static ssize_t open_timeout_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);
	ssize_t ret;

	mutex_lock(&gpdev->lock);
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", gpdev->open_tout);
	mutex_unlock(&gpdev->lock);

	return ret;
}

static DEVICE_ATTR_RW(open_timeout);

static int glink_pkt_rpdev_probe(struct rpmsg_device *rpdev)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct glink_pkt_device *gpdev = rpdrv_to_gpdev(rpdrv);

	struct device_node *node = rpdev->dev.parent->of_node;

	/* Packet firmware names MPSS by its GLINK label and ADSP by its
	 * remoteproc label. Both names identify the same physical edge.
	 */
	if (of_property_match_string(node, "label", gpdev->edge) < 0 &&
	    of_property_match_string(node, "qcom,glink-label", gpdev->edge) < 0)
		return -ENODEV;

	mutex_lock(&gpdev->lock);
	gpdev->rpdev = rpdev;
	mutex_unlock(&gpdev->lock);

	dev_set_drvdata(&rpdev->dev, gpdev);
	complete_all(&gpdev->ch_open);

	return 0;
}

static void glink_pkt_clear_queues(struct glink_pkt_device *gpdev)
{
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	if (gpdev->rskb) {
		kfree_skb(gpdev->rskb);
		gpdev->rskb = NULL;
		gpdev->rdata = NULL;
		gpdev->rdata_len = 0;
	}

	while ((skb = skb_dequeue(&gpdev->queue)))
		kfree_skb(skb);

	spin_unlock_irqrestore(&gpdev->queue_lock, flags);
}

static int glink_pkt_rpdev_cb(struct rpmsg_device *rpdev, void *buf,
				   int len, void *priv, u32 addr)
{
	struct glink_pkt_device *gpdev = dev_get_drvdata(&rpdev->dev);
	unsigned long flags;
	struct sk_buff *skb;

	if (!gpdev) {
		GLINK_PKT_ERR("channel is in reset\n");
		return -ENETRESET;
	}

	GLINK_PKT_INFO("Data received on:%s len:%d\n", gpdev->ch_name, len);
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		GLINK_PKT_ERR("skb failed for channel:%s len:%d\n", gpdev->ch_name, len);
		return -ENOMEM;
	}

	skb_put_data(skb, buf, len);

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	skb_queue_tail(&gpdev->queue, skb);
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	/* wake up any blocking processes, waiting for new data */
	wake_up_interruptible(&gpdev->readq);

	GLINK_PKT_INFO("Data queued on:%s len:%d\n", gpdev->ch_name, len);

	return 0;
}

static int glink_pkt_rpdev_sigs(struct rpmsg_device *rpdev, void *priv, bool pause)
{
	struct glink_pkt_device *gpdev = dev_get_drvdata(&rpdev->dev);
	unsigned long flags;

	if (!gpdev)
		return -ENETRESET;
	spin_lock_irqsave(&gpdev->queue_lock, flags);
	gpdev->sig_change = true;
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);
	wake_up_interruptible(&gpdev->readq);
	return 0;
}

static void glink_pkt_rpdev_remove(struct rpmsg_device *rpdev)
{
	struct device_driver *drv = rpdev->dev.driver;
	struct rpmsg_driver *rpdrv = drv_to_rpdrv(drv);
	struct glink_pkt_device *gpdev = rpdrv_to_gpdev(rpdrv);

	mutex_lock(&gpdev->lock);
	glink_pkt_clear_queues(gpdev);
	gpdev->rpdev = NULL;
	mutex_unlock(&gpdev->lock);

	dev_set_drvdata(&rpdev->dev, NULL);

	/* wake up any blocked readers */
	reinit_completion(&gpdev->ch_open);
	wake_up_interruptible(&gpdev->readq);
}

static int glink_pkt_drv_try_register(struct glink_pkt_device *gpdev)
{
	int ret = 0;

	mutex_lock(&gpdev->drv_lock);
	if (gpdev->removed) {
		ret = -ENODEV;
	} else if (!gpdev->drv_registered) {
		ret = register_rpmsg_driver(&gpdev->drv);
		if (!ret)
			gpdev->drv_registered = true;
	}
	mutex_unlock(&gpdev->drv_lock);

	return ret;
}

static void glink_pkt_drv_try_unregister(struct glink_pkt_device *gpdev)
{
	mutex_lock(&gpdev->drv_lock);
	if (gpdev->drv_registered) {
		unregister_rpmsg_driver(&gpdev->drv);
		gpdev->drv_registered = false;
	}
	mutex_unlock(&gpdev->drv_lock);
}

/**
 * glink_pkt_open() - open() syscall for the glink_pkt device
 * inode:	Pointer to the inode structure.
 * file:	Pointer to the file structure.
 *
 * This function is used to open the glink pkt device when
 * userspace client do a open() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static void glink_pkt_put_user(struct glink_pkt_device *gpdev)
{
	mutex_lock(&gpdev->users_lock);
	refcount_dec(&gpdev->refcount);
	if (refcount_read(&gpdev->refcount) == 1) {
		if (gpdev->enable_ch_close)
			glink_pkt_drv_try_unregister(gpdev);
		mutex_lock(&gpdev->lock);
		glink_pkt_clear_queues(gpdev);
		WRITE_ONCE(gpdev->sig_change, false);
		mutex_unlock(&gpdev->lock);
	}
	mutex_unlock(&gpdev->users_lock);
	put_device(&gpdev->dev);
}

static int glink_pkt_open(struct inode *inode, struct file *file)
{
	struct glink_pkt_device *gpdev = cdev_to_gpdev(inode->i_cdev);
	unsigned long tout = msecs_to_jiffies(READ_ONCE(gpdev->open_tout) * 1000U);
	int ret = 0;

	get_device(&gpdev->dev);
	mutex_lock(&gpdev->users_lock);
	refcount_inc(&gpdev->refcount);
	if (gpdev->enable_ch_close)
		ret = glink_pkt_drv_try_register(gpdev);
	mutex_unlock(&gpdev->users_lock);
	if (ret)
		goto failed;
	ret = wait_for_completion_interruptible_timeout(&gpdev->ch_open, tout);
	if (READ_ONCE(gpdev->removed))
		ret = -ENODEV;
	else if (!ret)
		ret = -ETIMEDOUT;
	if (ret < 0)
		goto failed;
	file->private_data = gpdev;
	return 0;
failed:
	glink_pkt_put_user(gpdev);
	return ret;
}

static int glink_pkt_release(struct inode *inode, struct file *file)
{
	glink_pkt_put_user(file->private_data);
	return 0;
}

/**
 * glink_pkt_read() - read() syscall for the glink_pkt device
 * file:	Pointer to the file structure.
 * buf:		Pointer to the userspace buffer.
 * count:	Number bytes to read from the file.
 * ppos:	Pointer to the position into the file.
 *
 * This function is used to Read the data from glink pkt device when
 * userspace client do a read() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static ssize_t glink_pkt_read(struct file *file,
			char __user *buf, size_t count, loff_t *ppos)
{
	struct glink_pkt_device *gpdev = file->private_data;
	struct sk_buff *skb = NULL;
	int ret = 0;
	int use;

	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR("invalid device handle\n");
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&gpdev->lock))
		return -ERESTARTSYS;

	if (READ_ONCE(gpdev->removed) || !completion_done(&gpdev->ch_open)) {
		GLINK_PKT_ERR("%s channel in reset\n", gpdev->ch_name);
		mutex_unlock(&gpdev->lock);
		return -ENETRESET;
	}

	GLINK_PKT_INFO("begin for %s by %s:%d ref_cnt[%d], remaining[%zu], count[%zu]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount),
			   gpdev->rdata_len, count);

	mutex_unlock(&gpdev->lock);

	/* Wait for data in the queue */
	spin_lock_irq(&gpdev->queue_lock);
	if (skb_queue_empty(&gpdev->queue) && !gpdev->rskb) {
		if (file->f_flags & O_NONBLOCK) {
			spin_unlock_irq(&gpdev->queue_lock);
			return -EAGAIN;
		}

		/* Wait until we get data or the endpoint goes away */
		ret = wait_event_interruptible_lock_irq(gpdev->readq,
							!skb_queue_empty(&gpdev->queue) ||
							(READ_ONCE(gpdev->removed) || !completion_done(&gpdev->ch_open)),
							gpdev->queue_lock);
	}
	spin_unlock_irq(&gpdev->queue_lock);

	if (ret)
		return -ERESTARTSYS;
	if (READ_ONCE(gpdev->removed) || !completion_done(&gpdev->ch_open))
		return -ENETRESET;

	if (mutex_lock_interruptible(&gpdev->lock))
		return -ERESTARTSYS;

	mutex_lock(&gpdev->rskb_read_lock);
	spin_lock_irq(&gpdev->queue_lock);
	if (!gpdev->rskb) {
		gpdev->rskb = skb_dequeue(&gpdev->queue);
		if (!gpdev->rskb) {
			spin_unlock_irq(&gpdev->queue_lock);
			mutex_unlock(&gpdev->rskb_read_lock);
			mutex_unlock(&gpdev->lock);
			return 0;
		}
		gpdev->rdata = gpdev->rskb->data;
		gpdev->rdata_len = gpdev->rskb->len;
	}
	spin_unlock_irq(&gpdev->queue_lock);

	use = min_t(size_t, count, gpdev->rdata_len);

	if (copy_to_user(buf, gpdev->rdata, use)) {
		mutex_unlock(&gpdev->rskb_read_lock);
		mutex_unlock(&gpdev->lock);
		return -EFAULT;
	}

	spin_lock_irq(&gpdev->queue_lock);
	gpdev->rdata += use;
	gpdev->rdata_len -= use;

	if (!gpdev->fragmented_read || !gpdev->rdata_len) {
		skb = gpdev->rskb;

		gpdev->rskb = NULL;
		gpdev->rdata = NULL;
		gpdev->rdata_len = 0;
	}
	spin_unlock_irq(&gpdev->queue_lock);

	if (skb)
		kfree_skb(skb);
	mutex_unlock(&gpdev->rskb_read_lock);

	ret = (ret < 0) ? ret : use;
	GLINK_PKT_INFO("end for %s by %s:%d ret[%d], remaining[%zu]\n", gpdev->ch_name,
		       current->comm, task_pid_nr(current), ret, gpdev->rdata_len);

	mutex_unlock(&gpdev->lock);
	return ret;
}

/**
 * glink_pkt_write() - write() syscall for the glink_pkt device
 * file:	Pointer to the file structure.
 * buf:		Pointer to the userspace buffer.
 * count:	Number bytes to read from the file.
 * ppos:	Pointer to the position into the file.
 *
 * This function is used to write the data to glink pkt device when
 * userspace client do a write() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static ssize_t glink_pkt_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	struct glink_pkt_device *gpdev = file->private_data;
	void *kbuf;
	int ret;

	gpdev = file->private_data;
	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR("invalid device handle\n");
		return -EINVAL;
	}

	GLINK_PKT_INFO("begin to %s buffer_size %zu\n", gpdev->ch_name, count);
	if (count > INT_MAX)
		return -EMSGSIZE;
	kbuf = vmemdup_user(buf, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (mutex_lock_interruptible(&gpdev->lock)) {
		ret = -ERESTARTSYS;
		goto free_kbuf;
	}
	if (READ_ONCE(gpdev->removed) || !gpdev->rpdev) {
		GLINK_PKT_ERR("%s channel in reset\n", gpdev->ch_name);
		ret = -ENETRESET;
		goto unlock_ch;
	}

	if (file->f_flags & O_NONBLOCK)
		ret = rpmsg_trysend(gpdev->rpdev->ept, kbuf, count);
	else
		ret = rpmsg_send(gpdev->rpdev->ept, kbuf, count);

unlock_ch:
	mutex_unlock(&gpdev->lock);

free_kbuf:
	kvfree(kbuf);
	GLINK_PKT_INFO("finish to %s ret %d\n", gpdev->ch_name, ret);
	return ret < 0 ? ret : count;
}

/**
 * glink_pkt_poll() - poll() syscall for the glink_pkt device
 * file:	Pointer to the file structure.
 * wait:	pointer to Poll table.
 *
 * This function is used to poll on the glink pkt device when
 * userspace client do a poll() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static __poll_t glink_pkt_poll(struct file *file, poll_table *wait)
{
	struct glink_pkt_device *gpdev = file->private_data;
	__poll_t mask = 0;
	unsigned long flags;

	gpdev = file->private_data;
	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR("invalid device handle\n");
		return POLLERR;
	}
	if (READ_ONCE(gpdev->removed) || !completion_done(&gpdev->ch_open)) {
		GLINK_PKT_ERR("%s channel in reset\n", gpdev->ch_name);
		return POLLHUP | POLLPRI;
	}

	GLINK_PKT_INFO("Wait for pkt on channel:%s\n", gpdev->ch_name);
	poll_wait(file, &gpdev->readq, wait);

	mutex_lock(&gpdev->lock);

	if (READ_ONCE(gpdev->removed) || !gpdev->rpdev) {
		GLINK_PKT_ERR("%s channel reset after wait\n", gpdev->ch_name);
		mutex_unlock(&gpdev->lock);
		return POLLHUP;
	}

	spin_lock_irqsave(&gpdev->queue_lock, flags);
	if (!skb_queue_empty(&gpdev->queue) || gpdev->rskb)
		mask |= POLLIN | POLLRDNORM;

	if (gpdev->sig_change)
		mask |= POLLPRI;
	spin_unlock_irqrestore(&gpdev->queue_lock, flags);

	mask |= rpmsg_poll(gpdev->rpdev->ept, file, wait);

	mutex_unlock(&gpdev->lock);

	GLINK_PKT_INFO("Exit channel:%s\n", gpdev->ch_name);

	return mask;
}

/**
 * glink_pkt_tiocmset() - set the signals for glink_pkt device
 * devp:	Pointer to the glink_pkt device structure.
 * cmd:		IOCTL command.
 * arg:		Arguments to the ioctl call.
 *
 * This function is used to set the signals on the glink pkt device
 * when userspace client do a ioctl() system call with TIOCMBIS,
 * TIOCMBIC and TICOMSET.
 */
static int glink_pkt_tiocmset(struct glink_pkt_device *gpdev, unsigned int cmd,
			      int __user *arg)
{
	u32 set, clear, val;
	int ret;

	ret = get_user(val, arg);
	if (ret)
		return ret;
	set = clear = 0;
	switch (cmd) {
	case TIOCMBIS:
		set = val;
		break;
	case TIOCMBIC:
		clear = val;
		break;
	case TIOCMSET:
		set = val;
		clear = ~val;
		break;
	}

	set &= TIOCM_DTR | TIOCM_RTS | TIOCM_CD | TIOCM_RI;
	clear &= TIOCM_DTR | TIOCM_RTS | TIOCM_CD | TIOCM_RI;
	GLINK_PKT_INFO("set[0x%x] clear[0x%x]\n", set, clear);

	return qcom_glink_update_signals(gpdev->rpdev->ept, set, clear);
}

/*
 * The native transport returns intents immediately from its RX callback.
 * Give each userspace mapping its own read-only pages, also for packets whose
 * two end pages share a modem intent with unrelated data.
 */
struct glink_pkt_slot {
	struct list_head list;
	pgoff_t first;
	unsigned int pages;
};

struct glink_pkt_mapping {
	struct kref refs;
	struct mutex lock;
	struct xarray pages;
	struct list_head slots;
};

static void glink_pkt_mapping_release(struct kref *ref)
{
	struct glink_pkt_mapping *map = container_of(ref, struct glink_pkt_mapping, refs);
	struct glink_pkt_slot *slot, *next;
	struct page *page;
	unsigned long index;

	list_for_each_entry_safe(slot, next, &map->slots, list)
		kfree(slot);
	xa_for_each(&map->pages, index, page)
		put_page(page);
	xa_destroy(&map->pages);
	kfree(map);
}

static void glink_pkt_vma_open(struct vm_area_struct *vma)
{
	struct glink_pkt_mapping *map = vma->vm_private_data;

	kref_get(&map->refs);
}

static void glink_pkt_vma_close(struct vm_area_struct *vma)
{
	struct glink_pkt_mapping *map = vma->vm_private_data;

	kref_put(&map->refs, glink_pkt_mapping_release);
}

static vm_fault_t glink_pkt_fault(struct vm_fault *vmf)
{
	struct glink_pkt_mapping *map = vmf->vma->vm_private_data;
	struct page *page = xa_load(&map->pages, vmf->pgoff);

	if (!page)
		return VM_FAULT_SIGBUS;
	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct glink_pkt_vm_ops = {
	.open = glink_pkt_vma_open,
	.close = glink_pkt_vma_close,
	.fault = glink_pkt_fault,
};

static int glink_pkt_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct glink_pkt_mapping *map;

	if (vma->vm_flags & (VM_WRITE | VM_EXEC) || vma->vm_pgoff)
		return -EPERM;
	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;
	kref_init(&map->refs);
	mutex_init(&map->lock);
	xa_init(&map->pages);
	INIT_LIST_HEAD(&map->slots);
	vm_flags_clear(vma, VM_MAYWRITE | VM_MAYEXEC);
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTCOPY | VM_DONTDUMP);
	vma->vm_ops = &glink_pkt_vm_ops;
	vma->vm_private_data = map;
	return 0;
}

static int glink_pkt_map_packet(struct file *file, unsigned int cmd,
				struct glink_pkt_zerocopy_receive *zc)
{
	struct glink_pkt_device *gpdev = file->private_data;
	struct vm_area_struct *vma;
	struct glink_pkt_mapping *map;
	struct glink_pkt_slot *slot;
	struct sk_buff *skb;
	unsigned long addr = zc->address;
	pgoff_t first;
	unsigned int i, n;
	int ret = 0;

	if (addr != zc->address || !PAGE_ALIGNED(addr))
		return -EINVAL;

	mmap_read_lock(current->mm);
	vma = vma_lookup(current->mm, addr);
	if (!vma || vma->vm_file != file || vma->vm_ops != &glink_pkt_vm_ops) {
		ret = -EINVAL;
		goto unlock_mm;
	}
	map = vma->vm_private_data;
	first = linear_page_index(vma, addr);
	mutex_lock(&map->lock);
	if (cmd == GLINK_PKT_IOCTL_ZC_DONE) {
		ret = -EINVAL;
		list_for_each_entry(slot, &map->slots, list) {
			if (slot->first != first)
				continue;
			for (i = 0; i < slot->pages; i++)
				clear_highpage(xa_load(&map->pages, first + i));
			list_del(&slot->list);
			kfree(slot);
			ret = 0;
			break;
		}
		goto unlock;
	}
	skb = skb_dequeue(&gpdev->queue);
	if (!skb) {
		ret = -EAGAIN;
		goto unlock;
	}
	n = DIV_ROUND_UP(skb->len, PAGE_SIZE);
	if (n > (vma->vm_end - addr) >> PAGE_SHIFT) {
		ret = -EMSGSIZE;
		goto requeue;
	}
	list_for_each_entry(slot, &map->slots, list) {
		if (first < slot->first + slot->pages && slot->first < first + n) {
			ret = -EBUSY;
			goto requeue;
		}
	}
	slot = kmalloc(sizeof(*slot), GFP_KERNEL_ACCOUNT);
	if (!slot) {
		ret = -ENOMEM;
		goto requeue;
	}
	for (i = 0; i < n; i++) {
		struct page *page = xa_load(&map->pages, first + i);

		if (!page) {
			page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
			if (!page) {
				ret = -ENOMEM;
				break;
			}
			ret = xa_err(xa_store(&map->pages, first + i, page, GFP_KERNEL));
			if (ret) {
				put_page(page);
				break;
			}
		}
		memcpy_to_page(page, 0, skb->data + i * PAGE_SIZE,
			       min_t(size_t, PAGE_SIZE, skb->len - i * PAGE_SIZE));
	}
	if (ret) {
		while (i)
			clear_highpage(xa_load(&map->pages, first + --i));
		kfree(slot);
		goto requeue;
	}
	slot->first = first;
	slot->pages = n;
	list_add_tail(&slot->list, &map->slots);
	zc->offset = 0;
	zc->length = skb->len;
	kfree_skb(skb);
	goto unlock;

requeue:
	skb_queue_head(&gpdev->queue, skb);
unlock:
	mutex_unlock(&map->lock);
unlock_mm:
	mmap_read_unlock(current->mm);
	return ret;
}

/**
 * glink_pkt_ioctl() - ioctl() syscall for the glink_pkt device
 * file:	Pointer to the file structure.
 * cmd:		IOCTL command.
 * arg:		Arguments to the ioctl call.
 *
 * This function is used to ioctl on the glink pkt device when
 * userspace client do a ioctl() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static long glink_pkt_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct glink_pkt_zerocopy_receive zc;
	struct glink_pkt_device *gpdev;
	unsigned long flags;
	int ret;

	gpdev = file->private_data;
	if (!gpdev || refcount_read(&gpdev->refcount) == 1) {
		GLINK_PKT_ERR("invalid device handle\n");
		return -EINVAL;
	}
	if (mutex_lock_interruptible(&gpdev->lock))
		return -ERESTARTSYS;

	if (READ_ONCE(gpdev->removed) || !gpdev->rpdev) {
		GLINK_PKT_ERR("%s channel in reset\n", gpdev->ch_name);
		mutex_unlock(&gpdev->lock);
		return -ENETRESET;
	}

	switch (cmd) {
	case TIOCMGET:
		spin_lock_irqsave(&gpdev->queue_lock, flags);
		gpdev->sig_change = false;
		spin_unlock_irqrestore(&gpdev->queue_lock, flags);

		ret = qcom_glink_get_signals(gpdev->rpdev->ept);
		if (ret >= 0)
			ret = put_user(ret, (int __user *)arg);
		break;
	case TIOCMSET:
	case TIOCMBIS:
	case TIOCMBIC:
		ret = glink_pkt_tiocmset(gpdev, cmd, (int __user *)arg);
		break;
	case GLINK_PKT_IOCTL_QUEUE_RX_INTENT:
		/* Return success to not break userspace client logic */
		ret = 0;
		break;
	case GLINK_PKT_IOCTL_ZC_RECV:
	case GLINK_PKT_IOCTL_ZC_DONE:
		if (copy_from_user(&zc, (void __user *)arg, sizeof(zc))) {
			ret = -EFAULT;
			break;
		}
		ret = glink_pkt_map_packet(file, cmd, &zc);
		if (!ret && cmd == GLINK_PKT_IOCTL_ZC_RECV &&
		    copy_to_user((void __user *)arg, &zc, sizeof(zc))) {
			glink_pkt_map_packet(file, GLINK_PKT_IOCTL_ZC_DONE, &zc);
			ret = -EFAULT;
		}
		break;
	default:
		GLINK_PKT_ERR("unrecognized ioctl command 0x%x\n", cmd);
		ret = -ENOIOCTLCMD;
	}

	mutex_unlock(&gpdev->lock);

	return ret;
}

static const struct file_operations glink_pkt_fops = {
	.owner = THIS_MODULE,
	.open = glink_pkt_open,
	.release = glink_pkt_release,
	.read = glink_pkt_read,
	.write = glink_pkt_write,
	.poll = glink_pkt_poll,
	.unlocked_ioctl = glink_pkt_ioctl,
	.mmap = glink_pkt_mmap,
	.compat_ioctl = compat_ptr_ioctl,
};

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);

	return scnprintf(buf, RPMSG_NAME_SIZE, "%s\n", gpdev->ch_name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *glink_pkt_device_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_open_timeout.attr,
	NULL,
};
ATTRIBUTE_GROUPS(glink_pkt_device);

/**
 * parse_glinkpkt_devicetree() - parse device tree binding for a subnode
 *
 * np:		pointer to a device tree node
 * gpdev:	pointer to GLINK PACKET device
 *
 * Return:	0 on success, standard Linux error codes on error.
 */
static int glink_pkt_parse_devicetree(struct device_node *np,
				      struct glink_pkt_device *gpdev)
{
	char *key;
	int ret;

	key = "qcom,glinkpkt-edge";
	ret = of_property_read_string(np, key, &gpdev->edge);
	if (ret < 0)
		goto error;

	key = "qcom,glinkpkt-ch-name";
	ret = of_property_read_string(np, key, &gpdev->ch_name);
	if (ret < 0)
		goto error;

	key = "qcom,glinkpkt-dev-name";
	ret = of_property_read_string(np, key, &gpdev->dev_name);
	if (ret < 0)
		goto error;

	key = "qcom,glinkpkt-enable-ch-close";
	gpdev->enable_ch_close = of_property_read_bool(np, key);

	key = "qcom,glinkpkt-fragmented-read";
	gpdev->fragmented_read = of_property_read_bool(np, key);

	GLINK_PKT_INFO(
	"Parsed %s:%s /dev/%s enable channel close:%d fragmented-read:%d\n",
		      gpdev->edge, gpdev->ch_name, gpdev->dev_name,
		      gpdev->enable_ch_close, gpdev->fragmented_read);

	return 0;

error:
	GLINK_PKT_ERR("%s: missing key: %s\n", __func__, key);
	return ret;
}

static void glink_pkt_release_device(struct device *dev)
{
	struct glink_pkt_device *gpdev = dev_to_gpdev(dev);

	GLINK_PKT_INFO("for %s by %s:%d ref_cnt[%d]\n",
		       gpdev->ch_name, current->comm,
		       task_pid_nr(current), refcount_read(&gpdev->refcount));

	ida_free(&glink_pkt_minor_ida, MINOR(gpdev->dev.devt));
	kfree(gpdev);
}

static int glink_pkt_init_rpmsg(struct glink_pkt_device *gpdev)
{
	struct rpmsg_driver *rpdrv = &gpdev->drv;
	struct device *dev = &gpdev->dev;
	struct rpmsg_device_id *match;
	char *drv_name;

	match = devm_kzalloc(dev, sizeof(*match) * 2, GFP_KERNEL);
	if (!match)
		return -ENOMEM;
	strscpy(match->name, gpdev->ch_name, RPMSG_NAME_SIZE);

	drv_name = devm_kasprintf(dev, GFP_KERNEL,
				   "%s_%s", "glink_pkt", gpdev->dev_name);
	if (!drv_name)
		return -ENOMEM;

	rpdrv->probe = glink_pkt_rpdev_probe;
	rpdrv->remove = glink_pkt_rpdev_remove;
	rpdrv->callback = glink_pkt_rpdev_cb;
	rpdrv->flowcontrol = glink_pkt_rpdev_sigs;
	rpdrv->id_table = match;
	rpdrv->drv.name = drv_name;

	return gpdev->enable_ch_close ? 0 : glink_pkt_drv_try_register(gpdev);
}

/**
 * glink_pkt_add_device() - Create glink packet device and add cdev
 * parent:	pointer to the parent device of this glink packet device
 * np:		pointer to device node this glink packet device represents
 *
 * return:	0 for success, Standard Linux errors
 */
static void glink_pkt_remove_port(void *data)
{
	struct glink_pkt_device *gpdev = data;

	cdev_device_del(&gpdev->cdev, &gpdev->dev);
	mutex_lock(&gpdev->drv_lock);
	gpdev->removed = true;
	mutex_unlock(&gpdev->drv_lock);
	glink_pkt_drv_try_unregister(gpdev);
	complete_all(&gpdev->ch_open);
	wake_up_interruptible(&gpdev->readq);
	put_device(&gpdev->dev);
}

static int glink_pkt_create_device(struct device *parent,
				   struct device_node *np)
{
	struct glink_pkt_device *gpdev;
	struct device *dev;
	int ret, minor;

	gpdev = kzalloc(sizeof(*gpdev), GFP_KERNEL);
	if (!gpdev)
		return -ENOMEM;

	minor = ida_alloc_max(&glink_pkt_minor_ida, GLINK_PKT_MINORS - 1, GFP_KERNEL);
	if (minor < 0) {
		kfree(gpdev);
		return minor;
	}

	dev = &gpdev->dev;

	ret = glink_pkt_parse_devicetree(np, gpdev);
	if (ret < 0) {
		GLINK_PKT_ERR("failed to parse dt ret:%d\n", ret);
		ida_free(&glink_pkt_minor_ida, minor);
		kfree(gpdev);
		return ret;
	}

	mutex_init(&gpdev->lock);
	mutex_init(&gpdev->users_lock);
	mutex_init(&gpdev->drv_lock);
	mutex_init(&gpdev->rskb_read_lock);
	refcount_set(&gpdev->refcount, 1);
	init_completion(&gpdev->ch_open);

	/* Default open timeout for open is 120 sec */
	gpdev->open_tout = 120;
	gpdev->sig_change = false;

	spin_lock_init(&gpdev->queue_lock);

	gpdev->rskb = NULL;
	gpdev->rdata = NULL;
	gpdev->rdata_len = 0;

	skb_queue_head_init(&gpdev->queue);
	init_waitqueue_head(&gpdev->readq);

	device_initialize(dev);
	dev->release = glink_pkt_release_device;
	dev->class = glink_pkt_class;
	dev->parent = parent;
	dev->groups = glink_pkt_device_groups;
	dev_set_drvdata(dev, gpdev);

	cdev_init(&gpdev->cdev, &glink_pkt_fops);
	gpdev->cdev.owner = THIS_MODULE;

	dev->devt = MKDEV(MAJOR(glink_pkt_major), minor);
	ret = dev_set_name(dev, "%s", gpdev->dev_name);
	if (ret)
		goto free_dev;

	ret = glink_pkt_init_rpmsg(gpdev);
	if (ret)
		goto free_dev;
	ret = cdev_device_add(&gpdev->cdev, dev);
	if (ret) {
		glink_pkt_drv_try_unregister(gpdev);
		goto free_dev;
	}
	return devm_add_action_or_reset(parent, glink_pkt_remove_port, gpdev);
free_dev:
	put_device(dev);
	return ret;
}

static int glink_pkt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	for_each_available_child_of_node_scoped(dev->of_node, child) {
		int ret = glink_pkt_create_device(dev, child);

		if (ret)
			return dev_err_probe(dev, ret, "failed to create %pOFn packet port\n", child);
	}
	return 0;
}

static const struct of_device_id glink_pkt_match_table[] = {
	{ .compatible = "qcom,glinkpkt" },
	{},
};

MODULE_DEVICE_TABLE(of, glink_pkt_match_table);

static struct platform_driver glink_pkt_driver = {
	.probe = glink_pkt_probe,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = glink_pkt_match_table,
	 },
};

/**
 * glink_pkt_init() - Initialization function for this module
 *
 * returns:	0 on success, standard Linux error code otherwise.
 */
static int __init glink_pkt_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&glink_pkt_major, 0, GLINK_PKT_MINORS, "glinkpkt");
	if (ret)
		return ret;
	glink_pkt_class = class_create("glinkpkt");
	if (IS_ERR(glink_pkt_class)) {
		ret = PTR_ERR(glink_pkt_class);
		goto unregister_devt;
	}
	glink_pkt_ilctxt = ipc_log_context_create(GLINK_PKT_IPC_LOG_PAGE_CNT, "glink_pkt", 0);
	ret = platform_driver_register(&glink_pkt_driver);
	if (!ret)
		return 0;
	ipc_log_context_destroy(glink_pkt_ilctxt);
	class_destroy(glink_pkt_class);
unregister_devt:
	unregister_chrdev_region(glink_pkt_major, GLINK_PKT_MINORS);
	return ret;
}

static void __exit glink_pkt_exit(void)
{
	platform_driver_unregister(&glink_pkt_driver);
	ipc_log_context_destroy(glink_pkt_ilctxt);
	class_destroy(glink_pkt_class);
	unregister_chrdev_region(glink_pkt_major, GLINK_PKT_MINORS);
}

module_init(glink_pkt_init);
module_exit(glink_pkt_exit);

MODULE_DESCRIPTION("MSM G-Link Packet Port");
MODULE_LICENSE("GPL v2");
