// SPDX-License-Identifier: GPL-2.0-only
/* CPUCP alternates between two shared SRAM banks and rings RIMPS channel 1. */
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#define CREATE_TRACE_POINTS
#include <trace/events/qcom_cpucp.h>

#define CPUCP_LINE_SIZE 1024
#define CPUCP_PACKETS 4

struct rimps_packet {
	char *data;
	size_t size;
	bool gap;
};

struct rimps_log {
	struct mbox_client client;
	struct mbox_chan *chan;
	void __iomem *bank[2];
	size_t bank_size[2];
	struct rimps_packet queue[CPUCP_PACKETS];
	spinlock_t lock;
	unsigned int head, tail;
	bool gap;
	struct work_struct work;
	char line[CPUCP_LINE_SIZE + 1];
	unsigned int chars;
};

static void rimps_process(struct work_struct *work)
{
	struct rimps_log *log = container_of(work, struct rimps_log, work);
	unsigned long flags;

	for (;;) {
		struct rimps_packet *packet;
		size_t i;

		spin_lock_irqsave(&log->lock, flags);
		if (log->tail == log->head) {
			spin_unlock_irqrestore(&log->lock, flags);
			break;
		}
		packet = &log->queue[log->tail % CPUCP_PACKETS];
		spin_unlock_irqrestore(&log->lock, flags);
		if (packet->gap)
			log->chars = 0;
		for (i = 0; i < packet->size; i++) {
			char ch = packet->data[i];

			if (ch != '\n' && ch)
				log->line[log->chars++] = ch;
			if (ch == '\n' || log->chars == CPUCP_LINE_SIZE) {
				log->line[log->chars] = 0;
				trace_cpucp_log(log->line);
				log->chars = 0;
			}
		}
		spin_lock_irqsave(&log->lock, flags);
		log->tail++;
		spin_unlock_irqrestore(&log->lock, flags);
	}
}

static void rimps_receive(struct mbox_client *cl, void *message)
{
	struct rimps_log *log = container_of(cl, struct rimps_log, client);
	struct rimps_packet *packet;
	u32 marker = readl(log->bank[0]);
	size_t size, offset;
	unsigned long flags;
	unsigned int bank;

	spin_lock_irqsave(&log->lock, flags);
	if (marker < sizeof(marker) || marker > log->bank_size[0] + log->bank_size[1] ||
	    log->head - log->tail == CPUCP_PACKETS) {
		log->gap = true;
		spin_unlock_irqrestore(&log->lock, flags);
		return;
	}
	bank = marker > log->bank_size[0];
	offset = bank ? 0 : sizeof(marker);
	size = bank ? marker - log->bank_size[0] : marker - offset;
	packet = &log->queue[log->head % CPUCP_PACKETS];
	memcpy_fromio(packet->data, log->bank[bank] + offset, size);
	packet->size = size;
	packet->gap = log->gap;
	log->gap = false;
	log->head++;
	spin_unlock_irqrestore(&log->lock, flags);
	schedule_work(&log->work);
}

static void rimps_free_packet(void *data)
{
	kvfree(data);
}

static void rimps_log_stop(void *data)
{
	struct rimps_log *log = data;

	mbox_free_channel(log->chan);
	cancel_work_sync(&log->work);
}

static int rimps_log_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rimps_log *log;
	unsigned int i;
	int ret;

	log = devm_kzalloc(dev, sizeof(*log), GFP_KERNEL);
	if (!log)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(log->bank); i++) {
		struct resource *res;

		log->bank[i] = devm_platform_get_and_ioremap_resource(pdev, i, &res);
		if (IS_ERR(log->bank[i]))
			return PTR_ERR(log->bank[i]);
		log->bank_size[i] = resource_size(res);
		if (log->bank_size[i] < sizeof(u32) || log->bank_size[i] > U32_MAX / 2)
			return -EINVAL;
	}
	for (i = 0; i < ARRAY_SIZE(log->queue); i++) {
		log->queue[i].data = kvmalloc(max(log->bank_size[0], log->bank_size[1]),
					     GFP_KERNEL);
		if (!log->queue[i].data)
			return -ENOMEM;
		ret = devm_add_action_or_reset(dev, rimps_free_packet, log->queue[i].data);
		if (ret)
			return ret;
	}
	spin_lock_init(&log->lock);
	INIT_WORK(&log->work, rimps_process);
	log->client.dev = dev;
	log->client.knows_txdone = true;
	log->client.rx_callback = rimps_receive;
	log->chan = mbox_request_channel(&log->client, 0);
	if (IS_ERR(log->chan))
		return dev_err_probe(dev, PTR_ERR(log->chan), "cannot request CPUCP log mailbox\n");
	return devm_add_action_or_reset(dev, rimps_log_stop, log);
}

static const struct of_device_id rimps_log_match[] = {
	{ .compatible = "qcom,rimps-log" },
	{}
};
MODULE_DEVICE_TABLE(of, rimps_log_match);
static struct platform_driver rimps_log_driver = {
	.probe = rimps_log_probe,
	.driver = {
		.name = "rimps-log",
		.of_match_table = rimps_log_match,
	},
};
module_platform_driver(rimps_log_driver);
MODULE_DESCRIPTION("Qualcomm CPUCP firmware trace receiver");
MODULE_LICENSE("GPL");
