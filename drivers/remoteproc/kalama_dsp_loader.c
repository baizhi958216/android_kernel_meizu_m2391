// SPDX-License-Identifier: GPL-2.0-only
/* Android starts the Kalama DSPs after mounting the modem firmware partition. */
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

struct kalama_dsp_loader {
	struct kobject kobj;
	struct rproc *rproc;
	struct work_struct boot_work;
	struct mutex command_lock;
	bool held;
};

static void kalama_dsp_boot(struct work_struct *work)
{
	struct kalama_dsp_loader *loader = container_of(work, typeof(*loader), boot_work);
	int ret;

	if (loader->held)
		return;
	ret = rproc_boot(loader->rproc);
	if (!ret)
		loader->held = true;
	else
		dev_err(&loader->rproc->dev, "Android DSP boot failed: %d\n", ret);
}

/* Called under command_lock. Synchronize with the implicit firmware request
 * before releasing this loader's single reference to the remote processor.
 */
static int kalama_dsp_stop(struct kalama_dsp_loader *loader)
{
	int ret = 0;

	cancel_work_sync(&loader->boot_work);
	if (loader->held) {
		ret = rproc_shutdown(loader->rproc);
		if (!ret)
			loader->held = false;
	}
	return ret;
}

static ssize_t kalama_dsp_command(struct kobject *kobj, struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct kalama_dsp_loader *loader = container_of(kobj, typeof(*loader), kobj);
	bool restart = !strcmp(attr->attr.name, "ssr");
	unsigned int command;
	int ret;

	ret = kstrtouint(buf, 10, &command);
	if (ret)
		return ret;
	if (command > 1 || (restart && !command))
		return -EINVAL;

	mutex_lock(&loader->command_lock);
	if (restart || !command)
		ret = kalama_dsp_stop(loader);
	if (!ret && command)
		schedule_work(&loader->boot_work);
	mutex_unlock(&loader->command_lock);
	return ret ?: count;
}

static struct kobj_attribute boot_attr = __ATTR(boot, 0220, NULL, kalama_dsp_command);
static struct kobj_attribute ssr_attr = __ATTR(ssr, 0220, NULL, kalama_dsp_command);

static struct attribute *adsp_attrs[] = { &boot_attr.attr, &ssr_attr.attr, NULL };
static const struct attribute_group adsp_group = { .attrs = adsp_attrs };
static struct attribute *cdsp_attrs[] = { &boot_attr.attr, NULL };
static const struct attribute_group cdsp_group = { .attrs = cdsp_attrs };

static void kalama_loader_release(struct kobject *kobj)
{
	struct kalama_dsp_loader *loader = container_of(kobj, typeof(*loader), kobj);

	rproc_put(loader->rproc);
	kfree(loader);
}

static const struct kobj_type loader_ktype = {
	.release = kalama_loader_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

static int kalama_loader_probe(struct platform_device *pdev)
{
	const struct attribute_group *group = device_get_match_data(&pdev->dev);
	struct kalama_dsp_loader *loader;
	struct rproc *rproc;
	u32 handle;
	int ret;

	if (!of_machine_is_compatible("qcom,kalama"))
		return -ENODEV;
	ret = of_property_read_u32(pdev->dev.of_node, "qcom,rproc-handle", &handle);
	if (ret)
		return ret;
	rproc = rproc_get_by_phandle(handle);
	if (!rproc)
		return -EPROBE_DEFER;
	loader = kzalloc(sizeof(*loader), GFP_KERNEL);
	if (!loader) {
		rproc_put(rproc);
		return -ENOMEM;
	}
	loader->rproc = rproc;
	mutex_init(&loader->command_lock);
	INIT_WORK(&loader->boot_work, kalama_dsp_boot);
	ret = kobject_init_and_add(&loader->kobj, &loader_ktype, kernel_kobj, "boot_%s",
				   group == &adsp_group ? "adsp" : "cdsp");
	if (ret)
		goto put;
	ret = sysfs_create_group(&loader->kobj, group);
	if (ret) {
		kobject_del(&loader->kobj);
		goto put;
	}
	platform_set_drvdata(pdev, loader);
	return 0;
put:
	kobject_put(&loader->kobj);
	return ret;
}

static void kalama_loader_remove(struct platform_device *pdev)
{
	struct kalama_dsp_loader *loader = platform_get_drvdata(pdev);

	sysfs_remove_group(&loader->kobj, device_get_match_data(&pdev->dev));
	kalama_dsp_stop(loader);
	kobject_del(&loader->kobj);
	kobject_put(&loader->kobj);
}

static const struct of_device_id kalama_loader_match[] = {
	{ .compatible = "qcom,adsp-loader", .data = &adsp_group },
	{ .compatible = "qcom,cdsp-loader", .data = &cdsp_group },
	{}
};
MODULE_DEVICE_TABLE(of, kalama_loader_match);

static struct platform_driver kalama_loader_driver = {
	.probe = kalama_loader_probe,
	.remove = kalama_loader_remove,
	.driver = {
		.name = "kalama-dsp-loader",
		.of_match_table = kalama_loader_match,
	},
};
module_platform_driver(kalama_loader_driver);
MODULE_DESCRIPTION("Android lifecycle control for Kalama ADSP and CDSP");
MODULE_LICENSE("GPL");
