/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __ALTMODE_H__
#define __ALTMODE_H__

#include <linux/types.h>

/**
 * struct altmode_client_data
 *	Uniquely define altmode client while registering with altmode framework.
 *
 * @svid:		Unique ID for Type-C Altmode client devices
 * @name:		Short descriptive name to identify client
 * @priv:		Pointer to client driver's internal top level structure
 * @callback:		Callback function to receive PMIC GLINK message data
 */
struct altmode_client_data {
	u16		svid;
	const char	*name;
	void		*priv;
	int		(*callback)(void *priv, void *data, size_t len);
};

struct altmode_client;

struct device;

struct altmode_client *altmode_register_client(struct device *dev,
		const struct altmode_client_data *client_data);
void altmode_deregister_client(struct altmode_client *client);

#endif
