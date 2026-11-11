// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Qualcomm Embedded USB Debug on Kalama, with the factory extcon and UART ABI.
 */
#include <linux/delay.h>
#include <linux/extcon-provider.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeirq.h>
#include <linux/serial_core.h>
#include <linux/tty_flip.h>

#define EUD_TX_ID	0x00
#define EUD_TX_DATA	0x08
#define EUD_RX_ID	0x0c
#define EUD_RX_LEN	0x10
#define EUD_RX_DATA	0x14
#define EUD_IRQ_ENABLE	0x24
#define EUD_IRQ_STATUS	0x44
#define EUD_CTL_OUT	0x74
#define EUD_VBUS_CLEAR	0x80
#define EUD_CHG_CLEAR	0x84
#define EUD_ENABLE	0x1014
#define EUD_ATTACH	0x1018

#define EUD_RX		BIT(0)
#define EUD_TX		BIT(1)
#define EUD_VBUS	BIT(2)
#define EUD_CHG		BIT(3)
#define EUD_SAFE	BIT(4)
#define EUD_CONTROL	(EUD_VBUS | EUD_CHG | EUD_SAFE)
#define EUD_UART_ID	0x90
#define EUD_FIFO	14
#define PORT_MSM_EUD	300

struct msm_eud {
	struct uart_port port;
	struct extcon_dev *extcon;
	void __iomem *base;
	phys_addr_t mode_mgr;
	int irq;
	bool enabled;
	u32 uart_irqs;
};

static DEFINE_MUTEX(eud_lock);
static struct msm_eud *eud_device;
static const unsigned int eud_cables[] = {
	EXTCON_USB, EXTCON_CHG_USB_SDP, EXTCON_JIG, EXTCON_NONE
};
static struct uart_driver eud_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "msm-eud",
	.dev_name = "ttyEUD",
	.nr = 1,
};

/* Caller holds the serial port lock. */
static void eud_write_mask(struct msm_eud *eud)
{
	writel(eud->enabled ? (EUD_CONTROL | eud->uart_irqs) : 0,
	       eud->base + EUD_IRQ_ENABLE);
}

static void eud_tx_start(struct uart_port *port)
{
	struct msm_eud *eud = container_of(port, struct msm_eud, port);

	eud->uart_irqs |= EUD_TX;
	eud_write_mask(eud);
}

static void eud_tx_stop(struct uart_port *port)
{
	struct msm_eud *eud = container_of(port, struct msm_eud, port);

	eud->uart_irqs &= ~EUD_TX;
	eud_write_mask(eud);
}

static void eud_rx_stop(struct uart_port *port)
{
	struct msm_eud *eud = container_of(port, struct msm_eud, port);

	eud->uart_irqs &= ~EUD_RX;
	eud_write_mask(eud);
}

static int eud_uart_startup(struct uart_port *port)
{
	struct msm_eud *eud = container_of(port, struct msm_eud, port);
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	eud->uart_irqs = EUD_RX;
	eud_write_mask(eud);
	uart_port_unlock_irqrestore(port, flags);
	return 0;
}

static void eud_uart_shutdown(struct uart_port *port)
{
	struct msm_eud *eud = container_of(port, struct msm_eud, port);
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	eud->uart_irqs = 0;
	eud_write_mask(eud);
	uart_port_unlock_irqrestore(port, flags);
}

static unsigned int eud_tx_empty(struct uart_port *port)
{
	return readl(port->membase + EUD_IRQ_STATUS) & EUD_TX ? TIOCSER_TEMT : 0;
}

static void eud_set_mctrl(struct uart_port *port, unsigned int mctrl) { }
static unsigned int eud_get_mctrl(struct uart_port *port) { return 0; }
static void eud_set_termios(struct uart_port *port, struct ktermios *new,
			    const struct ktermios *old) { }
static const char *eud_uart_type(struct uart_port *port) { return "EUD UART"; }

static void eud_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_MSM_EUD;
}

static const struct uart_ops eud_uart_ops = {
	.config_port = eud_config_port,
	.tx_empty = eud_tx_empty, .start_tx = eud_tx_start, .stop_tx = eud_tx_stop,
	.stop_rx = eud_rx_stop, .startup = eud_uart_startup, .shutdown = eud_uart_shutdown,
	.set_mctrl = eud_set_mctrl, .get_mctrl = eud_get_mctrl,
	.set_termios = eud_set_termios, .type = eud_uart_type,
};

static void eud_uart_handle(struct msm_eud *eud, u32 irqs)
{
	struct uart_port *port = &eud->port;
	unsigned long flags;
	u32 i, len;
	u8 ch;

	uart_port_lock_irqsave(port, &flags);
	if (!port->state)
		goto out;
	if ((irqs & EUD_RX) && readl(eud->base + EUD_RX_ID) == EUD_UART_ID) {
		len = min_t(u32, readl(eud->base + EUD_RX_LEN), EUD_FIFO);
		for (i = 0; i < len; i++) {
			ch = readl(eud->base + EUD_RX_DATA);
			port->icount.rx++;
			uart_insert_char(port, 0, 0, ch, TTY_NORMAL);
		}
		tty_flip_buffer_push(&port->state->port);
	}
	if (irqs & EUD_TX) {
		writel(EUD_UART_ID, eud->base + EUD_TX_ID);
		if (readl(eud->base + EUD_TX_ID) == EUD_UART_ID)
			uart_port_tx_limited(port, ch, EUD_FIFO, true,
					     writel(ch, eud->base + EUD_TX_DATA), ({}));
	}
out:
	uart_port_unlock_irqrestore(port, flags);
}

static void eud_ack(struct msm_eud *eud, u32 reg)
{
	writel(1, eud->base + reg);
	writel(0, eud->base + reg);
}

static irqreturn_t eud_irq(int irq, void *data)
{
	struct msm_eud *eud = data;
	u32 status, control;

	guard(mutex)(&eud_lock);
	status = readl(eud->base + EUD_IRQ_STATUS) & readl(eud->base + EUD_IRQ_ENABLE);
	if (!status)
		return IRQ_NONE;
	control = readl(eud->base + EUD_CTL_OUT);
	if (status & (EUD_RX | EUD_TX))
		eud_uart_handle(eud, status);
	if (status & EUD_VBUS) {
		extcon_set_state_sync(eud->extcon, EXTCON_USB, !!(control & BIT(4)));
		eud_ack(eud, EUD_VBUS_CLEAR);
	}
	if (status & EUD_CHG) {
		struct power_supply *usb = power_supply_get_by_name("usb");
		union power_supply_propval limit = { .intval = control & BIT(6) ? -EINVAL : 0 };

		if (usb) {
			int ret = power_supply_set_property(usb, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
							    &limit);
			if (ret)
				dev_warn_ratelimited(eud->port.dev, "charger vote failed: %d\n", ret);
			power_supply_put(usb);
		}
		eud_ack(eud, EUD_CHG_CLEAR);
	}
	if (status & EUD_SAFE) {
		if (readl(eud->base + EUD_ATTACH) & BIT(0)) {
			writel(0, eud->base + EUD_ATTACH);
			usleep_range(100, 150);
		}
		writel(1, eud->base + EUD_ATTACH);
	}
	return IRQ_HANDLED;
}

static void eud_cable_state(struct msm_eud *eud, bool enabled)
{
	extcon_set_state(eud->extcon, EXTCON_JIG, enabled);
	extcon_set_state_sync(eud->extcon, EXTCON_USB, enabled);
	extcon_set_state_sync(eud->extcon, EXTCON_CHG_USB_SDP, enabled);
}

static void eud_local_enable(struct msm_eud *eud, bool enabled)
{
	unsigned long flags;

	uart_port_lock_irqsave(&eud->port, &flags);
	eud->enabled = enabled;
	writel(enabled, eud->base + EUD_ENABLE);
	eud_write_mask(eud);
	uart_port_unlock_irqrestore(&eud->port, flags);
}

static int eud_set(const char *val, const struct kernel_param *kp)
{
	struct msm_eud *eud;
	unsigned int enable;
	int ret;

	ret = kstrtouint(val, 0, &enable);
	if (ret || enable > 1)
		return -EINVAL;
	guard(mutex)(&eud_lock);
	eud = eud_device;
	if (!eud)
		return -ENODEV;
	if (eud->enabled == enable)
		return 0;
	ret = qcom_scm_io_writel(eud->mode_mgr, enable);
	if (ret)
		return ret;
	extcon_set_state_sync(eud->extcon, EXTCON_USB, false);
	eud_local_enable(eud, enable);
	usleep_range(50, 100);
	eud_cable_state(eud, enable);
	return 0;
}

static int eud_get(char *buf, const struct kernel_param *kp)
{
	guard(mutex)(&eud_lock);
	return sysfs_emit(buf, "%d\n", eud_device && eud_device->enabled);
}

static const struct kernel_param_ops eud_ops = { .get = eud_get, .set = eud_set };
module_param_cb(enable, &eud_ops, NULL, 0644);

static void eud_remove_uart(void *data)
{
	struct msm_eud *eud = data;

	uart_remove_one_port(&eud_uart_driver, &eud->port);
}

static int msm_eud_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct msm_eud *eud;
	u32 secure_state;
	int ret;

	if (!device_property_read_bool(dev, "qcom,secure-eud-en"))
		return -EINVAL;
	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;
	eud = devm_kzalloc(dev, sizeof(*eud), GFP_KERNEL);
	if (!eud)
		return -ENOMEM;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "eud_mode_mgr2");
	if (!res || resource_size(res) < sizeof(u32))
		return -EINVAL;
	eud->mode_mgr = res->start;
	ret = qcom_scm_io_readl(eud->mode_mgr, &secure_state);
	if (ret) {
		/* The DWC3 cable provider remains available on retail firmware.
		 * A later enable still requires a successful SCM command.
		 */
		dev_dbg(dev, "firmware EUD state unavailable: %d\n", ret);
		secure_state = 0;
	}
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "eud_base");
	if (!res || resource_size(res) < EUD_ATTACH + sizeof(u32))
		return -EINVAL;
	eud->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(eud->base))
		return PTR_ERR(eud->base);
	eud->irq = platform_get_irq_byname(pdev, "eud_irq");
	if (eud->irq < 0)
		return eud->irq;
	eud->extcon = devm_extcon_dev_allocate(dev, eud_cables);
	if (IS_ERR(eud->extcon))
		return PTR_ERR(eud->extcon);
	ret = devm_extcon_dev_register(dev, eud->extcon);
	if (ret)
		return ret;
	eud->port = (struct uart_port) {
		.dev = dev, .type = PORT_MSM_EUD, .fifosize = EUD_FIFO,
		.iotype = UPIO_MEM, .flags = UPF_FIXED_TYPE | UPF_BOOT_AUTOCONF,
		.mapbase = res->start, .membase = eud->base, .irq = eud->irq,
		.ops = &eud_uart_ops,
	};
	ret = uart_add_one_port(&eud_uart_driver, &eud->port);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, eud_remove_uart, eud);
	if (ret)
		return ret;
	ret = devm_request_threaded_irq(dev, eud->irq, NULL, eud_irq, IRQF_ONESHOT,
					dev_name(dev), eud);
	if (ret)
		return ret;
	ret = devm_device_init_wakeup(dev);
	if (ret)
		return ret;
	ret = dev_pm_set_wake_irq(dev, eud->irq);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, eud);
	guard(mutex)(&eud_lock);
	if (secure_state & BIT(0)) {
		eud_local_enable(eud, true);
		eud_cable_state(eud, true);
	}
	eud_device = eud;
	return 0;
}

static void msm_eud_remove(struct platform_device *pdev)
{
	struct msm_eud *eud = platform_get_drvdata(pdev);

	guard(mutex)(&eud_lock);
	eud_device = NULL;
	dev_pm_clear_wake_irq(&pdev->dev);
	writel(0, eud->base + EUD_IRQ_ENABLE);
	eud_cable_state(eud, false);
}

static const struct of_device_id msm_eud_match[] = {
	{ .compatible = "qcom,msm-eud" }, {}
};
MODULE_DEVICE_TABLE(of, msm_eud_match);

static struct platform_driver msm_eud_driver = {
	.probe = msm_eud_probe, .remove = msm_eud_remove,
	.driver = { .name = "msm-eud", .of_match_table = msm_eud_match },
};

static int __init msm_eud_init(void)
{
	int ret = uart_register_driver(&eud_uart_driver);

	if (ret)
		return ret;
	ret = platform_driver_register(&msm_eud_driver);
	if (ret)
		uart_unregister_driver(&eud_uart_driver);
	return ret;
}
module_init(msm_eud_init);

static void __exit msm_eud_exit(void)
{
	platform_driver_unregister(&msm_eud_driver);
	uart_unregister_driver(&eud_uart_driver);
}
module_exit(msm_eud_exit);

MODULE_DESCRIPTION("Qualcomm Kalama embedded USB debug control and UART");
MODULE_LICENSE("GPL");
