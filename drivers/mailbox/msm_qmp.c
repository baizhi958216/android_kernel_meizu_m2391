// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kalama TME QMP over SMEM item 629 and an IPCC doorbell.
 * Firmware publishes both mailbox windows relative to the QMP descriptor.
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox/qmp.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smem.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>

#define QMP_MAGIC		0x4d41494c
#define QMP_VERSION		1
#define QMP_UP			0x0000ffff
#define QMP_DOWN		0xffff0000
#define QMP_SMEM_ITEM		629

struct qmp_channel_desc {
	u32 link_state;
	u32 link_ack;
	u32 channel_state;
	u32 channel_ack;
	u32 size;
	u32 offset;
};

struct qmp_descriptor {
	u32 magic;
	u32 version;
	u32 features;
	struct qmp_channel_desc remote;
	struct qmp_channel_desc local;
	u32 reserved;
};

struct qmp_window {
	void __iomem *addr;
	size_t size;
};

struct qmp_edge {
	struct mbox_controller controller;
	struct mbox_chan chan;
	struct mbox_client doorbell_client;
	struct mbox_chan *doorbell;
	struct qmp_descriptor __iomem *desc;
	size_t region_size;
	struct qmp_window tx;
	struct qmp_window rx;
	void *rx_buf;
	spinlock_t lock;
	wait_queue_head_t event;
	int irq;
	bool negotiated;
	bool requested;
	bool connected;
};

static void qmp_ring(struct qmp_edge *edge)
{
	int ret;

	wmb();
	ret = mbox_send_message(edge->doorbell, NULL);
	if (ret >= 0)
		mbox_client_txdone(edge->doorbell, 0);
	else
		dev_err_ratelimited(edge->controller.dev, "IPCC doorbell failed (%d)\n", ret);
}

static bool qmp_read_window(struct qmp_edge *edge, struct qmp_channel_desc __iomem *desc,
			    struct qmp_window *window)
{
	u32 offset = readl(&desc->offset);
	u32 size = readl(&desc->size);

	if (!IS_ALIGNED(offset, 4) || !IS_ALIGNED(size, 4) ||
	    offset < sizeof(*edge->desc) || size < 2 * sizeof(u32) ||
	    offset > edge->region_size || size > edge->region_size - offset)
		return false;

	window->addr = (void __iomem *)edge->desc + offset;
	window->size = size;
	return true;
}

/* Mirrors the states in the downstream QMP v1 descriptor. Device IRQs may
 * arrive before a client registers and transitions can share a doorbell.
 */
static irqreturn_t qmp_edge_irq(int irq, void *data)
{
	struct qmp_edge *edge = data;
	struct qmp_descriptor __iomem *d = edge->desc;
	unsigned long flags;
	struct qmp_pkt pkt = {};
	bool ring = false;
	u32 local, remote, len;

	if (readl(&d->magic) != QMP_MAGIC || readl(&d->version) != QMP_VERSION)
		return IRQ_HANDLED;

	spin_lock_irqsave(&edge->lock, flags);
	remote = readl(&d->remote.link_state);
	if (readl(&d->remote.link_ack) != remote) {
		writel(remote, &d->remote.link_ack);
		ring = true;
	}
	if (remote != QMP_UP) {
		edge->connected = false;
		edge->negotiated = false;
		goto out;
	}
	if (!edge->negotiated) {
		if (!qmp_read_window(edge, &d->local, &edge->tx) ||
		    !qmp_read_window(edge, &d->remote, &edge->rx) ||
		    !((unsigned long)edge->tx.addr + edge->tx.size <= (unsigned long)edge->rx.addr ||
		      (unsigned long)edge->rx.addr + edge->rx.size <= (unsigned long)edge->tx.addr))
			goto out;
		writel(QMP_DOWN, &d->local.link_ack);
		writel(QMP_DOWN, &d->local.channel_ack);
		writel(QMP_DOWN, &d->local.channel_state);
		writel(QMP_UP, &d->local.link_state);
		edge->negotiated = true;
		ring = true;
		goto out;
	}
	if (readl(&d->local.link_ack) != QMP_UP)
		goto out;
	local = edge->requested ? QMP_UP : QMP_DOWN;
	if (readl(&d->local.channel_state) != local) {
		writel(local, &d->local.channel_state);
		ring = true;
	}
	remote = readl(&d->remote.channel_state);
	if (readl(&d->remote.channel_ack) != remote) {
		writel(remote, &d->remote.channel_ack);
		ring = true;
	}
	edge->connected = edge->requested && remote == QMP_UP &&
			  readl(&d->local.channel_ack) == QMP_UP;
	if (!edge->connected)
		goto out;

	len = readl(edge->rx.addr);
	if (len) {
		if (len <= edge->rx.size - sizeof(u32)) {
			__ioread32_copy(edge->rx_buf, edge->rx.addr + sizeof(u32),
					DIV_ROUND_UP(len, sizeof(u32)));
			pkt.data = edge->rx_buf;
			pkt.size = len;
		}
		writel(0, edge->rx.addr);
		ring = true;
	}
out:
	spin_unlock_irqrestore(&edge->lock, flags);
	if (pkt.size)
		mbox_chan_received_data(&edge->chan, &pkt);
	if (ring)
		qmp_ring(edge);
	wake_up_all(&edge->event);
	return IRQ_HANDLED;
}

static int qmp_edge_startup(struct mbox_chan *chan)
{
	struct qmp_edge *edge = chan->con_priv;
	unsigned long flags;

	spin_lock_irqsave(&edge->lock, flags);
	edge->requested = true;
	if (edge->negotiated && readl(&edge->desc->local.link_ack) == QMP_UP)
		writel(QMP_UP, &edge->desc->local.channel_state);
	spin_unlock_irqrestore(&edge->lock, flags);
	qmp_ring(edge);
	if (wait_event_timeout(edge->event, READ_ONCE(edge->connected), 5 * HZ))
		return 0;

	spin_lock_irqsave(&edge->lock, flags);
	edge->requested = false;
	edge->connected = false;
	if (edge->negotiated)
		writel(QMP_DOWN, &edge->desc->local.channel_state);
	spin_unlock_irqrestore(&edge->lock, flags);
	qmp_ring(edge);
	return -ETIMEDOUT;
}

static void qmp_edge_shutdown(struct mbox_chan *chan)
{
	struct qmp_edge *edge = chan->con_priv;
	unsigned long flags;

	spin_lock_irqsave(&edge->lock, flags);
	edge->requested = false;
	edge->connected = false;
	if (edge->negotiated)
		writel(QMP_DOWN, &edge->desc->local.channel_state);
	spin_unlock_irqrestore(&edge->lock, flags);
	qmp_ring(edge);
	synchronize_irq(edge->irq);
}

static int qmp_edge_send(struct mbox_chan *chan, void *data)
{
	struct qmp_edge *edge = chan->con_priv;
	const struct qmp_pkt *pkt = data;
	unsigned long flags;
	int ret = 0;
	size_t i;

	if (!pkt || !pkt->data || !pkt->size)
		return -EINVAL;
	spin_lock_irqsave(&edge->lock, flags);
	if (!edge->connected)
		ret = -ENOLINK;
	else if (!IS_ALIGNED(pkt->size, 4) || pkt->size > edge->tx.size - sizeof(u32))
		ret = -EMSGSIZE;
	else if (readl(edge->tx.addr))
		ret = -EBUSY;
	else {
		for (i = 0; i < pkt->size; i += sizeof(u32))
			writel(get_unaligned_le32(pkt->data + i), edge->tx.addr + sizeof(u32) + i);
		writel(pkt->size, edge->tx.addr);
		readl(edge->tx.addr);
	}
	spin_unlock_irqrestore(&edge->lock, flags);
	if (!ret)
		qmp_ring(edge);
	return ret;
}

static bool qmp_edge_tx_done(struct mbox_chan *chan)
{
	struct qmp_edge *edge = chan->con_priv;

	return READ_ONCE(edge->connected) && !readl(edge->tx.addr);
}

static const struct mbox_chan_ops qmp_edge_ops = {
	.startup = qmp_edge_startup,
	.shutdown = qmp_edge_shutdown,
	.send_data = qmp_edge_send,
	.last_tx_done = qmp_edge_tx_done,
};

static void qmp_put_channel(void *data)
{
	mbox_free_channel(data);
}

static void qmp_disable_wake(void *data)
{
	struct qmp_edge *edge = data;

	disable_irq_wake(edge->irq);
}

static int qmp_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qmp_edge *edge;
	void *region;
	u32 pid, offset;
	size_t size;
	int ret;

	if (of_property_present(dev->of_node, "qcom,qmp"))
		return -ENODEV;
	if (of_property_read_u32(dev->of_node, "qcom,remote-pid", &pid) ||
	    of_property_read_u32(dev->of_node, "mbox-desc-offset", &offset))
		return -EINVAL;

	region = qcom_smem_get(pid, QMP_SMEM_ITEM, &size);
	if (IS_ERR(region))
		return dev_err_probe(dev, PTR_ERR(region), "SMEM QMP region unavailable\n");
	if (!region || !IS_ALIGNED(offset, 4) || offset > size ||
	    size - offset < sizeof(struct qmp_descriptor))
		return -EINVAL;

	edge = devm_kzalloc(dev, sizeof(*edge), GFP_KERNEL);
	if (!edge)
		return -ENOMEM;
	edge->desc = (void __iomem *)(region + offset);
	edge->region_size = size - offset;
	edge->rx_buf = devm_kmalloc(dev, edge->region_size, GFP_KERNEL);
	if (!edge->rx_buf)
		return -ENOMEM;
	spin_lock_init(&edge->lock);
	init_waitqueue_head(&edge->event);
	edge->controller.dev = dev;
	edge->controller.ops = &qmp_edge_ops;
	edge->controller.chans = &edge->chan;
	edge->controller.num_chans = 1;
	edge->controller.txdone_poll = true;
	edge->controller.txpoll_period = 1;
	edge->chan.con_priv = edge;

	edge->doorbell_client.dev = dev;
	edge->doorbell_client.knows_txdone = true;
	edge->doorbell = mbox_request_channel(&edge->doorbell_client, 0);
	if (IS_ERR(edge->doorbell))
		return dev_err_probe(dev, PTR_ERR(edge->doorbell), "IPCC doorbell unavailable\n");
	ret = devm_add_action_or_reset(dev, qmp_put_channel, edge->doorbell);
	if (ret)
		return ret;
	edge->irq = platform_get_irq(pdev, 0);
	if (edge->irq < 0)
		return edge->irq;
	ret = devm_request_threaded_irq(dev, edge->irq, NULL, qmp_edge_irq, IRQF_ONESHOT,
					dev_name(dev), edge);
	if (ret)
		return ret;
	if (!enable_irq_wake(edge->irq)) {
		ret = devm_add_action_or_reset(dev, qmp_disable_wake, edge);
		if (ret)
			return ret;
	}
	if (of_property_read_bool(dev->of_node, "qcom,early-boot"))
		qmp_edge_irq(edge->irq, edge);
	return devm_mbox_controller_register(dev, &edge->controller);
}

static const struct of_device_id qmp_match[] = {
	{ .compatible = "qcom,qmp-mbox" },
	{}
};
MODULE_DEVICE_TABLE(of, qmp_match);

static struct platform_driver qmp_mbox_driver = {
	.probe = qmp_mbox_probe,
	.driver = {
		.name = "msm-qmp",
		.of_match_table = qmp_match,
	},
};
module_platform_driver(qmp_mbox_driver);

MODULE_DESCRIPTION("Qualcomm shared memory QMP mailbox");
MODULE_LICENSE("GPL");
