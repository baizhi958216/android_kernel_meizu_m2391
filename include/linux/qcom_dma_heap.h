/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _QCOM_DMA_HEAP_H
#define _QCOM_DMA_HEAP_H

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/dma-buf.h>

#include <linux/qcom_dma_heap_dt_constants.h>
#include <linux/err.h>

/* Keep an idle DMA mapping reusable until the importer detaches. */
#define QCOM_DMA_ATTR_DELAYED_UNMAP	BIT(17)

/* Heap flags */
#define QCOM_DMA_HEAP_FLAG_CACHED	BIT(1)

#define QCOM_DMA_HEAP_FLAG_CP_TRUSTED_VM		BIT(15)
#define QCOM_DMA_HEAP_FLAG_CP_TZ		BIT(16)
#define QCOM_DMA_HEAP_FLAG_CP_TOUCH		BIT(17)
#define QCOM_DMA_HEAP_FLAG_CP_BITSTREAM		BIT(18)
#define QCOM_DMA_HEAP_FLAG_CP_PIXEL		BIT(19)
#define QCOM_DMA_HEAP_FLAG_CP_NON_PIXEL		BIT(20)
#define QCOM_DMA_HEAP_FLAG_CP_CAMERA		BIT(21)
#define QCOM_DMA_HEAP_FLAG_CP_HLOS		BIT(22)
#define QCOM_DMA_HEAP_FLAG_CP_SPSS_SP		BIT(23)
#define QCOM_DMA_HEAP_FLAG_CP_SPSS_SP_SHARED	BIT(24)
#define QCOM_DMA_HEAP_FLAG_CP_SEC_DISPLAY	BIT(25)
#define QCOM_DMA_HEAP_FLAG_CP_APP		BIT(26)
#define QCOM_DMA_HEAP_FLAG_CP_CAMERA_PREVIEW	BIT(27)
#define QCOM_DMA_HEAP_FLAG_CP_MSS_MSA		BIT(28)
#define QCOM_DMA_HEAP_FLAG_CP_CDSP		BIT(29)
#define QCOM_DMA_HEAP_FLAG_CP_SPSS_HLOS_SHARED	BIT(30)

#define QCOM_DMA_HEAP_FLAGS_CP_MASK	GENMASK(30, 15)

#define QCOM_DMA_HEAP_FLAG_SECURE	BIT(31)

struct dma_buf_heap_prefetch_region {
	u64 size;
	struct dma_heap *heap;
};

int qcom_secure_system_heap_prefetch(struct dma_buf_heap_prefetch_region *regions,
				     size_t nr_regions);

int qcom_secure_system_heap_drain(struct dma_buf_heap_prefetch_region *regions,
				  size_t nr_regions);

#endif /* _QCOM_DMA_HEAP_H */
