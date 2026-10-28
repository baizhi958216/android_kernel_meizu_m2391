// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/net.h>

#include "thermal_mitigation_device_service_v01.h"

#define QMI_CDEV_DRIVER		"qmi-cooling-device"
#define QMI_TMD_RESP_TOUT	msecs_to_jiffies(100)

struct qmi_cooling_device {
	struct device_node		*np;
	char				cdev_name[QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01];
	char				qmi_name[QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01];
	bool                            connection_active;
	struct list_head		qmi_node;
	struct thermal_cooling_device	*cdev;
	unsigned int			mtgn_state;
	unsigned int			max_level;
	struct qmi_tmd_instance		*tmd;
};

struct qmi_tmd_instance {
	struct device			*dev;
	struct qmi_handle		handle;
	struct mutex			mutex;
	uint32_t			inst_id;
	struct list_head		tmd_cdev_list;
	struct work_struct		svc_arrive_work;
	bool stopping;
};

static int qmi_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct qmi_cooling_device *qmi_cdev = cdev->devdata;

	if (!qmi_cdev)
		return -EINVAL;

	*state = READ_ONCE(qmi_cdev->max_level);

	return 0;
}

static int qmi_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct qmi_cooling_device *qmi_cdev = cdev->devdata;

	if (!qmi_cdev)
		return -EINVAL;

	*state = READ_ONCE(qmi_cdev->mtgn_state);

	return 0;
}

static int qmi_tmd_send_state_request(struct qmi_cooling_device *qmi_cdev,
				uint8_t state)
{
	int ret = 0;
	struct tmd_set_mitigation_level_req_msg_v01 req;
	struct tmd_set_mitigation_level_resp_msg_v01 tmd_resp;
	struct qmi_tmd_instance *tmd = qmi_cdev->tmd;
	struct qmi_txn txn;

	memset(&req, 0, sizeof(req));
	memset(&tmd_resp, 0, sizeof(tmd_resp));

	strscpy(req.mitigation_dev_id.mitigation_dev_id, qmi_cdev->qmi_name,
		QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01);
	req.mitigation_level = state;

	mutex_lock(&tmd->mutex);

	ret = qmi_txn_init(&tmd->handle, &txn,
		tmd_set_mitigation_level_resp_msg_v01_ei, &tmd_resp);
	if (ret < 0) {
		pr_err("qmi set state:%d txn init failed for %s ret:%d\n",
			state, qmi_cdev->cdev_name, ret);
		goto qmi_send_exit;
	}

	ret = qmi_send_request(&tmd->handle, NULL, &txn,
			QMI_TMD_SET_MITIGATION_LEVEL_REQ_V01,
			TMD_SET_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN,
			tmd_set_mitigation_level_req_msg_v01_ei, &req);
	if (ret < 0) {
		pr_err("qmi set state:%d txn send failed for %s ret:%d\n",
			state, qmi_cdev->cdev_name, ret);
		qmi_txn_cancel(&txn);
		goto qmi_send_exit;
	}

	ret = qmi_txn_wait(&txn, QMI_TMD_RESP_TOUT);
	if (ret < 0) {
		pr_err("qmi set state:%d txn wait failed for %s ret:%d\n",
			state, qmi_cdev->cdev_name, ret);
		goto qmi_send_exit;
	}
	if (tmd_resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ret = tmd_resp.resp.result;
		pr_err("qmi set state:%d NOT success for %s ret:%d\n",
			state, qmi_cdev->cdev_name, ret);
		goto qmi_send_exit;
	}
	ret = 0;
	pr_debug("Requested qmi state:%d for %s\n", state, qmi_cdev->cdev_name);

qmi_send_exit:
	mutex_unlock(&tmd->mutex);
	return ret;
}

static int qmi_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct qmi_cooling_device *qmi_cdev = cdev->devdata;
	int ret = 0;

	if (!qmi_cdev)
		return -EINVAL;

	if (state > READ_ONCE(qmi_cdev->max_level))
		return -EINVAL;

	if (READ_ONCE(qmi_cdev->mtgn_state) == state)
		return 0;

	/* save it and return if server exit */
	if (!READ_ONCE(qmi_cdev->connection_active)) {
		WRITE_ONCE(qmi_cdev->mtgn_state, state);
		pr_debug("Pending request:%ld for %s\n", state,
				qmi_cdev->cdev_name);
		return 0;
	}

	/* It is best effort to save state even if QMI fail */
	ret = qmi_tmd_send_state_request(qmi_cdev, (uint8_t)state);

	WRITE_ONCE(qmi_cdev->mtgn_state, state);

	return ret;
}

static struct thermal_cooling_device_ops qmi_device_ops = {
	.get_max_state = qmi_get_max_state,
	.get_cur_state = qmi_get_cur_state,
	.set_cur_state = qmi_set_cur_state,
};

static int qmi_register_cooling_device(struct qmi_cooling_device *qmi_cdev)
{
	qmi_cdev->cdev = thermal_of_cooling_device_register(
					qmi_cdev->np,
					qmi_cdev->cdev_name,
					qmi_cdev,
					&qmi_device_ops);
	if (IS_ERR(qmi_cdev->cdev)) {
		int ret = PTR_ERR(qmi_cdev->cdev);

		qmi_cdev->cdev = NULL;
		return ret;
	}
	pr_debug("Cooling register success for %s\n", qmi_cdev->cdev_name);

	return 0;
}

static int verify_devices_and_register(struct qmi_tmd_instance *tmd)
{
	struct tmd_get_mitigation_device_list_req_msg_v01 req;
	struct tmd_get_mitigation_device_list_resp_msg_v01 *tmd_resp;
	int ret = 0, i;
	struct qmi_txn txn;

	memset(&req, 0, sizeof(req));
	/* size of tmd_resp is very high, use heap memory rather than stack */
	tmd_resp = kzalloc(sizeof(*tmd_resp), GFP_KERNEL);
	if (!tmd_resp)
		return -ENOMEM;

	mutex_lock(&tmd->mutex);
	ret = qmi_txn_init(&tmd->handle, &txn,
		tmd_get_mitigation_device_list_resp_msg_v01_ei, tmd_resp);
	if (ret < 0) {
		pr_err("Transaction Init error for inst_id:0x%x ret:%d\n",
			tmd->inst_id, ret);
		goto reg_exit;
	}

	ret = qmi_send_request(&tmd->handle, NULL, &txn,
			QMI_TMD_GET_MITIGATION_DEVICE_LIST_REQ_V01,
			TMD_GET_MITIGATION_DEVICE_LIST_REQ_MSG_V01_MAX_MSG_LEN,
			tmd_get_mitigation_device_list_req_msg_v01_ei,
			&req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		goto reg_exit;
	}

	ret = qmi_txn_wait(&txn, QMI_TMD_RESP_TOUT);
	if (ret < 0) {
		pr_err("Transaction wait error for inst_id:0x%x ret:%d\n",
			tmd->inst_id, ret);
		goto reg_exit;
	}
	if (tmd_resp->resp.result != QMI_RESULT_SUCCESS_V01) {
		ret = tmd_resp->resp.result;
		pr_err("Get device list NOT success for inst_id:0x%x ret:%d\n",
			tmd->inst_id, ret);
		goto reg_exit;
	}
	mutex_unlock(&tmd->mutex);

	for (i = 0; i < tmd_resp->mitigation_device_list_len; i++) {
		struct qmi_cooling_device *qmi_cdev = NULL;

		list_for_each_entry(qmi_cdev, &tmd->tmd_cdev_list,
					qmi_node) {
			struct tmd_mitigation_dev_list_type_v01 *device =
				&tmd_resp->mitigation_device_list[i];

			if ((strncasecmp(qmi_cdev->qmi_name,
				device->mitigation_dev_id.mitigation_dev_id,
				QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01)))
				continue;

			WRITE_ONCE(qmi_cdev->connection_active, true);
			WRITE_ONCE(qmi_cdev->max_level, device->max_mitigation_level);
			/*
			 * It is better to set current state
			 * initially or during restart
			 */
			qmi_tmd_send_state_request(qmi_cdev,
							READ_ONCE(qmi_cdev->mtgn_state));
			if (!qmi_cdev->cdev)
				ret = qmi_register_cooling_device(qmi_cdev);
			break;
		}
	}

	for (i = 0; tmd_resp->mitigation_device_list_ext01_valid &&
		i < tmd_resp->mitigation_device_list_ext01_len; i++) {
		struct qmi_cooling_device *qmi_cdev = NULL;

		list_for_each_entry(qmi_cdev, &tmd->tmd_cdev_list,
					qmi_node) {
			struct tmd_mitigation_dev_list_type_v01 *device =
				&tmd_resp->mitigation_device_list_ext01[i];

			if ((strncasecmp(qmi_cdev->qmi_name,
				device->mitigation_dev_id.mitigation_dev_id,
				QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01)))
				continue;

			WRITE_ONCE(qmi_cdev->connection_active, true);
			WRITE_ONCE(qmi_cdev->max_level, device->max_mitigation_level);
			/*
			 * It is better to set current state
			 * initially or during restart
			 */
			qmi_tmd_send_state_request(qmi_cdev,
						READ_ONCE(qmi_cdev->mtgn_state));
			if (!qmi_cdev->cdev)
				ret = qmi_register_cooling_device(qmi_cdev);
			break;
		}
	}

	kfree(tmd_resp);
	return ret;

reg_exit:
	mutex_unlock(&tmd->mutex);
	kfree(tmd_resp);

	return ret;
}

static void qmi_tmd_svc_arrive(struct work_struct *work)
{
	struct qmi_tmd_instance *tmd = container_of(work,
						struct qmi_tmd_instance,
						svc_arrive_work);

	verify_devices_and_register(tmd);
}

static void thermal_qmi_net_reset(struct qmi_handle *qmi)
{
	struct qmi_tmd_instance *tmd = container_of(qmi,
						struct qmi_tmd_instance,
						handle);
	mutex_lock(&tmd->mutex);
	if (!tmd->stopping)
		queue_work(system_highpri_wq, &tmd->svc_arrive_work);
	mutex_unlock(&tmd->mutex);
}

static void thermal_qmi_del_server(struct qmi_handle *qmi,
				    struct qmi_service *service)
{
	struct qmi_tmd_instance *tmd = container_of(qmi,
						struct qmi_tmd_instance,
						handle);
	struct qmi_cooling_device *qmi_cdev = NULL;

	list_for_each_entry(qmi_cdev, &tmd->tmd_cdev_list, qmi_node)
		WRITE_ONCE(qmi_cdev->connection_active, false);
}

static int thermal_qmi_new_server(struct qmi_handle *qmi,
				    struct qmi_service *service)
{
	struct qmi_tmd_instance *tmd = container_of(qmi,
						struct qmi_tmd_instance,
						handle);
	struct sockaddr_qrtr sq = {AF_QIPCRTR, service->node, service->port};

	int ret = -ESHUTDOWN;

	mutex_lock(&tmd->mutex);
	if (!tmd->stopping) {
		ret = kernel_connect(qmi->sock, (struct sockaddr *)&sq, sizeof(sq), 0);
		if (!ret)
			queue_work(system_highpri_wq, &tmd->svc_arrive_work);
	}
	mutex_unlock(&tmd->mutex);
	return ret;
}

static struct qmi_ops thermal_qmi_event_ops = {
	.new_server = thermal_qmi_new_server,
	.del_server = thermal_qmi_del_server,
	.net_reset = thermal_qmi_net_reset,
};

static void release_tmd_instance(void *data)
{
	struct qmi_tmd_instance *tmd = data;
	struct qmi_cooling_device *cdev;

	mutex_lock(&tmd->mutex);
	tmd->stopping = true;
	mutex_unlock(&tmd->mutex);
	cancel_work_sync(&tmd->svc_arrive_work);
	list_for_each_entry(cdev, &tmd->tmd_cdev_list, qmi_node)
		if (cdev->cdev)
			thermal_cooling_device_unregister(cdev->cdev);
	qmi_handle_release(&tmd->handle);
}

static void put_cooling_node(void *data)
{
	of_node_put(data);
}

static int probe_tmd_instance(struct device *dev, struct device_node *node)
{
	struct qmi_tmd_instance *tmd;
	int ret;

	tmd = devm_kzalloc(dev, sizeof(*tmd), GFP_KERNEL);
	if (!tmd)
		return -ENOMEM;
	ret = of_property_read_u32(node, "qcom,instance-id", &tmd->inst_id);
	if (ret)
		return ret;
	tmd->dev = dev;
	mutex_init(&tmd->mutex);
	INIT_LIST_HEAD(&tmd->tmd_cdev_list);
	INIT_WORK(&tmd->svc_arrive_work, qmi_tmd_svc_arrive);
	for_each_available_child_of_node_scoped(node, child) {
		struct qmi_cooling_device *cdev;
		const char *name;

		ret = of_property_read_string(child, "qcom,qmi-dev-name", &name);
		if (ret)
			return ret;
		cdev = devm_kzalloc(dev, sizeof(*cdev), GFP_KERNEL);
		if (!cdev)
			return -ENOMEM;
		if (strscpy(cdev->qmi_name, name, sizeof(cdev->qmi_name)) < 0 ||
		    strscpy(cdev->cdev_name, child->name, sizeof(cdev->cdev_name)) < 0)
			return -ENAMETOOLONG;
		cdev->np = of_node_get(child);
		ret = devm_add_action_or_reset(dev, put_cooling_node, cdev->np);
		if (ret)
			return ret;
		cdev->tmd = tmd;
		list_add_tail(&cdev->qmi_node, &tmd->tmd_cdev_list);
	}
	if (list_empty(&tmd->tmd_cdev_list))
		return 0;

	ret = qmi_handle_init(&tmd->handle, TMD_GET_MITIGATION_DEVICE_LIST_RESP_MSG_V01_MAX_MSG_LEN,
			      &thermal_qmi_event_ops, NULL);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, release_tmd_instance, tmd);
	if (ret)
		return ret;
	return qmi_add_lookup(&tmd->handle, TMD_SERVICE_ID_V01, TMD_SERVICE_VERS_V01, tmd->inst_id);
}

static int qmi_device_probe(struct platform_device *pdev)
{
	for_each_available_child_of_node_scoped(pdev->dev.of_node, node) {
		int ret = probe_tmd_instance(&pdev->dev, node);

		if (ret)
			return dev_err_probe(&pdev->dev, ret, "%pOF: TMD service\n", node);
	}
	return 0;
}

static const struct of_device_id qmi_device_match[] = {
	{.compatible = "qcom,qmi-cooling-devices"},
	{}
};
MODULE_DEVICE_TABLE(of, qmi_device_match);

static struct platform_driver qmi_device_driver = {
	.probe          = qmi_device_probe,
	.driver         = {
		.name   = QMI_CDEV_DRIVER,
		.of_match_table = qmi_device_match,
	},
};

static int __init qmi_device_init(void)
{
	return platform_driver_register(&qmi_device_driver);
}
module_init(qmi_device_init);

static void __exit qmi_device_exit(void)
{
	platform_driver_unregister(&qmi_device_driver);
}
module_exit(qmi_device_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QTI QMI cooling device driver");
