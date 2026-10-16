/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _QCOM_SYSTEM_HEAP_H
#define _QCOM_SYSTEM_HEAP_H

#include <linux/dma-heap.h>
#include <linux/err.h>
#include "qcom_dynamic_page_pool.h"

struct qcom_system_heap {
	int uncached;
	struct dynamic_page_pool **pool_list;
};

int qcom_system_heap_create(const char *name, bool uncached);
int qcom_system_heap_create_alias(void);
struct page *qcom_sys_heap_alloc_largest_available(struct dynamic_page_pool **pools,
						   unsigned long size,
						   unsigned int max_order);

#endif /* _QCOM_SYSTEM_HEAP_H */
