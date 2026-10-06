// SPDX-License-Identifier: GPL-2.0-only
/* Boot-time supply votes on Qualcomm downstream device trees. */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/proxy-consumer.h>

struct proxy_vote {
	struct regulator *supply;
	bool voltage;
	bool load;
	bool enabled;
};

static void proxy_release(struct device *dev, void *data)
{
	struct proxy_vote *vote = data;

	if (vote->enabled && regulator_disable(vote->supply))
		dev_err(dev, "failed to disable proxy supply\n");
	if (vote->load)
		regulator_set_load(vote->supply, 0);
	if (vote->voltage)
		regulator_set_voltage(vote->supply, 0, INT_MAX);
	regulator_put(vote->supply);
}

int devm_regulator_proxy_consumer_register(struct device *dev, struct device_node *node)
{
	bool enable = of_property_read_bool(node, "qcom,proxy-consumer-enable");
	bool voltage = of_property_present(node, "qcom,proxy-consumer-voltage");
	bool load = of_property_present(node, "qcom,proxy-consumer-current");
	const char *name = "proxy";
	struct proxy_vote *vote;
	u32 uv[2], ua;
	int ret;

	if (!enable && !voltage && !load)
		return 0;
	if (voltage && (of_property_read_u32_array(node, "qcom,proxy-consumer-voltage",
						   uv, 2) ||
			uv[0] > uv[1] || uv[1] > INT_MAX))
		return -EINVAL;
	if (load && (of_property_read_u32(node, "qcom,proxy-consumer-current", &ua) ||
		     ua > INT_MAX))
		return -EINVAL;
	if (of_property_present(node, "qcom,proxy-consumer-name") &&
	    of_property_read_string(node, "qcom,proxy-consumer-name", &name))
		return -EINVAL;

	vote = devres_alloc(proxy_release, sizeof(*vote), GFP_KERNEL);
	if (!vote)
		return -ENOMEM;
	vote->supply = regulator_get_optional(dev, name);
	if (IS_ERR(vote->supply)) {
		ret = PTR_ERR(vote->supply);
		goto free_vote;
	}
	if (voltage) {
		ret = regulator_set_voltage(vote->supply, uv[0], uv[1]);
		if (ret)
			goto unwind;
		vote->voltage = true;
	}
	if (load) {
		ret = regulator_set_load(vote->supply, ua);
		if (ret < 0)
			goto unwind;
		vote->load = true;
	}
	if (enable) {
		ret = regulator_enable(vote->supply);
		if (ret)
			goto unwind;
		vote->enabled = true;
	}
	devres_add(dev, vote);
	return 0;

unwind:
	proxy_release(dev, vote);
free_vote:
	devres_free(vote);
	return ret;
}
EXPORT_SYMBOL_GPL(devm_regulator_proxy_consumer_register);

void regulator_proxy_consumer_sync_state(struct device *dev)
{
	/* Each devres entry represents one temporary vote on this provider. */
	while (!devres_release(dev, proxy_release, NULL, NULL))
		;
}
EXPORT_SYMBOL_GPL(regulator_proxy_consumer_sync_state);

MODULE_DESCRIPTION("Qualcomm boot-time regulator proxy votes");
MODULE_LICENSE("GPL");
