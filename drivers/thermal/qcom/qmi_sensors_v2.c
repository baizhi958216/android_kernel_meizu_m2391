// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/suspend.h>
#include <linux/thermal.h>

#include "thermal_sensor_service_v02.h"
#include "qmi_sensors.h"


#define QMI_SENS_DRIVER		"qmi-therm-sensors-v2"
#define QMI_TS_RESP_TOUT	msecs_to_jiffies(100)

struct qmi_sensor {
	struct device			*dev;
	char				qmi_name[QMI_CLIENT_NAME_LENGTH];
	bool                            connection_active;
	struct list_head		ts_node;
	struct thermal_zone_device	*tz_dev;
	int32_t				last_reading;
	int32_t				high_thresh;
	int32_t				low_thresh;
	struct qmi_ts_instance		*ts;
	enum qmi_ts_sensor		sens_type;
	struct work_struct		therm_notify_work;
};

struct qmi_ts_instance {
	struct device			*dev;
	struct qmi_ts_driver *drv;
	struct qmi_handle		handle;
	bool				stopping;
	struct mutex			mutex;
	uint32_t			inst_id;
	struct list_head		ts_sensor_list;
	struct work_struct		svc_arrive_work;
};

struct qmi_ts_driver {
	struct qmi_ts_instance *instances;
	unsigned int active;
	unsigned int count;
	struct notifier_block pm_nb;
	atomic_t in_suspend;
};


static int qmi_sensor_pm_notify(struct notifier_block *nb,
				unsigned long mode, void *_unused)
{
	struct qmi_ts_driver *drv = container_of(nb, struct qmi_ts_driver, pm_nb);

	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:
		atomic_set(&drv->in_suspend, 1);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		atomic_set(&drv->in_suspend, 0);
		break;
	default:
		break;
	}
	return 0;
}



static void qmi_ts_thresh_notify(struct work_struct *work)
{
	struct qmi_sensor *qmi_sens = container_of(work,
						struct qmi_sensor,
						therm_notify_work);

	thermal_zone_device_update(qmi_sens->tz_dev, THERMAL_TRIP_VIOLATED);
}

static void qmi_ts_update_temperature(struct qmi_ts_instance *ts,
		const struct ts_temp_report_ind_msg_v02 *ind_msg,
		uint8_t notify)
{
	struct qmi_sensor *qmi_sens;

	list_for_each_entry(qmi_sens, &ts->ts_sensor_list,
					ts_node) {
		if ((strncasecmp(qmi_sens->qmi_name,
			ind_msg->sensor_id.sensor_id,
			QMI_TS_SENSOR_ID_LENGTH_MAX_V02)))
			continue;

		bool first = READ_ONCE(qmi_sens->last_reading) == THERMAL_TEMP_INVALID;

		WRITE_ONCE(qmi_sens->last_reading, ind_msg->temp);

		pr_debug("sensor:%s temperature:%d\n",
				qmi_sens->qmi_name, qmi_sens->last_reading);
		if (!READ_ONCE(qmi_sens->tz_dev))
			return;
		if (first || (notify &&
			(qmi_sens->last_reading >= READ_ONCE(qmi_sens->high_thresh) ||
			 qmi_sens->last_reading <= READ_ONCE(qmi_sens->low_thresh)))) {
			pr_debug("Sensor:%s Notify. temp:%d\n",
				ind_msg->sensor_id.sensor_id,
				qmi_sens->last_reading);
			queue_work(system_highpri_wq,
				&qmi_sens->therm_notify_work);
		}
		return;
	}
}

static void qmi_ts_ind_cb_v02(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
				   struct qmi_txn *txn, const void *decoded)
{
	const struct ts_temp_report_ind_msg_v02 *ind_msg = decoded;
	bool notify;
	struct qmi_ts_instance *ts = container_of(qmi, struct qmi_ts_instance,
							handle);

	notify = ind_msg->report_type != QMI_TS_TEMP_REPORT_CURRENT_TEMP_V02;

	if (ind_msg->temp_valid)
		qmi_ts_update_temperature(ts, ind_msg, notify);
	else
		pr_err("Error invalid temperature field.\n");
}

static int qmi_ts_send_request(struct qmi_ts_instance *ts, int msg_id,
				size_t len, struct qmi_elem_info *req_ei,
				struct qmi_elem_info *resp_ei,
				void *req, void *resp,
				struct qmi_response_type_v01 *resp_data)
{
	int ret = 0;
	struct qmi_txn txn;

	mutex_lock(&ts->mutex);
	if (ts->stopping) {
		ret = -ENODEV;
		goto qmi_ts_send_exit;
	}
	ret = qmi_txn_init(&ts->handle, &txn, resp_ei, resp);
	if (ret < 0) {
		pr_err("qmi txn init failed for msg id:0x%x ret:%d\n",
			msg_id, ret);
		goto qmi_ts_send_exit;
	}

	ret = qmi_send_request(&ts->handle, NULL, &txn, msg_id, len,
				req_ei, req);
	if (ret < 0) {
		pr_err("qmi txn send failed for msg id:0x%x ret:%d\n",
			msg_id, ret);
		qmi_txn_cancel(&txn);
		goto qmi_ts_send_exit;
	}

	ret = qmi_txn_wait(&txn, QMI_TS_RESP_TOUT);
	if (ret < 0) {
		pr_err("qmi txn wait failed for msg id:0x%x ret:%d\n",
			msg_id, ret);
		goto qmi_ts_send_exit;
	}
	if (resp_data->result != QMI_RESULT_SUCCESS_V01) {
		ret = -EREMOTEIO;
		pr_err("qmi NOT success for msg id:0x%x ret:%d\n",
			msg_id, ret);
		goto qmi_ts_send_exit;
	}
	ret = 0;

qmi_ts_send_exit:
	mutex_unlock(&ts->mutex);
	return ret;
}

static int qmi_ts_request(struct qmi_sensor *qmi_sens,
				bool send_current_temp_report)
{
	int ret = 0;
	struct ts_register_notification_temp_resp_msg_v02 resp;
	struct ts_register_notification_temp_req_msg_v02 req;
	struct qmi_ts_instance *ts = qmi_sens->ts;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	strscpy(req.sensor_id.sensor_id, qmi_sens->qmi_name,
		QMI_TS_SENSOR_ID_LENGTH_MAX_V02);
	req.seq_num = 0;
	if (send_current_temp_report) {
		req.send_current_temp_report = 1;
		req.seq_num_valid = true;
	} else {
		req.seq_num_valid = false;
		req.temp_threshold_high_valid =
			qmi_sens->high_thresh != INT_MAX;
		req.temp_threshold_high = qmi_sens->high_thresh;
		req.temp_threshold_low_valid =
			qmi_sens->low_thresh != (-INT_MAX);
		req.temp_threshold_low = qmi_sens->low_thresh;
		pr_debug("sensor:%s high_valid:%d high:%d low_valid:%d low:%d\n",
				req.sensor_id.sensor_id, req.temp_threshold_high_valid,
				req.temp_threshold_high, req.temp_threshold_low_valid,
				req.temp_threshold_low);
	}

	ret = qmi_ts_send_request(ts, QMI_TS_REGISTER_NOTIFICATION_TEMP_REQ_V02,
			TS_REGISTER_NOTIFICATION_TEMP_REQ_MSG_V02_MAX_MSG_LEN,
			ts_register_notification_temp_req_msg_v02_ei,
			ts_register_notification_temp_resp_msg_v02_ei,
			&req, &resp, &resp.resp);
	if (ret < 0)
		pr_err("qmi ts txn failed for %s ret:%d\n",
			qmi_sens->qmi_name, ret);

	return ret;
}

static int qmi_sensor_read(struct thermal_zone_device *tz, int *temp)
{
	struct qmi_sensor *qmi_sens = thermal_zone_device_priv(tz);

	if (!READ_ONCE(qmi_sens->connection_active))
		return -EAGAIN;
	if (!atomic_read(&qmi_sens->ts->drv->in_suspend))
		qmi_ts_request(qmi_sens, true);
	*temp = READ_ONCE(qmi_sens->last_reading);
	if (*temp == THERMAL_TEMP_INVALID)
		return -EAGAIN;

	return 0;
}

static int qmi_sensor_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct qmi_sensor *qmi_sens = thermal_zone_device_priv(tz);
	int ret = 0;

	if (qmi_sens->high_thresh == high &&
		qmi_sens->low_thresh == low)
		return ret;
	WRITE_ONCE(qmi_sens->high_thresh, high);
	WRITE_ONCE(qmi_sens->low_thresh, low);
	if (!READ_ONCE(qmi_sens->connection_active))
		return ret;
	ret = qmi_ts_request(qmi_sens, false);
	if (ret < 0)
		pr_err("Sensor:%s set high trip:%d low trip:%d error%d\n",
				qmi_sens->qmi_name,
				qmi_sens->high_thresh,
				qmi_sens->low_thresh,
				ret);

	return ret;
}

static const struct thermal_zone_device_ops qmi_sensor_ops = {
	.get_temp = qmi_sensor_read,
	.set_trips = qmi_sensor_set_trips,
};

static struct qmi_msg_handler handlers[] = {
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_TS_TEMP_REPORT_IND_V02,
		.ei = ts_temp_report_ind_msg_v02_ei,
		.decoded_size = sizeof(struct ts_temp_report_ind_msg_v02),
		.fn = qmi_ts_ind_cb_v02
	},
	{}
};

static int qmi_register_sensor_device(struct qmi_sensor *qmi_sens)
{
	struct thermal_zone_device *tz;

	tz = devm_thermal_of_zone_register(qmi_sens->dev,
			qmi_sens->sens_type + qmi_sens->ts->inst_id,
			qmi_sens, &qmi_sensor_ops);
	if (IS_ERR(tz))
		return dev_err_probe(qmi_sens->dev, PTR_ERR(tz),
				     "Failed to register %s\n", qmi_sens->qmi_name);
	WRITE_ONCE(qmi_sens->tz_dev, tz);

	pr_debug("Sensor register success for %s\n", qmi_sens->qmi_name);
	return 0;
}

static int verify_sensor_and_register(struct qmi_ts_instance *ts)
{
	struct ts_get_sensor_list_req_msg_v02 list_req;
	struct ts_get_sensor_list_resp_msg_v02 *list_resp;
	struct ts_get_total_num_of_sensors_req_msg_v02 tot_req;
	struct ts_get_total_num_of_sensors_resp_msg_v02 tot_resp;
	int ret = 0, i, j, sensor_list_itr;

	memset(&tot_resp, 0, sizeof(tot_resp));
	memset(&tot_req, 0, sizeof(tot_req));
	/* size of list_resp is very high, use heap memory rather than stack */
	list_resp = kzalloc(sizeof(*list_resp), GFP_KERNEL);
	if (!list_resp)
		return -ENOMEM;

	ret = qmi_ts_send_request(ts, QMI_TS_GET_TOTAL_NUM_OF_SENSORS_REQ_V02,
			TS_GET_TOTAL_NUM_OF_SENSORS_REQ_MSG_V02_MAX_MSG_LEN,
			ts_get_total_num_of_sensors_req_msg_v02_ei,
			ts_get_total_num_of_sensors_resp_msg_v02_ei,
			&tot_req, &tot_resp, &tot_resp.resp);
	if (ret < 0) {
		pr_err("qmi ts txn failed, inst_id:%d ret:%d\n",
			ts->inst_id, ret);
		goto reg_exit;
	}

	sensor_list_itr = (tot_resp.total_num_sensors /
				QMI_TS_SENSOR_LIST_MAX_V02 +
			(tot_resp.total_num_sensors %
				QMI_TS_SENSOR_LIST_MAX_V02 ? 1 : 0));

	pr_debug("Total QMI sensors cnt:%d list iteration cnt:%d inst_id:%d\n",
		tot_resp.total_num_sensors, sensor_list_itr, ts->inst_id);

	for (i = 0; i < sensor_list_itr; i++) {
		memset(&list_req, 0, sizeof(list_req));
		memset(list_resp, 0, sizeof(*list_resp));

		list_req.list_index = i;

		ret = qmi_ts_send_request(ts, QMI_TS_GET_SENSOR_LIST_REQ_V02,
				TS_GET_SENSOR_LIST_REQ_MSG_V02_MAX_MSG_LEN,
				ts_get_sensor_list_req_msg_v02_ei,
				ts_get_sensor_list_resp_msg_v02_ei,
				&list_req, list_resp, &list_resp->resp);
		if (ret < 0) {
			pr_err("qmi ts txn failed, inst_id:%d itr:%d ret:%d\n",
			ts->inst_id, i, ret);
			goto reg_exit;
		}

		if (list_resp->sensor_list_len > ARRAY_SIZE(list_resp->sensor_list)) {
			ret = -EPROTO;
			goto reg_exit;
		}
		for (j = 0; j < list_resp->sensor_list_len; j++) {
			struct qmi_sensor *qmi_sens = NULL;

			pr_debug("QMI TS sensor[%d][%d]: %s\n", i, j,
				list_resp->sensor_list[j].sensor_id);

			list_for_each_entry(qmi_sens, &ts->ts_sensor_list,
						ts_node) {
				if ((strncasecmp(qmi_sens->qmi_name,
					list_resp->sensor_list[j].sensor_id,
					QMI_TS_SENSOR_ID_LENGTH_MAX_V02)))
					continue;

				WRITE_ONCE(qmi_sens->connection_active, true);
				/*
				 * Send a temperature request notification.
				 */
				qmi_ts_request(qmi_sens, true);
				if (!qmi_sens->tz_dev) {
					ret = qmi_register_sensor_device(qmi_sens);
				} else {
					ret = qmi_ts_request(qmi_sens, false);
					if (ret)
						pr_err(
						"sensor:%s trips:%d %d err%d\n",
							qmi_sens->qmi_name,
							qmi_sens->high_thresh,
							qmi_sens->low_thresh,
							ret);
				}
				break;
			}
		}
	}

reg_exit:
	kfree(list_resp);
	return ret;
}

static void qmi_ts_svc_arrive(struct work_struct *work)
{
	struct qmi_ts_instance *ts = container_of(work,
						struct qmi_ts_instance,
						svc_arrive_work);

	verify_sensor_and_register(ts);
}

static void thermal_qmi_net_reset(struct qmi_handle *qmi)
{
	struct qmi_ts_instance *ts = container_of(qmi, struct qmi_ts_instance, handle);
	struct qmi_sensor *sensor;

	list_for_each_entry(sensor, &ts->ts_sensor_list, ts_node) {
		WRITE_ONCE(sensor->connection_active, false);
		WRITE_ONCE(sensor->last_reading, THERMAL_TEMP_INVALID);
	}
}

static void thermal_qmi_del_server(struct qmi_handle *qmi, struct qmi_service *service)
{
	thermal_qmi_net_reset(qmi);
}

static int thermal_qmi_new_server(struct qmi_handle *qmi,
				    struct qmi_service *service)
{
	struct qmi_ts_instance *ts = container_of(qmi,
						struct qmi_ts_instance,
						handle);
	struct sockaddr_qrtr sq = {AF_QIPCRTR, service->node, service->port};
	int ret;

	mutex_lock(&ts->mutex);
	ret = ts->stopping ? -ENODEV :
		kernel_connect(qmi->sock, (struct sockaddr *)&sq, sizeof(sq), 0);
	if (!ret)
		queue_work(system_highpri_wq, &ts->svc_arrive_work);
	mutex_unlock(&ts->mutex);
	return ret;
}

static struct qmi_ops thermal_qmi_event_ops = {
	.new_server = thermal_qmi_new_server,
	.del_server = thermal_qmi_del_server,
	.net_reset = thermal_qmi_net_reset,
};

static void qmi_ts_cleanup(struct qmi_ts_driver *drv)
{
	while (drv->active) {
		struct qmi_ts_instance *ts = &drv->instances[--drv->active];
		struct qmi_sensor *sensor;

		mutex_lock(&ts->mutex);
		ts->stopping = true;
		mutex_unlock(&ts->mutex);
		cancel_work_sync(&ts->svc_arrive_work);
		qmi_handle_release(&ts->handle);
		list_for_each_entry(sensor, &ts->ts_sensor_list, ts_node)
			cancel_work_sync(&sensor->therm_notify_work);
	}
}

static int of_get_qmi_ts_platform_data(struct device *dev, struct qmi_ts_driver *drv)
{
	int ret = 0, i = 0, idx = 0;
	struct device_node *np = dev->of_node;
	struct device_node *subsys_np = NULL;
	struct qmi_ts_instance *ts;
	struct qmi_sensor *qmi_sens;
	int sens_name_max = 0, sens_idx = 0, subsys_cnt = 0;

	subsys_cnt = of_get_available_child_count(np);
	if (!subsys_cnt) {
		dev_err(dev, "No child node to process\n");
		return -EFAULT;
	}

	ts = devm_kcalloc(dev, subsys_cnt, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	for_each_available_child_of_node(np, subsys_np) {
		if (idx >= subsys_cnt)
			break;

		ret = of_property_read_u32(subsys_np, "qcom,instance-id",
					&ts[idx].inst_id);
		if (ret) {
			dev_err(dev, "error reading qcom,insance-id. ret:%d\n",
					ret);
			goto data_fetch_err;
		}

		ts[idx].dev = dev;
		ts[idx].drv = drv;
		mutex_init(&ts[idx].mutex);
		INIT_LIST_HEAD(&ts[idx].ts_sensor_list);
		INIT_WORK(&ts[idx].svc_arrive_work, qmi_ts_svc_arrive);

		sens_name_max = of_property_count_strings(subsys_np,
					"qcom,qmi-sensor-names");
		if (sens_name_max <= 0) {
			dev_err(dev, "Invalid or no sensor. err:%d\n",
					sens_name_max);
			ret = -EINVAL;
			goto data_fetch_err;
		}

		for (sens_idx = 0; sens_idx < sens_name_max; sens_idx++) {
			const char *qmi_name;

			qmi_sens = devm_kzalloc(dev, sizeof(*qmi_sens),
							GFP_KERNEL);
			if (!qmi_sens) {
				ret = -ENOMEM;
				goto data_fetch_err;
			}

			of_property_read_string_index(subsys_np,
					"qcom,qmi-sensor-names", sens_idx,
					&qmi_name);
			strscpy(qmi_sens->qmi_name, qmi_name,
						QMI_CLIENT_NAME_LENGTH);
			/* Check for supported qmi sensors */
			for (i = 0; i < QMI_TS_MAX_NR; i++) {
				if (!strcmp(sensor_clients[i],
							qmi_sens->qmi_name))
					break;
			}

			if (i >= QMI_TS_MAX_NR) {
				dev_err(dev, "Unknown sensor:%s\n",
					qmi_sens->qmi_name);
				ret = -EINVAL;
				goto data_fetch_err;
			}
			dev_dbg(dev, "QMI sensor:%s available\n", qmi_name);
			qmi_sens->sens_type = i;
			qmi_sens->ts = &ts[idx];
			qmi_sens->dev = dev;
			qmi_sens->last_reading = THERMAL_TEMP_INVALID;
			qmi_sens->high_thresh = INT_MAX;
			qmi_sens->low_thresh = -INT_MAX;
			INIT_WORK(&qmi_sens->therm_notify_work,
					qmi_ts_thresh_notify);
			list_add(&qmi_sens->ts_node, &ts[idx].ts_sensor_list);
		}
		idx++;
	}
	drv->instances = ts;
	drv->count = subsys_cnt;

	return 0;
data_fetch_err:
	of_node_put(subsys_np);
	return ret;
}

static int qmi_sens_device_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0, idx = 0;
	struct qmi_ts_instance *ts;

	struct qmi_ts_driver *drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);

	if (!drv)
		return -ENOMEM;
	platform_set_drvdata(pdev, drv);
	ret = of_get_qmi_ts_platform_data(dev, drv);
	if (ret)
		return ret;

	for (; idx < drv->count; idx++) {
		ts = &drv->instances[idx];
		if (list_empty(&ts->ts_sensor_list)) {
			ret = -ENODEV;
			goto probe_err;
		}
		ret = qmi_handle_init(&ts->handle,
			TS_GET_SENSOR_LIST_RESP_MSG_V02_MAX_MSG_LEN,
			&thermal_qmi_event_ops, handlers);
		if (ret < 0) {
			dev_err(dev, "QMI[0x%x] handle init failed. err:%d\n",
					ts->inst_id, ret);
			
			goto probe_err;
		}
		drv->active++;
		ret = qmi_add_lookup(&ts->handle, TS_SERVICE_ID_V02,
				TS_SERVICE_VERS_V02, ts->inst_id);
		if (ret < 0) {
			dev_err(dev, "QMI register failed for 0x%x, ret:%d\n",
				ts->inst_id, ret);
			goto probe_err;
		}
	}
	atomic_set(&drv->in_suspend, 0);
	drv->pm_nb.notifier_call = qmi_sensor_pm_notify;
	ret = register_pm_notifier(&drv->pm_nb);
	if (!ret)
		return 0;

probe_err:
	qmi_ts_cleanup(drv);
	return ret;
}

static void qmi_sens_device_remove(struct platform_device *pdev)
{
	struct qmi_ts_driver *drv = platform_get_drvdata(pdev);

	unregister_pm_notifier(&drv->pm_nb);
	qmi_ts_cleanup(drv);
}

static const struct of_device_id qmi_sens_device_match[] = {
	{.compatible = "qcom,qmi-sensors"},
	{}
};

MODULE_DEVICE_TABLE(of, qmi_sens_device_match);

static struct platform_driver qmi_sens_device_driver = {
	.probe          = qmi_sens_device_probe,
	.remove         = qmi_sens_device_remove,
	.driver         = {
		.name   = QMI_SENS_DRIVER,
		.of_match_table = qmi_sens_device_match,
	},
};

module_platform_driver(qmi_sens_device_driver);
MODULE_LICENSE("GPL v2");
