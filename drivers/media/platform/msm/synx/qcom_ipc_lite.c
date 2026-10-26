// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/unaligned.h>

#include "qcom_ipc_lite.h"

#define IPCLITE_GLINK_GUID      "ipcliteglink-apps-dsp"


static DEFINE_MUTEX(channel_lock);
static DEFINE_SPINLOCK(callback_lock);
static struct rpmsg_device *channel;
static IPCC_Client client_callback;
static void *client_priv;

int ipc_lite_register_client(IPCC_Client cb_func_ptr, void *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&callback_lock, flags);
	client_callback = cb_func_ptr;
	client_priv = priv;
	spin_unlock_irqrestore(&callback_lock, flags);
	return 0;
}
EXPORT_SYMBOL(ipc_lite_register_client);

int ipc_lite_msg_send(int32_t client_id, uint64_t data, int allow_wait)
{
	int ret;

	if (client_id != IPCC_CLIENT_CDSP)
		return -EINVAL;
	if (allow_wait)
		mutex_lock(&channel_lock);
	else if (!mutex_trylock(&channel_lock))
		return -EAGAIN;

	if (!channel)
		ret = -ENODEV;
	else if (allow_wait)
		ret = rpmsg_send(channel->ept, &data, sizeof(data));
	else
		ret = rpmsg_trysend(channel->ept, &data, sizeof(data));
	mutex_unlock(&channel_lock);
	return ret;
}
EXPORT_SYMBOL(ipc_lite_msg_send);

static int ipc_lite_rpmsg_callback(struct rpmsg_device *rpdev,
	void *data, int len, void *priv, u32 addr)
{
	unsigned long flags;
	int ret = 0;

	if (len != sizeof(u64))
		return -EMSGSIZE;
	spin_lock_irqsave(&callback_lock, flags);
	if (client_callback)
		ret = client_callback(IPCC_CLIENT_CDSP, get_unaligned_le64(data), client_priv);
	spin_unlock_irqrestore(&callback_lock, flags);
	return ret;
}

static int ipc_lite_rpmsg_probe(struct rpmsg_device *rpdev)
{
	int ret = 0;

	mutex_lock(&channel_lock);
	if (channel)
		ret = -EBUSY;
	else
		channel = rpdev;
	mutex_unlock(&channel_lock);
	return ret;
}

static void ipc_lite_rpmsg_remove(struct rpmsg_device *rpdev)
{
	mutex_lock(&channel_lock);
	channel = NULL;
	mutex_unlock(&channel_lock);
}

static const struct rpmsg_device_id ipc_lite_rpmsg_id_table[] = {
	{ IPCLITE_GLINK_GUID },
	{ },
};

MODULE_DEVICE_TABLE(rpmsg, ipc_lite_rpmsg_id_table);

static struct rpmsg_driver ipc_lite_rpmsg_client = {
	.id_table = ipc_lite_rpmsg_id_table,
	.probe = ipc_lite_rpmsg_probe,
	.remove = ipc_lite_rpmsg_remove,
	.callback = ipc_lite_rpmsg_callback,
	.drv = {
		.name = "qcom,msm_ipclite_rpmsg",
	},
};

module_rpmsg_driver(ipc_lite_rpmsg_client);

MODULE_DESCRIPTION("Qualcomm AP to CDSP Synx fence transport");
MODULE_LICENSE("GPL v2");
