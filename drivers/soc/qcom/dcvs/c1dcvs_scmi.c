// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm CPUCP C1DCVS policy controls. */
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/scmi_c1dcvs.h>
#include <linux/scmi_protocol.h>
#include <linux/string_helpers.h>

struct c1dcvs {
	struct scmi_protocol_handle *ph;
	const struct c1dcvs_ops *ops;
	struct device *root;
	bool user_enable, kernel_enable;
};

static DEFINE_MUTEX(c1_lock);
static struct c1dcvs *current_policy;

static int set_enable(struct c1dcvs *policy, bool kernel, bool user)
{
	u32 value = kernel && user;
	int ret = policy->ops->write(policy->ph, C1DCVS_ENABLE, &value, 1);

	if (!ret) {
		policy->kernel_enable = kernel;
		policy->user_enable = user;
	}
	return ret;
}

int c1dcvs_enable(bool enable)
{
	guard(mutex)(&c1_lock);
	if (!current_policy)
		return -EPROBE_DEFER;
	return set_enable(current_policy, enable, current_policy->user_enable);
}
EXPORT_SYMBOL_GPL(c1dcvs_enable);

struct c1_attr {
	struct kobj_attribute kattr;
	enum c1dcvs_setting setting;
	bool per_cluster;
};

static ssize_t setting_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct c1_attr *item = container_of(attr, struct c1_attr, kattr);
	u32 values[C1DCVS_MAX_VALUES];
	ssize_t len = 0;
	int n;

	guard(mutex)(&c1_lock);
	if (!current_policy)
		return -ENODEV;
	n = current_policy->ops->read(current_policy->ph, item->setting, values, ARRAY_SIZE(values));
	if (n < 0)
		return n;
	if (!item->per_cluster)
		return sysfs_emit(buf, "%u\n", values[0]);
	for (int i = 0; i < n && values[i]; i++)
		len += sysfs_emit_at(buf, len, "%u\t", values[i]);
	return len + sysfs_emit_at(buf, len, "\n");
}

static ssize_t setting_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	struct c1_attr *item = container_of(attr, struct c1_attr, kattr);
	size_t words = item->per_cluster ? 2 : 1;
	char **argv;
	u32 values[2];
	int argc, ret = -EINVAL;

	argv = argv_split(GFP_KERNEL, buf, &argc);
	if (!argv)
		return -ENOMEM;
	if (argc == words) {
		for (int i = 0; i < argc; i++) {
			ret = kstrtou32(argv[i], 10, &values[i]);
			if (ret)
				break;
		}
	}
	argv_free(argv);
	if (ret)
		return ret;

	guard(mutex)(&c1_lock);
	if (!current_policy)
		return -ENODEV;
	if (item->setting == C1DCVS_ENABLE)
		ret = set_enable(current_policy, current_policy->kernel_enable, !!values[0]);
	else
		ret = current_policy->ops->write(current_policy->ph, item->setting, values, words);
	return ret ? ret : count;
}

#define C1_ATTR(name, id, cluster) static struct c1_attr name = { \
	.kattr = __ATTR(name, 0644, setting_show, setting_store), .setting = id, .per_cluster = cluster }

C1_ATTR(enable_c1dcvs, C1DCVS_ENABLE, false);
C1_ATTR(enable_trace, C1DCVS_TRACE, false);
C1_ATTR(ipc_thresh, C1DCVS_IPC, true);
C1_ATTR(efreq_thresh, C1DCVS_EFREQ, true);
C1_ATTR(hysteresis, C1DCVS_HYSTERESIS, false);
C1_ATTR(c1dcvs_opt_mode, C1DCVS_OPT_MODE, false);

static struct attribute *c1_attrs[] = {
	&enable_c1dcvs.kattr.attr, &enable_trace.kattr.attr,
	&ipc_thresh.kattr.attr, &efreq_thresh.kattr.attr,
	&hysteresis.kattr.attr, &c1dcvs_opt_mode.kattr.attr, NULL,
};
static const struct attribute_group c1_group = { .name = "c1dcvs", .attrs = c1_attrs };

static int c1_probe(struct scmi_device *sdev)
{
	struct c1dcvs *policy;
	int ret;

	policy = devm_kzalloc(&sdev->dev, sizeof(*policy), GFP_KERNEL);
	if (!policy)
		return -ENOMEM;
	policy->ops = sdev->handle->devm_protocol_get(sdev, QCOM_SCMI_C1DCVS, &policy->ph);
	if (IS_ERR(policy->ops))
		return PTR_ERR(policy->ops);
	policy->root = bus_get_dev_root(&cpu_subsys);
	if (!policy->root)
		return -ENODEV;
	mutex_lock(&c1_lock);
	if (current_policy) {
		mutex_unlock(&c1_lock);
		put_device(policy->root);
		return -EBUSY;
	}
	policy->user_enable = policy->kernel_enable = true;
	current_policy = policy;
	mutex_unlock(&c1_lock);
	ret = sysfs_create_group(&policy->root->kobj, &c1_group);
	if (ret) {
		mutex_lock(&c1_lock);
		current_policy = NULL;
		mutex_unlock(&c1_lock);
		put_device(policy->root);
	}
	dev_set_drvdata(&sdev->dev, policy);
	return ret;
}

static void c1_remove(struct scmi_device *sdev)
{
	struct c1dcvs *policy = dev_get_drvdata(&sdev->dev);

	sysfs_remove_group(&policy->root->kobj, &c1_group);
	mutex_lock(&c1_lock);
	current_policy = NULL;
	mutex_unlock(&c1_lock);
	put_device(policy->root);
}

static const struct scmi_device_id c1_ids[] = {
	{ QCOM_SCMI_C1DCVS, "scmi_c1dcvs_protocol" }, {}
};
MODULE_DEVICE_TABLE(scmi, c1_ids);
static struct scmi_driver c1_driver = {
	.name = "scmi-c1dcvs-driver", .probe = c1_probe, .remove = c1_remove, .id_table = c1_ids,
};
module_scmi_driver(c1_driver);
MODULE_SOFTDEP("pre: c1dcvs_vendor");
MODULE_DESCRIPTION("Qualcomm CPUCP C1DCVS policy controls");
MODULE_LICENSE("GPL");
