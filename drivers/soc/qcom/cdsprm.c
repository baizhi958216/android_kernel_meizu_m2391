// SPDX-License-Identifier: GPL-2.0-only
/* SM8550 compute DSP resource requests on the factory GLINK transport. */
#include <asm/arch_timer.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/rpmsg.h>
#include <linux/workqueue.h>

#define CDSP_RM_REQUEST 2
#define CDSP_VERSION 12
#define CDSP_PD_KILL 16
#define CDSP_PD_KILL_STATUS 17
#define CDSP_QOS_ENABLE 1
#define CDSP_QOS_DISABLE 2

struct cdsprm_request {
	__le32 feature;
	__le32 value;
	__le32 tick_low;
	__le32 tick_high;
	__le32 size;
};

/* All version-2 requests occupy the same wire union, dimensioned for the
 * largest VTCM configuration (16 partitions and 32 application mappings).
 */
struct cdsprm_config {
	__le32 size;
	__le32 version;
	__le32 feature;
	__le32 value;
	u8 reserved[260];
};
static_assert(sizeof(struct cdsprm_config) == 276);

struct cdsprm {
	struct rpmsg_device *rpdev;
	spinlock_t lock;
	struct delayed_work latency_work;
	struct work_struct config_work;
	struct pm_qos_request latency_request;
	struct dentry *debug;
	u64 deadline, max_ticks;
	u32 hz, latency, version;
	bool policy, sent_policy, applied, awaiting_ack, stopping;
};

static void cdsprm_latency_work(struct work_struct *work)
{
	struct cdsprm *rm = container_of(to_delayed_work(work), struct cdsprm, latency_work);
	unsigned long flags;
	u64 deadline, now, delta;

	spin_lock_irqsave(&rm->lock, flags);
	deadline = rm->deadline;
	spin_unlock_irqrestore(&rm->lock, flags);
	now = arch_timer_read_counter();
	delta = deadline > now ? min(deadline - now, rm->max_ticks) : 0;
	cpu_latency_qos_update_request(&rm->latency_request,
				       delta ? rm->latency : PM_QOS_DEFAULT_VALUE);
	spin_lock_irqsave(&rm->lock, flags);
	if (delta && deadline == rm->deadline && !rm->stopping)
		mod_delayed_work(system_highpri_wq, &rm->latency_work,
				 max_t(u64, 1, DIV_ROUND_UP_ULL(delta * HZ, rm->hz)));
	spin_unlock_irqrestore(&rm->lock, flags);
}

static void cdsprm_config_work(struct work_struct *work)
{
	struct cdsprm *rm = container_of(work, struct cdsprm, config_work);
	struct cdsprm_config config = {
		.size = cpu_to_le32(sizeof(config)),
		.version = cpu_to_le32(2),
		.feature = cpu_to_le32(CDSP_PD_KILL),
	};
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&rm->lock, flags);
	if (rm->version <= 2 || rm->stopping || rm->awaiting_ack) {
		spin_unlock_irqrestore(&rm->lock, flags);
		return;
	}
	rm->sent_policy = rm->policy;
	rm->awaiting_ack = true;
	config.value = cpu_to_le32(rm->sent_policy);
	spin_unlock_irqrestore(&rm->lock, flags);
	ret = rpmsg_send(rm->rpdev->ept, &config, sizeof(config));
	if (ret) {
		spin_lock_irqsave(&rm->lock, flags);
		rm->awaiting_ack = false;
		spin_unlock_irqrestore(&rm->lock, flags);
		dev_err_ratelimited(&rm->rpdev->dev, "CDSP policy send failed: %d\n", ret);
	}
}

static int cdsprm_rx(struct rpmsg_device *rpdev, void *data, int len, void *priv, u32 addr)
{
	struct cdsprm *rm = dev_get_drvdata(&rpdev->dev);
	const struct cdsprm_request *req = data;
	unsigned long flags;
	u32 value;
	u64 now, deadline;

	if (!rm || len < sizeof(*req))
		return -EINVAL;
	value = le32_to_cpu(req->value);
	spin_lock_irqsave(&rm->lock, flags);
	if (rm->stopping)
		goto unlock;
	switch (le32_to_cpu(req->feature)) {
	case CDSP_RM_REQUEST:
		if (value != CDSP_QOS_ENABLE && value != CDSP_QOS_DISABLE)
			break;
		deadline = 0;
		if (value == CDSP_QOS_ENABLE) {
			now = arch_timer_read_counter();
			deadline = (u64)le32_to_cpu(req->tick_high) << 32 | le32_to_cpu(req->tick_low);
			deadline = min(deadline, now + rm->max_ticks);
		}
		rm->deadline = deadline;
		mod_delayed_work(system_highpri_wq, &rm->latency_work, 0);
		break;
	case CDSP_VERSION:
		rm->version = value;
		rm->awaiting_ack = false;
		schedule_work(&rm->config_work);
		break;
	case CDSP_PD_KILL_STATUS:
		if (!rm->awaiting_ack)
			break;
		rm->awaiting_ack = false;
		if (!value)
			rm->applied = rm->sent_policy;
		if (rm->policy != rm->sent_policy)
			schedule_work(&rm->config_work);
		break;
	}
unlock:
	spin_unlock_irqrestore(&rm->lock, flags);
	return 0;
}

static int cdsprm_policy_get(void *data, u64 *value)
{
	struct cdsprm *rm = data;

	*value = READ_ONCE(rm->policy);
	return 0;
}

static int cdsprm_policy_set(void *data, u64 value)
{
	struct cdsprm *rm = data;
	unsigned long flags;

	if (value > 1)
		return -EINVAL;
	spin_lock_irqsave(&rm->lock, flags);
	rm->policy = value;
	if (!rm->stopping)
		schedule_work(&rm->config_work);
	spin_unlock_irqrestore(&rm->lock, flags);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(cdsprm_policy_ops, cdsprm_policy_get, cdsprm_policy_set, "%llu\n");

static int cdsprm_probe(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	struct device_node *np;
	struct cdsprm *rm;
	u32 max_ms;
	int ret;

	rm = devm_kzalloc(dev, sizeof(*rm), GFP_KERNEL);
	if (!rm)
		return -ENOMEM;
	for_each_available_child_of_node(dev->of_node, np)
		if (of_device_is_compatible(np, "qcom,msm-cdsp-rm"))
			break;
	if (!np)
		return -ENODEV;
	ret = of_property_read_u32(np, "qcom,qos-latency-us", &rm->latency);
	if (!ret)
		ret = of_property_read_u32(np, "qcom,qos-maxhold-ms", &max_ms);
	rm->policy = of_property_read_bool(np, "qcom,resmgr-pdkill-enable");
	of_node_put(np);
	if (ret)
		return ret;
	rm->hz = arch_timer_get_cntfrq();
	if (!rm->hz || !max_ms || rm->latency > S32_MAX)
		return -EINVAL;
	rm->max_ticks = mul_u32_u32(rm->hz, max_ms) / MSEC_PER_SEC;
	rm->rpdev = rpdev;
	spin_lock_init(&rm->lock);
	INIT_DELAYED_WORK(&rm->latency_work, cdsprm_latency_work);
	INIT_WORK(&rm->config_work, cdsprm_config_work);
	cpu_latency_qos_add_request(&rm->latency_request, PM_QOS_DEFAULT_VALUE);
	rm->debug = debugfs_create_dir("compute", NULL);
	debugfs_create_file("resmgr_pdkill_override_state", 0644, rm->debug, rm, &cdsprm_policy_ops);
	debugfs_create_bool("resmgr_pdkill_state", 0444, rm->debug, &rm->applied);
	dev_set_drvdata(dev, rm);
	return 0;
}

static void cdsprm_remove(struct rpmsg_device *rpdev)
{
	struct cdsprm *rm = dev_get_drvdata(&rpdev->dev);
	unsigned long flags;

	spin_lock_irqsave(&rm->lock, flags);
	rm->stopping = true;
	spin_unlock_irqrestore(&rm->lock, flags);
	debugfs_remove_recursive(rm->debug);
	cancel_work_sync(&rm->config_work);
	cancel_delayed_work_sync(&rm->latency_work);
	cpu_latency_qos_remove_request(&rm->latency_request);
}

static const struct rpmsg_device_id cdsprm_ids[] = {
	{ "cdsprmglink-apps-dsp" }, {}
};
MODULE_DEVICE_TABLE(rpmsg, cdsprm_ids);
static const struct of_device_id cdsprm_match[] = {
	{ .compatible = "qcom,msm-cdsprm-rpmsg" }, {}
};
MODULE_DEVICE_TABLE(of, cdsprm_match);

static struct rpmsg_driver cdsprm_driver = {
	.drv = { .name = "cdsprm", .of_match_table = cdsprm_match },
	.id_table = cdsprm_ids,
	.probe = cdsprm_probe,
	.remove = cdsprm_remove,
	.callback = cdsprm_rx,
};
module_rpmsg_driver(cdsprm_driver);

MODULE_DESCRIPTION("Kalama compute DSP latency and resource policy");
MODULE_LICENSE("GPL");
