/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_RPMSG_QCOM_GLINK_H
#define _LINUX_RPMSG_QCOM_GLINK_H

#include <linux/device.h>

struct rpmsg_endpoint;
int qcom_glink_get_signals(struct rpmsg_endpoint *ept);
int qcom_glink_update_signals(struct rpmsg_endpoint *ept, u32 set, u32 clear);

struct qcom_glink_smem;
struct qcom_glink_window;

/* Coherent memory shared with MPSS, valid until unregister returns. */
struct qcom_glink_window *qcom_glink_register_modem_window(void *cpu,
							   phys_addr_t phys, size_t size);
void qcom_glink_unregister_modem_window(struct qcom_glink_window *window);

#if IS_ENABLED(CONFIG_RPMSG_QCOM_GLINK)
void qcom_glink_ssr_notify(const char *ssr_name);
#else
static inline void qcom_glink_ssr_notify(const char *ssr_name) {}
#endif

#if IS_ENABLED(CONFIG_RPMSG_QCOM_GLINK_SMEM)

struct qcom_glink_smem *qcom_glink_smem_register(struct device *parent,
						 struct device_node *node);
void qcom_glink_smem_unregister(struct qcom_glink_smem *glink);

#else

static inline struct qcom_glink_smem *
qcom_glink_smem_register(struct device *parent,
			 struct device_node *node)
{
	return NULL;
}

static inline void qcom_glink_smem_unregister(struct qcom_glink_smem *glink) {}
#endif

#endif
