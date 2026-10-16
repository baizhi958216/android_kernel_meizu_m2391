// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/err.h>

#include <linux/qcom_dma_heap.h>
#include <linux/mem-buf.h>
#include "deferred-free-helper.h"
#include "qcom_cma_heap.h"
#include "qcom_dt_parser.h"
#include "qcom_system_heap.h"
#include "qcom_carveout_heap.h"
#include "qcom_secure_system_heap.h"
#include "qcom_bitstream_contig_heap.h"

static bool heap_registered(const char *name)
{
	struct dma_heap *heap = dma_heap_find(name);

	if (!heap)
		return false;
	dma_heap_put(heap);
	return true;
}

static int qcom_dma_heap_probe(struct platform_device *pdev)
{
	static struct platform_data *heaps;
	int ret, i;

	if (mem_buf_current_vmid() < 0)
		return -EPROBE_DEFER;

	/* These heaps live for the life of the system. Resume a partial probe
	 * without creating child devices twice or reassigning protected pages.
	 */
	if (!heaps) {
		struct platform_data *data = parse_heap_dt(pdev);

		if (IS_ERR(data))
			return PTR_ERR(data);
		heaps = data;
	}
	if (!heap_registered("qcom,system")) {
		ret = qcom_system_heap_create("qcom,system", false);
		if (ret)
			return ret;
	}
	if (!heap_registered("system")) {
		ret = qcom_system_heap_create_alias();
		if (ret)
			return ret;
	}
	if (!heap_registered("qcom,secure-pixel")) {
		ret = qcom_secure_system_heap_create("qcom,secure-pixel",
						    QCOM_DMA_HEAP_FLAG_CP_PIXEL);
		if (ret)
			return ret;
	}
	if (!heap_registered("qcom,secure-non-pixel")) {
		ret = qcom_secure_system_heap_create("qcom,secure-non-pixel",
						    QCOM_DMA_HEAP_FLAG_CP_NON_PIXEL);
		if (ret)
			return ret;
	}
	for (i = 0; i < heaps->nr; i++) {
		struct platform_heap *data = &heaps->heaps[i];

		if (heap_registered(data->name))
			continue;
		switch (data->type) {
		case HEAP_TYPE_SECURE_CARVEOUT:
			ret = qcom_secure_carveout_heap_create(data);
			break;
		case HEAP_TYPE_CARVEOUT:
			ret = qcom_carveout_heap_create(data);
			break;
		case HEAP_TYPE_CMA:
			ret = qcom_add_cma_heap(data);
			break;
		default:
			ret = -EOPNOTSUPP;
		}
		if (ret)
			return dev_err_probe(&pdev->dev, ret, "creating %s\n", data->name);
	}
	return qcom_add_bitstream_contig_heap("system-secure");
}

static int qcom_dma_heaps_freeze(struct device *dev)
{
	int ret;

	ret = qcom_secure_carveout_heap_freeze();
	if (ret) {
		pr_err("Failed to freeze secure carveout heap: %d\n", ret);
		return ret;
	}

	ret = qcom_secure_system_heap_freeze();
	if (ret) {
		pr_err("Failed to freeze secure system heap: %d\n", ret);
		goto err;
	}

	return 0;
err:
	ret = qcom_secure_carveout_heap_restore();
	if (ret) {
		pr_err("Failed to restore secure carveout heap: %d\n", ret);
		return ret;
	}
	return -EBUSY;
}

static int qcom_dma_heaps_restore(struct device *dev)
{
	int ret;

	ret = qcom_secure_carveout_heap_restore();
	if (ret)
		return ret;

	ret = qcom_secure_system_heap_restore();
	if (ret)
		pr_err("Failed to restore secure system heap: %d\n", ret);

	return ret;
}

static const struct dev_pm_ops qcom_dma_heaps_pm_ops = {
	.freeze_late = qcom_dma_heaps_freeze,
	.restore_early = qcom_dma_heaps_restore,
};

static const struct of_device_id qcom_dma_heap_match_table[] = {
	{.compatible = "qcom,dma-heaps"},
	{},
};

MODULE_DEVICE_TABLE(of, qcom_dma_heap_match_table);

static struct platform_driver qcom_dma_heap_driver = {
	.probe = qcom_dma_heap_probe,
	.driver = {
		.name = "qcom-dma-heap",
		.suppress_bind_attrs = true,
		.of_match_table = qcom_dma_heap_match_table,
		.pm = &qcom_dma_heaps_pm_ops,
	},
};

static int __init init_heap_driver(void)
{
	int ret = deferred_freelist_init();

	if (ret)
		return ret;
	ret = platform_driver_register(&qcom_dma_heap_driver);
	if (ret)
		deferred_freelist_exit();
	return ret;
}
module_init(init_heap_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm Kalama multimedia DMA-BUF heaps");
MODULE_IMPORT_NS("DMA_BUF");
MODULE_IMPORT_NS("DMA_BUF_HEAP");
