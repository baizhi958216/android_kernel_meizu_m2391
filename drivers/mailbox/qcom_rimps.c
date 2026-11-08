// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm RIMPS CPU co-processor doorbells on Kalama. */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define RIMPS_TX_DOORBELL	0xc
#define RIMPS_RX_CLEAR		0x308
#define RIMPS_RX_STATUS		0x30c
#define RIMPS_DOMAIN_STRIDE	0x1000

struct rimps_ipc {
	struct mbox_controller controller;
	struct mbox_chan channels[2];
	void __iomem *tx;
	void __iomem *rx;
	int irq;
};

static int rimps_send(struct mbox_chan *chan, void *message)
{
	struct rimps_ipc *ipc = dev_get_drvdata(chan->mbox->dev);

	writel(BIT(28), ipc->tx + RIMPS_TX_DOORBELL);
	return 0;
}

static const struct mbox_chan_ops rimps_ops = {
	.send_data = rimps_send,
};

static irqreturn_t rimps_irq(int irq, void *data)
{
	struct rimps_ipc *ipc = data;
	irqreturn_t result = IRQ_NONE;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ipc->channels); i++) {
		void __iomem *domain = ipc->rx + i * RIMPS_DOMAIN_STRIDE;
		struct mbox_chan *chan = &ipc->channels[i];
		unsigned long flags;

		if (!(readl(domain + RIMPS_RX_STATUS) & BIT(3)))
			continue;
		spin_lock_irqsave(&chan->lock, flags);
		if (chan->cl)
			mbox_chan_received_data(chan, NULL);
		writel(BIT(3), domain + RIMPS_RX_CLEAR);
		spin_unlock_irqrestore(&chan->lock, flags);
		result = IRQ_HANDLED;
	}
	return result;
}

static void rimps_stop_irq(void *data)
{
	struct rimps_ipc *ipc = data;

	disable_irq(ipc->irq);
}

static int rimps_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rimps_ipc *ipc;
	struct resource *res;
	int ret;

	ipc = devm_kzalloc(dev, sizeof(*ipc), GFP_KERNEL);
	if (!ipc)
		return -ENOMEM;
	ipc->tx = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(ipc->tx))
		return PTR_ERR(ipc->tx);
	if (resource_size(res) < RIMPS_TX_DOORBELL + sizeof(u32))
		return -EINVAL;
	ipc->rx = devm_platform_get_and_ioremap_resource(pdev, 1, &res);
	if (IS_ERR(ipc->rx))
		return PTR_ERR(ipc->rx);
	if (resource_size(res) < RIMPS_DOMAIN_STRIDE + RIMPS_RX_STATUS + sizeof(u32))
		return -EINVAL;

	ipc->irq = platform_get_irq(pdev, 0);
	if (ipc->irq < 0)
		return ipc->irq;
	ret = devm_request_irq(dev, ipc->irq, rimps_irq, IRQF_NO_SUSPEND | IRQF_NO_AUTOEN,
			       dev_name(dev), ipc);
	if (ret)
		return ret;
	ipc->controller.dev = dev;
	ipc->controller.chans = ipc->channels;
	ipc->controller.num_chans = ARRAY_SIZE(ipc->channels);
	ipc->controller.ops = &rimps_ops;
	platform_set_drvdata(pdev, ipc);
	ret = devm_mbox_controller_register(dev, &ipc->controller);
	if (ret)
		return ret;
	enable_irq(ipc->irq);
	return devm_add_action_or_reset(dev, rimps_stop_irq, ipc);
}

static const struct of_device_id rimps_match[] = {
	{ .compatible = "qcom,rimps" },
	{}
};
MODULE_DEVICE_TABLE(of, rimps_match);

static struct platform_driver rimps_driver = {
	.probe = rimps_probe,
	.driver = {
		.name = "qcom-rimps",
		.of_match_table = rimps_match,
	},
};
module_platform_driver(rimps_driver);
MODULE_DESCRIPTION("Qualcomm RIMPS CPUCP mailbox");
MODULE_LICENSE("GPL");
