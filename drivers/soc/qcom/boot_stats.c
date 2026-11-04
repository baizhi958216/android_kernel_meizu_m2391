// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm bootloader timing in the shared IMEM block and MPM sleep counter. */
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

/* Flyme ABL writes all eight of these 32-kHz timestamps before Linux entry. */
struct kalama_boot_stats {
	u32 bootloader_start;
	u32 bootloader_end;
	u32 boot_start;
	u32 boot_end;
	u32 vendor_boot_start;
	u32 vendor_boot_end;
	u32 init_boot_start;
	u32 init_boot_end;
};

static int kalama_boot_stats_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kalama_boot_stats __iomem *stats;
	struct device_node *imem;
	struct resource res;
	void __iomem *counter;
	u32 freq;
	int ret;

	ret = of_property_read_u32(dev->of_node, "clock-frequency", &freq);
	if (ret || !freq)
		return -EINVAL;
	counter = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(counter))
		return PTR_ERR(counter);

	imem = of_find_compatible_node(NULL, NULL, "qcom,msm-imem-boot_stats");
	if (!imem)
		return -ENODEV;
	ret = of_address_to_resource(imem, 0, &res);
	of_node_put(imem);
	if (ret || resource_size(&res) < sizeof(*stats))
		return -EINVAL;
	stats = devm_ioremap(dev, res.start, sizeof(*stats));
	if (!stats)
		return -ENOMEM;

	dev_info(dev, "KPI: Bootloader start count = %u\n", readl_relaxed(&stats->bootloader_start));
	dev_info(dev, "KPI: Bootloader end count = %u\n", readl_relaxed(&stats->bootloader_end));
	dev_info(dev, "KPI: Bootloader load kernel count = %u\n",
		 readl_relaxed(&stats->boot_end) - readl_relaxed(&stats->boot_start));
	dev_info(dev, "KPI: Kernel MPM timestamp = %u\n", readl_relaxed(counter));
	dev_info(dev, "KPI: Kernel MPM Clock frequency = %u\n", freq);
	return 0;
}

static const struct of_device_id kalama_boot_stats_match[] = {
	{ .compatible = "qcom,mpm2-sleep-counter" },
	{}
};
MODULE_DEVICE_TABLE(of, kalama_boot_stats_match);

static struct platform_driver kalama_boot_stats_driver = {
	.probe = kalama_boot_stats_probe,
	.driver = {
		.name = "qcom-boot-stats",
		.of_match_table = kalama_boot_stats_match,
	},
};
module_platform_driver(kalama_boot_stats_driver);
MODULE_DESCRIPTION("Qualcomm bootloader IMEM and MPM boot timing");
MODULE_LICENSE("GPL");
