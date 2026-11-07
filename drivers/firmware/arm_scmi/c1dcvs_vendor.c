// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm CPUCP C1DCVS firmware interface. */
#include <linux/scmi_c1dcvs.h>
#include "common.h"

static int c1dcvs_write(const struct scmi_protocol_handle *ph, enum c1dcvs_setting setting,
			const u32 *values, size_t count)
{
	struct scmi_xfer *xfer;
	__le32 *msg;
	int ret;

	if (!count || count > 2)
		return -EINVAL;
	ret = ph->xops->xfer_get_init(ph, setting, count * sizeof(*msg),
				      count * sizeof(*msg), &xfer);
	if (ret)
		return ret;
	msg = xfer->tx.buf;
	for (size_t i = 0; i < count; i++)
		msg[i] = cpu_to_le32(values[i]);
	ret = ph->xops->do_xfer(ph, xfer);
	ph->xops->xfer_put(ph, xfer);
	return ret;
}

static int c1dcvs_read(const struct scmi_protocol_handle *ph, enum c1dcvs_setting setting,
		       u32 *values, size_t capacity)
{
	struct scmi_xfer *xfer;
	const __le32 *msg;
	size_t count;
	int ret;

	/* Each get follows its paired set ID and takes two reserved words. */
	ret = ph->xops->xfer_get_init(ph, setting + 1, 2 * sizeof(__le32), 0, &xfer);
	if (ret)
		return ret;
	memset(xfer->tx.buf, 0, xfer->tx.len);
	ret = ph->xops->do_xfer(ph, xfer);
	if (ret)
		goto out;
	count = xfer->rx.len / sizeof(*msg);
	if (!count || count > capacity || xfer->rx.len % sizeof(*msg)) {
		ret = -EPROTO;
		goto out;
	}
	msg = xfer->rx.buf;
	for (size_t i = 0; i < count; i++)
		values[i] = le32_to_cpu(msg[i]);
	ret = count;
out:
	ph->xops->xfer_put(ph, xfer);
	return ret;
}

static int c1dcvs_instance_init(const struct scmi_protocol_handle *ph)
{
	u32 version;

	return ph->xops->version_get(ph, &version);
}

static const struct c1dcvs_ops c1dcvs_ops = { .write = c1dcvs_write, .read = c1dcvs_read };
static const struct scmi_protocol c1dcvs = {
	.id = QCOM_SCMI_C1DCVS,
	.owner = THIS_MODULE,
	.vendor_id = "Qualcomm",
	.instance_init = c1dcvs_instance_init,
	.ops = &c1dcvs_ops,
};
module_scmi_protocol(c1dcvs);
MODULE_ALIAS("scmi-protocol-0x87-Qualcomm");
MODULE_DESCRIPTION("Qualcomm SCMI C1DCVS protocol");
MODULE_LICENSE("GPL");
