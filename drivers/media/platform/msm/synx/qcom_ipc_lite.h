/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_SYNX_IPC_LITE_H
#define _QCOM_SYNX_IPC_LITE_H

#include <linux/types.h>
#include <dt-bindings/mailbox/qcom-ipcc.h>

typedef int (*IPCC_Client)(u32 client_id, u64 data, void *priv);

/**
 * ipc_lite_msg_send() - Sends message to remote client.
 *
 * @client_id  : Identifier for remote client or subsystem.
 * @data       : 64 bit message value.
 * @allow_wait : Whether or not to allow waiting for transport to be
 *               available.
 *
 * @return Zero on successful registration, negative on failure.
 */
int ipc_lite_msg_send(int32_t client_id, uint64_t data, int allow_wait);

/**
 * ipc_lite_register_client() - Registers client callback with framework.
 *
 * @cb_func_ptr : Atomic receive callback, or NULL to unregister and drain it.
 * @priv        : Private data required by client for handling callback.
 *
 * @return Zero on successful registration, negative on failure.
 */
int ipc_lite_register_client(IPCC_Client cb_func_ptr, void *priv);

#endif
