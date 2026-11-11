// SPDX-License-Identifier: GPL-2.0-only
/* Trace endpoints in Qualcomm firmware are enabled through QMI service 0x33. */
#include <linux/coresight.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include "coresight-priv.h"
#include "coresight-qmi.h"

struct remote_etm {
	struct qmi_handle qmi;
	struct coresight_device *csdev;
	struct mutex config;
	spinlock_t lock;
	struct sockaddr_qrtr peer;
	struct work_struct restart;
	u32 trace_id;
	bool wanted, removing;
};

static int remote_set(struct remote_etm *etm, bool enabled)
{
	struct coresight_set_etm_req_msg_v01 request = { .state = enabled };
	struct coresight_set_etm_resp_msg_v01 response = {};
	struct sockaddr_qrtr peer;
	struct qmi_txn txn;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&etm->lock, flags);
	peer = etm->peer;
	spin_unlock_irqrestore(&etm->lock, flags);
	if (!peer.sq_family)
		return -ENOTCONN;
	ret = qmi_txn_init(&etm->qmi, &txn, coresight_set_etm_resp_msg_v01_ei, &response);
	if (ret)
		return ret;
	ret = qmi_send_request(&etm->qmi, &peer, &txn, CORESIGHT_QMI_SET_ETM_REQ_V01,
			       CORESIGHT_QMI_SET_ETM_REQ_MAX_LEN,
			       coresight_set_etm_req_msg_v01_ei, &request);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		return ret;
	}
	ret = qmi_txn_wait(&txn, msecs_to_jiffies(TIMEOUT_MS));
	return ret ?: (response.resp.result ? -EREMOTEIO : 0);
}

static void remote_reapply(struct work_struct *work)
{
	struct remote_etm *etm = container_of(work, struct remote_etm, restart);
	int ret;

	guard(mutex)(&etm->config);
	if (etm->wanted) {
		ret = remote_set(etm, true);
		if (ret)
			dev_warn_ratelimited(etm->csdev->dev.parent,
					     "restoring remote ETM failed: %d\n", ret);
	}
}

static int remote_online(struct qmi_handle *handle, struct qmi_service *svc)
{
	struct remote_etm *etm = container_of(handle, struct remote_etm, qmi);
	unsigned long flags;

	spin_lock_irqsave(&etm->lock, flags);
	etm->peer = (struct sockaddr_qrtr) {
		.sq_family = AF_QIPCRTR, .sq_node = svc->node, .sq_port = svc->port,
	};
	if (!etm->removing)
		schedule_work(&etm->restart);
	spin_unlock_irqrestore(&etm->lock, flags);
	return 0;
}

static void remote_offline(struct qmi_handle *handle, struct qmi_service *svc)
{
	struct remote_etm *etm = container_of(handle, struct remote_etm, qmi);
	unsigned long flags;

	spin_lock_irqsave(&etm->lock, flags);
	if (etm->peer.sq_node == svc->node && etm->peer.sq_port == svc->port)
		etm->peer.sq_family = 0;
	spin_unlock_irqrestore(&etm->lock, flags);
}

static const struct qmi_ops remote_qmi_ops = {
	.new_server = remote_online, .del_server = remote_offline,
};

static int remote_enable(struct coresight_device *csdev, struct perf_event *event,
			 enum cs_mode mode, struct coresight_path *path)
{
	struct remote_etm *etm = dev_get_drvdata(csdev->dev.parent);
	int ret;

	if (mode != CS_MODE_SYSFS)
		return -EOPNOTSUPP;
	guard(mutex)(&etm->config);
	if (!coresight_take_mode(csdev, mode))
		return -EBUSY;
	ret = remote_set(etm, true);
	if (ret)
		coresight_set_mode(csdev, CS_MODE_DISABLED);
	else
		etm->wanted = true;
	return ret;
}

static void remote_disable(struct coresight_device *csdev, struct perf_event *event)
{
	struct remote_etm *etm = dev_get_drvdata(csdev->dev.parent);
	int ret;

	guard(mutex)(&etm->config);
	etm->wanted = false;
	ret = remote_set(etm, false);
	if (ret && ret != -ENOTCONN)
		dev_warn(etm->csdev->dev.parent, "disabling remote trace failed: %d\n", ret);
	coresight_set_mode(csdev, CS_MODE_DISABLED);
}

static int remote_id(struct coresight_device *csdev, enum cs_mode mode,
		     struct coresight_device *sink)
{
	struct remote_etm *etm = dev_get_drvdata(csdev->dev.parent);

	return etm->trace_id;
}

static const struct coresight_ops_source remote_source = {
	.enable = remote_enable, .disable = remote_disable,
};
static const struct coresight_ops remote_ops = {
	.source_ops = &remote_source, .trace_id = remote_id,
};

static void remote_stop(void *data)
{
	struct remote_etm *etm = data;
	unsigned long flags;

	spin_lock_irqsave(&etm->lock, flags);
	etm->removing = true;
	spin_unlock_irqrestore(&etm->lock, flags);
	cancel_work_sync(&etm->restart);
	qmi_handle_release(&etm->qmi);
}

static int remote_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct coresight_desc desc = {
		.dev = dev, .type = CORESIGHT_DEV_TYPE_SOURCE,
		.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_OTHERS,
		.ops = &remote_ops,
	};
	struct remote_etm *etm;
	u32 instance;
	int ret;

	etm = devm_kzalloc(dev, sizeof(*etm), GFP_KERNEL);
	if (!etm)
		return -ENOMEM;
	if (device_property_read_u32(dev, "qcom,inst-id", &instance) ||
	    of_property_read_u32_index(dev->of_node, "atid", 0, &etm->trace_id) ||
	    !etm->trace_id || etm->trace_id >= 128 ||
	    device_property_read_string(dev, "coresight-name", &desc.name))
		return -EINVAL;
	desc.pdata = coresight_get_platform_data(dev);
	if (IS_ERR(desc.pdata))
		return PTR_ERR(desc.pdata);
	dev->platform_data = desc.pdata;
	spin_lock_init(&etm->lock);
	mutex_init(&etm->config);
	INIT_WORK(&etm->restart, remote_reapply);
	ret = qmi_handle_init(&etm->qmi, CORESIGHT_QMI_SET_ETM_RESP_MAX_LEN, &remote_qmi_ops, NULL);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, remote_stop, etm);
	if (ret)
		return ret;
	ret = qmi_add_lookup(&etm->qmi, CORESIGHT_QMI_SVC_ID, CORESIGHT_QMI_VERSION, instance);
	if (ret < 0)
		return ret;
	platform_set_drvdata(pdev, etm);
	etm->csdev = coresight_register(&desc);
	if (IS_ERR(etm->csdev))
		return PTR_ERR(etm->csdev);
	pm_runtime_enable(dev);
	return 0;
}

static void remote_remove(struct platform_device *pdev)
{
	struct remote_etm *etm = platform_get_drvdata(pdev);

	/* Stop restart callbacks before unregistering the CoreSight device. */
	spin_lock_irq(&etm->lock);
	etm->removing = true;
	spin_unlock_irq(&etm->lock);
	cancel_work_sync(&etm->restart);
	coresight_unregister(etm->csdev);
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id remote_match[] = {
	{ .compatible = "qcom,coresight-remote-etm" }, {}
};
MODULE_DEVICE_TABLE(of, remote_match);
static struct platform_driver remote_driver = {
	.probe = remote_probe, .remove = remote_remove,
	.driver = { .name = "coresight-remote-etm", .of_match_table = remote_match },
};
module_platform_driver(remote_driver);
MODULE_DESCRIPTION("Qualcomm firmware-controlled remote ETM trace sources");
MODULE_LICENSE("GPL");
