// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm CPUCP frequency statistics shared-memory discovery. */
#include <linux/scmi_cpufreq_stats.h>
#include "common.h"

struct stats_window {
	__le32 attributes;
	__le32 address_low;
	__le32 address_high;
	__le32 length;
};

static int get_memory(const struct scmi_protocol_handle *ph, phys_addr_t *base, u32 *size)
{
	struct stats_window *reply;
	struct scmi_xfer *xfer;
	int ret;

	ret = ph->xops->xfer_get_init(ph, 16, sizeof(*reply), sizeof(*reply), &xfer);
	if (ret)
		return ret;
	memset(xfer->tx.buf, 0, xfer->tx.len);
	ret = ph->xops->do_xfer(ph, xfer);
	if (!ret && xfer->rx.len != sizeof(*reply))
		ret = -EPROTO;
	if (!ret) {
		reply = xfer->rx.buf;
		*base = (u64)le32_to_cpu(reply->address_high) << 32 |
			le32_to_cpu(reply->address_low);
		*size = le32_to_cpu(reply->length);
	}
	ph->xops->xfer_put(ph, xfer);
	return ret;
}

static int stats_instance_init(const struct scmi_protocol_handle *ph)
{
	u32 version;

	return ph->xops->version_get(ph, &version);
}

static const struct cpufreq_stats_ops stats_ops = { .get_memory = get_memory };
static const struct scmi_protocol stats_proto = {
	.id = QCOM_SCMI_CPUFREQ_STATS,
	.owner = THIS_MODULE,
	.vendor_id = "Qualcomm",
	.instance_init = stats_instance_init,
	.ops = &stats_ops,
};
module_scmi_protocol(stats_proto);
MODULE_ALIAS("scmi-protocol-0x84-Qualcomm");
MODULE_DESCRIPTION("Qualcomm SCMI CPU frequency statistics protocol");
MODULE_LICENSE("GPL");
