// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2014-2021, The Linux Foundation. All rights reserved. */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/suspend.h>

/* ADSP's "sleepstate" SMEM word uses bit 12 for AP wakefulness. */
#define AP_AWAKE BIT(12)

struct smp2p_sleepstate {
	struct qcom_smem_state *state;
	struct notifier_block pm;
};

static int smp2p_sleepstate_notify(struct notifier_block *nb,
				  unsigned long event, void *unused)
{
	struct smp2p_sleepstate *s = container_of(nb, struct smp2p_sleepstate, pm);
	int ret;

	switch (event) {
	case PM_SUSPEND_PREPARE:
	case PM_POST_SUSPEND:
		ret = qcom_smem_state_update_bits(s->state, AP_AWAKE,
						 event == PM_POST_SUSPEND ? AP_AWAKE : 0);
		return notifier_from_errno(ret);
	default:
		return NOTIFY_DONE;
	}
}

static irqreturn_t smp2p_sleepstate_irq(int irq, void *data)
{
	pm_wakeup_event(data, 200);
	return IRQ_HANDLED;
}

static int smp2p_sleepstate_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct smp2p_sleepstate *s;
	unsigned int bit;
	int irq, ret;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->state = devm_qcom_smem_state_get(dev, NULL, &bit);
	if (IS_ERR(s->state))
		return dev_err_probe(dev, PTR_ERR(s->state), "sleepstate SMEM unavailable\n");

	irq = platform_get_irq_byname(pdev, "smp2p-sleepstate-in");
	if (irq < 0)
		return irq;

	ret = devm_device_init_wakeup(dev);
	if (ret)
		return ret;
	ret = devm_request_threaded_irq(dev, irq, NULL, smp2p_sleepstate_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING, dev_name(dev), dev);
	if (ret)
		return ret;

	ret = qcom_smem_state_update_bits(s->state, AP_AWAKE, AP_AWAKE);
	if (ret)
		return ret;

	s->pm.notifier_call = smp2p_sleepstate_notify;
	s->pm.priority = INT_MAX;
	ret = register_pm_notifier(&s->pm);
	if (ret)
		qcom_smem_state_update_bits(s->state, AP_AWAKE, 0);
	platform_set_drvdata(pdev, s);
	return ret;
}

static void smp2p_sleepstate_remove(struct platform_device *pdev)
{
	struct smp2p_sleepstate *s = platform_get_drvdata(pdev);

	unregister_pm_notifier(&s->pm);
	qcom_smem_state_update_bits(s->state, AP_AWAKE, 0);
}

static const struct of_device_id smp2p_sleepstate_match[] = {
	{ .compatible = "qcom,smp2p-sleepstate" },
	{}
};
MODULE_DEVICE_TABLE(of, smp2p_sleepstate_match);

static struct platform_driver smp2p_sleepstate_driver = {
	.probe = smp2p_sleepstate_probe,
	.remove = smp2p_sleepstate_remove,
	.driver = {
		.name = "smp2p_sleepstate",
		.of_match_table = smp2p_sleepstate_match,
	},
};
module_platform_driver(smp2p_sleepstate_driver);
MODULE_DESCRIPTION("Qualcomm ADSP sleep-state notification");
MODULE_LICENSE("GPL");
