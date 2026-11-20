// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kalama TME Hardware Key Manager mailbox protocol.
 * Based on the Qualcomm 2022 tmecom and tme_hwkm_master interfaces.
 */
#include <linux/completion.h>
#include <linux/mailbox/qmp.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "tme-hwkm.h"

#define TME_HWKM_BROADCAST_TAG	0x442f01d9
#define TME_HWKM_BROADCAST_CMD	6
#define TME_TIMEOUT_MS		500

struct tme_request {
	__le32 reserved;
	__le32 txn;
	__le32 tag;
	__le32 command;
};

struct tme_response {
	__le32 reserved;
	__le32 txn;
	__le32 tag;
	__le32 fw_status;
	__le32 seq_status;
	__le32 policy_status[2];
	__le32 seq_debug;
} __packed;

struct tme_hwkm {
	struct mbox_client client;
	struct mbox_chan *channel;
	struct completion completion;
	spinlock_t rx_lock;
	int result;
	bool waiting;
};

/* The Kalama transport key goes to all active receivers on the one master. */
static DEFINE_MUTEX(tme_lock);
static struct tme_hwkm *tme_master;

static void tme_hwkm_rx(struct mbox_client *client, void *message)
{
	struct tme_hwkm *tme = container_of(client, struct tme_hwkm, client);
	const struct qmp_pkt *pkt = message;
	const struct tme_response *rsp;
	unsigned long flags;

	if (!pkt || !pkt->data || pkt->size < offsetof(struct tme_response, tag))
		return;
	rsp = pkt->data;

	spin_lock_irqsave(&tme->rx_lock, flags);
	if (tme->waiting && !rsp->txn) {
		if (pkt->size != sizeof(*rsp))
			tme->result = -EBADMSG;
		else if (rsp->fw_status || rsp->seq_status ||
			 rsp->policy_status[0] || rsp->policy_status[1])
			tme->result = -EIO;
		else
			tme->result = 0;
		tme->waiting = false;
		complete(&tme->completion);
	}
	spin_unlock_irqrestore(&tme->rx_lock, flags);
}

/* The firmware DT does not express this dependency on the UFS node. */
int qcom_tme_hwkm_link(struct device *consumer)
{
	guard(mutex)(&tme_lock);
	if (!tme_master)
		return -EPROBE_DEFER;
	if (!device_link_add(consumer, tme_master->client.dev, DL_FLAG_AUTOREMOVE_CONSUMER))
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(qcom_tme_hwkm_link);

int qcom_tme_hwkm_broadcast(void)
{
	struct tme_request req = {
		.tag = cpu_to_le32(TME_HWKM_BROADCAST_TAG),
		.command = cpu_to_le32(TME_HWKM_BROADCAST_CMD),
	};
	struct qmp_pkt pkt = { .size = sizeof(req), .data = &req };
	struct tme_hwkm *tme;
	unsigned long flags;
	int ret;

	guard(mutex)(&tme_lock);
	tme = tme_master;
	if (!tme)
		return -EPROBE_DEFER;

	spin_lock_irqsave(&tme->rx_lock, flags);
	/*
	 * Kalama firmware uses transaction zero on every broadcast. Only
	 * one request may be in flight, including a timed-out request
	 * whose response has not arrived yet.
	 */
	if (tme->waiting) {
		spin_unlock_irqrestore(&tme->rx_lock, flags);
		return -EAGAIN;
	}
	reinit_completion(&tme->completion);
	tme->waiting = true;
	spin_unlock_irqrestore(&tme->rx_lock, flags);

	ret = mbox_send_message(tme->channel, &pkt);
	if (ret < 0) {
		/* A TX timeout can still be followed by an RX completion. */
		if (ret != -ETIME) {
			spin_lock_irqsave(&tme->rx_lock, flags);
			tme->waiting = false;
			spin_unlock_irqrestore(&tme->rx_lock, flags);
		}
		return ret;
	}

	if (!wait_for_completion_timeout(&tme->completion,
					 msecs_to_jiffies(TME_TIMEOUT_MS)))
		return -ETIMEDOUT;

	return tme->result;
}
EXPORT_SYMBOL_GPL(qcom_tme_hwkm_broadcast);

static int tme_hwkm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tme_hwkm *tme;

	tme = devm_kzalloc(dev, sizeof(*tme), GFP_KERNEL);
	if (!tme)
		return -ENOMEM;

	init_completion(&tme->completion);
	spin_lock_init(&tme->rx_lock);
	tme->client.dev = dev;
	tme->client.tx_block = true;
	tme->client.tx_tout = TME_TIMEOUT_MS;
	tme->client.rx_callback = tme_hwkm_rx;

	guard(mutex)(&tme_lock);
	if (tme_master)
		return -EBUSY;
	tme->channel = mbox_request_channel_byname(&tme->client, "tmecom");
	if (IS_ERR(tme->channel))
		return dev_err_probe(dev, PTR_ERR(tme->channel), "TME mailbox unavailable\n");

	platform_set_drvdata(pdev, tme);
	tme_master = tme;
	return 0;
}

static void tme_hwkm_remove(struct platform_device *pdev)
{
	struct tme_hwkm *tme = platform_get_drvdata(pdev);

	guard(mutex)(&tme_lock);
	tme_master = NULL;
	mbox_free_channel(tme->channel);
}

static const struct of_device_id tme_hwkm_match[] = {
	{ .compatible = "qcom,tmecom-qmp-client" },
	{}
};
MODULE_DEVICE_TABLE(of, tme_hwkm_match);

static struct platform_driver tme_hwkm_driver = {
	.probe = tme_hwkm_probe,
	.remove = tme_hwkm_remove,
	.driver = {
		.name = "tmecom-hwkm",
		.of_match_table = tme_hwkm_match,
	},
};
module_platform_driver(tme_hwkm_driver);

MODULE_SOFTDEP("pre: msm_qmp");
MODULE_DESCRIPTION("Qualcomm Kalama TME transport key broadcast");
MODULE_LICENSE("GPL");
