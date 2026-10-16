/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _QCOM_SECURE_SYSTEM_HEAP_H
#define _QCOM_SECURE_SYSTEM_HEAP_H

#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/types.h>
#include "qcom_dynamic_page_pool.h"

struct qcom_secure_system_heap {
	struct dynamic_page_pool **pool_list;
	int vmid;
	struct list_head list;
	atomic_long_t total_allocated;
};

int qcom_secure_system_heap_create(const char *name, int vmid);
int qcom_secure_system_heap_freeze(void);
int qcom_secure_system_heap_restore(void);

#endif /* _QCOM_SECURE_SYSTEM_HEAP_H */
