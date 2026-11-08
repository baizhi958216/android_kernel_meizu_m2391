// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm CPUCP performance-lock sysfs interface. */
#include <linux/module.h>
#include <linux/scmi_plh.h>
#include <linux/scmi_protocol.h>
#include <linux/slab.h>

struct plh_state {
	bool initialized;
	u16 active, sample_ms, log_level;
};
struct plh_client {
	struct scmi_protocol_handle *ph;
	const struct plh_ops *ops;
	struct plh_state state[2];
};
struct plh_attr {
	struct kobj_attribute kattr;
	enum plh_feature feature;
	enum plh_action action;
};
static DEFINE_MUTEX(plh_lock);
static struct plh_client *client;

static int parse_table(char *input, enum plh_feature feature, u16 *words)
{
	bool scroll = feature == PLH_SCROLL;
	char *section;
	u16 groups, used, pairs = 0;

	if (kstrtou16(strsep(&input, ":"), 10, &groups) || !groups || groups > (scroll ? 8 : 4))
		return -EINVAL;
	words[0] = groups;
	used = scroll ? 2 : 1;	/* Scroll uses one pair count common to every FPS. */
	for (u16 i = 0; i < groups; i++) {
		u16 row[11], elements = 0, row_pairs;
		char *token;

		section = strsep(&input, ":");
		if (!section)
			return -EINVAL;
		while ((token = strsep(&section, ","))) {
			if (elements >= ARRAY_SIZE(row) || kstrtou16(token, 10, &row[elements]))
				return -EINVAL;
			elements++;
		}
		/* Group key followed by 1..5 IPC/frequency pairs. */
		if (elements < 3 || !(elements & 1))
			return -EINVAL;
		row_pairs = elements / 2;
		if (scroll && i && pairs != row_pairs)
			return -EINVAL;
		pairs = row_pairs;
		if (used + elements + !scroll > PLH_MAX_WORDS)
			return -E2BIG;
		words[used++] = row[0];
		if (!scroll)
			words[used++] = row_pairs;
		memcpy(&words[used], &row[1], (elements - 1) * sizeof(u16));
		used += elements - 1;
	}
	if (input)
		return -EINVAL;
	if (scroll)
		words[1] = pairs;
	return used;
}

static ssize_t plh_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct plh_attr *item = container_of(attr, struct plh_attr, kattr);
	struct plh_state *state;
	u16 value;

	guard(mutex)(&plh_lock);
	if (!client)
		return -ENODEV;
	state = &client->state[item->feature];
	switch (item->action) {
	case PLH_SAMPLE: value = state->sample_ms; break;
	case PLH_LOG: value = state->log_level; break;
	default: value = state->active; break;
	}
	return sysfs_emit(buf, "%u\n", value);
}

static ssize_t plh_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	struct plh_attr *item = container_of(attr, struct plh_attr, kattr);
	enum plh_action action = item->action;
	struct plh_state *state;
	char *input __free(kfree) = kstrndup(buf, count, GFP_KERNEL);
	u16 words[PLH_MAX_WORDS];
	int ret, size = 1;

	if (!input)
		return -ENOMEM;
	if (action == PLH_START && str_has_prefix(input, "Init:")) {
		size = parse_table(strim(input + 5), item->feature, words);
		if (size < 0)
			return size;
		action = PLH_TABLE;
	} else {
		ret = kstrtou16(strim(input), 10, words);
		if (ret)
			return ret;
		if (action == PLH_SAMPLE)
			words[0] = clamp_t(u16, words[0], 1, 30);
		else if (action == PLH_LOG)
			words[0] = min_t(u16, words[0], 15);
		else if (!words[0])
			action = PLH_STOP;
	}

	guard(mutex)(&plh_lock);
	if (!client)
		return -ENODEV;
	state = &client->state[item->feature];
	if ((action == PLH_START || action == PLH_STOP) && !state->initialized)
		return -EINVAL;
	ret = client->ops->command(client->ph, item->feature, action, words, size);
	if (ret)
		return ret;
	switch (action) {
	case PLH_TABLE: state->initialized = true; break;
	case PLH_SAMPLE: state->sample_ms = words[0]; break;
	case PLH_LOG: state->log_level = words[0]; break;
	default: state->active = words[0]; break;
	}
	return count;
}

#define PLH_ATTR(name, id, command) static struct plh_attr name = { \
	.kattr = __ATTR(name, 0644, plh_show, plh_store), .feature = id, .action = command }

PLH_ATTR(splh_notif, PLH_SCROLL, PLH_START);
PLH_ATTR(splh_sample_ms, PLH_SCROLL, PLH_SAMPLE);
PLH_ATTR(splh_log_level, PLH_SCROLL, PLH_LOG);
PLH_ATTR(lplh_notif, PLH_LAUNCH, PLH_START);
PLH_ATTR(lplh_sample_ms, PLH_LAUNCH, PLH_SAMPLE);
PLH_ATTR(lplh_log_level, PLH_LAUNCH, PLH_LOG);

static struct attribute *plh_attrs[] = {
	&splh_notif.kattr.attr, &splh_sample_ms.kattr.attr, &splh_log_level.kattr.attr,
	&lplh_notif.kattr.attr, &lplh_sample_ms.kattr.attr, &lplh_log_level.kattr.attr, NULL,
};
static const struct attribute_group plh_group = { .name = "parameters", .attrs = plh_attrs };

static int plh_probe(struct scmi_device *sdev)
{
	struct plh_client *instance;
	int ret;

	instance = devm_kzalloc(&sdev->dev, sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return -ENOMEM;
	instance->ops = sdev->handle->devm_protocol_get(sdev, QCOM_SCMI_PLH, &instance->ph);
	if (IS_ERR(instance->ops))
		return PTR_ERR(instance->ops);
	mutex_lock(&plh_lock);
	if (client) {
		mutex_unlock(&plh_lock);
		return -EBUSY;
	}
	client = instance;
	mutex_unlock(&plh_lock);
	ret = sysfs_merge_group(msm_perf_kobject(), &plh_group);
	if (ret) {
		guard(mutex)(&plh_lock);
		client = NULL;
	}
	return ret;
}

static void plh_remove(struct scmi_device *sdev)
{
	u16 stop = 0;

	sysfs_unmerge_group(msm_perf_kobject(), &plh_group);
	guard(mutex)(&plh_lock);
	for (int i = PLH_SCROLL; i <= PLH_LAUNCH; i++)
		if (client->state[i].active)
			client->ops->command(client->ph, i, PLH_STOP, &stop, 1);
	client = NULL;
}
static const struct scmi_device_id plh_ids[] = {
	{ QCOM_SCMI_PLH, "scmi_protocol_plh" }, {}
};
MODULE_DEVICE_TABLE(scmi, plh_ids);
static struct scmi_driver plh_driver = {
	.name = "scmi-plh-driver", .probe = plh_probe, .remove = plh_remove, .id_table = plh_ids,
};
module_scmi_driver(plh_driver);
MODULE_SOFTDEP("pre: plh_vendor");
MODULE_DESCRIPTION("Qualcomm CPUCP scroll and launch performance locks");
MODULE_LICENSE("GPL");
