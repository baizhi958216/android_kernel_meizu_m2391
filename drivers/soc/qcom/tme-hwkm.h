/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _QCOM_TME_HWKM_H
#define _QCOM_TME_HWKM_H

#include <linux/errno.h>
#include <linux/kconfig.h>

#if IS_ENABLED(CONFIG_QCOM_KALAMA_HWKM)
int qcom_tme_hwkm_broadcast(void);
#else
static inline int qcom_tme_hwkm_broadcast(void) { return -EOPNOTSUPP; }
#endif

#endif
