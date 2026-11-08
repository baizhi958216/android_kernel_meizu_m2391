// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm CPUCP scroll and application-launch performance voting. */
#include <linux/scmi_plh.h>
#include "common.h"

static int plh_command(const struct scmi_protocol_handle *ph, enum plh_feature feature,
		       enum plh_action action, const u16 *args, size_t count)
{
	struct scmi_xfer *xfer;
	__le16 *payload;
	unsigned int msg = (feature == PLH_SCROLL ? 16 : 32) + action;
	int ret;

	if (feature > PLH_LAUNCH || action > PLH_LOG || !count || count > PLH_MAX_WORDS)
		return -EINVAL;
	ret = ph->xops->xfer_get_init(ph, msg, ALIGN(count * sizeof(*payload), 4),
				      sizeof(__le32), &xfer);
	if (ret)
		return ret;
	payload = xfer->tx.buf;
	memset(payload, 0, xfer->tx.len);
	for (size_t i = 0; i < count; i++)
		payload[i] = cpu_to_le16(args[i]);
	ret = ph->xops->do_xfer(ph, xfer);
	ph->xops->xfer_put(ph, xfer);
	return ret;
}

static int plh_instance_init(const struct scmi_protocol_handle *ph)
{
	u32 version;

	return ph->xops->version_get(ph, &version);
}

static const struct plh_ops plh_ops = { .command = plh_command };
static const struct scmi_protocol plh_proto = {
	.id = QCOM_SCMI_PLH,
	.owner = THIS_MODULE,
	.vendor_id = "Qualcomm",
	.instance_init = plh_instance_init,
	.ops = &plh_ops,
};
module_scmi_protocol(plh_proto);
MODULE_ALIAS("scmi-protocol-0x81-Qualcomm");
MODULE_DESCRIPTION("Qualcomm SCMI performance lock protocol");
MODULE_LICENSE("GPL");
