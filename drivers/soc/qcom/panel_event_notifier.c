// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/soc/qcom/panel_event_notifier.h>

struct panel_subscription {
	enum panel_event_notifier_tag tag;
	enum panel_event_notifier_client client;
	struct drm_panel *panel;
	panel_event_notifier_handler handler;
	void *data;
};

static DEFINE_MUTEX(panel_clients_lock);
DEFINE_STATIC_SRCU(panel_clients_srcu);
static struct panel_subscription __rcu *panel_clients[PANEL_EVENT_NOTIFIER_CLIENT_MAX];

void *panel_event_notifier_register(enum panel_event_notifier_tag tag,
		enum panel_event_notifier_client client, struct drm_panel *panel,
		panel_event_notifier_handler handler, void *data)
{
	struct panel_subscription *sub;

	if (tag <= PANEL_EVENT_NOTIFICATION_NONE || tag >= PANEL_EVENT_NOTIFICATION_MAX ||
	    (unsigned int)client >= ARRAY_SIZE(panel_clients) || !handler)
		return ERR_PTR(-EINVAL);

	sub = kmalloc(sizeof(*sub), GFP_KERNEL);
	if (!sub)
		return ERR_PTR(-ENOMEM);

	sub->tag = tag;
	sub->client = client;
	sub->panel = panel;
	sub->handler = handler;
	sub->data = data;
	mutex_lock(&panel_clients_lock);
	if (rcu_access_pointer(panel_clients[client])) {
		mutex_unlock(&panel_clients_lock);
		kfree(sub);
		return ERR_PTR(-EEXIST);
	}
	rcu_assign_pointer(panel_clients[client], sub);
	mutex_unlock(&panel_clients_lock);
	return sub;
}
EXPORT_SYMBOL(panel_event_notifier_register);

void panel_event_notifier_unregister(void *cookie)
{
	struct panel_subscription *sub = cookie;

	if (IS_ERR_OR_NULL(sub))
		return;

	mutex_lock(&panel_clients_lock);
	RCU_INIT_POINTER(panel_clients[sub->client], NULL);
	mutex_unlock(&panel_clients_lock);

	/* A client can unload or free callback data as soon as we return. */
	synchronize_srcu(&panel_clients_srcu);
	kfree(sub);
}
EXPORT_SYMBOL(panel_event_notifier_unregister);

void panel_event_notification_trigger(enum panel_event_notifier_tag tag,
				      struct panel_event_notification *notification)
{
	struct panel_subscription *sub;
	unsigned int i;
	int index;

	if (!notification || tag <= PANEL_EVENT_NOTIFICATION_NONE ||
	    tag >= PANEL_EVENT_NOTIFICATION_MAX)
		return;

	index = srcu_read_lock(&panel_clients_srcu);
	for (i = 0; i < ARRAY_SIZE(panel_clients); i++) {
		sub = srcu_dereference(panel_clients[i], &panel_clients_srcu);
		/* SmartSwap follows the primary display across panel selection. */
		if (sub && sub->tag == tag && (sub->panel == notification->panel ||
		    (sub->client == PANEL_EVENT_NOTIFIER_CLIENT_SMARTSWAP && !sub->panel)))
			sub->handler(tag, notification, sub->data);
	}
	srcu_read_unlock(&panel_clients_srcu, index);
}
EXPORT_SYMBOL(panel_event_notification_trigger);

MODULE_DESCRIPTION("Qualcomm display panel event notifier");
MODULE_LICENSE("GPL");
