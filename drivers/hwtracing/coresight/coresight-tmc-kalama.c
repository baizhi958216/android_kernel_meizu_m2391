// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm ETR input filters and byte counter share the QDSS CSR block. */
#include <linux/bitmap.h>
#include <linux/coresight.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include "coresight-priv.h"
#include "coresight-tmc.h"

#define KALAMA_ATIDS 128

struct kalama_trace_csr {
	void __iomem *base;
	u32 atid_off;
	u32 count_off;
	unsigned int users[KALAMA_ATIDS];
};

/* Both ETRs operate the same CSR software lock. */
static DEFINE_RAW_SPINLOCK(csr_lock);

int tmc_kalama_csr_probe(struct device *dev, struct tmc_drvdata *drvdata)
{
	struct device_node *node;
	struct kalama_trace_csr *csr;
	struct resource res;
	resource_size_t size;
	int ret;

	if (!of_property_present(dev->of_node, "coresight-csr"))
		return 0;
	node = of_parse_phandle(dev->of_node, "coresight-csr", 0);
	if (!node)
		return -EINVAL;
	ret = of_address_to_resource(node, 0, &res);
	of_node_put(node);
	if (ret)
		return ret;
	size = resource_size(&res);
	csr = devm_kzalloc(dev, sizeof(*csr), GFP_KERNEL);
	if (!csr)
		return -ENOMEM;
	if (device_property_read_u32(dev, "csr-atid-offset", &csr->atid_off) ||
	    device_property_read_u32(dev, "csr-irqctrl-offset", &csr->count_off) ||
	    !IS_ALIGNED(csr->atid_off | csr->count_off, sizeof(u32)) ||
	    size < CORESIGHT_LAR + 4 || csr->atid_off > size - 16 ||
	    csr->count_off > size - 4)
		return -EINVAL;
	csr->base = devm_ioremap(dev, res.start, size);
	if (!csr->base)
		return -ENOMEM;
	drvdata->csr = csr;
	return 0;
}

void tmc_kalama_byte_count(struct tmc_drvdata *drvdata, u32 bytes)
{
	struct kalama_trace_csr *csr = drvdata->csr;
	unsigned long flags;

	raw_spin_lock_irqsave(&csr_lock, flags);
	CS_UNLOCK(csr->base);
	writel(bytes / 8, csr->base + csr->count_off);
	CS_LOCK(csr->base);
	raw_spin_unlock_irqrestore(&csr_lock, flags);
}

void tmc_kalama_filter_path(struct coresight_device *sink, struct coresight_device *source,
			    u8 trace_id, bool enable)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(sink->dev.parent);
	struct kalama_trace_csr *csr = drvdata->csr;
	DECLARE_BITMAP(ids, KALAMA_ATIDS) = {};
	const __be32 *words;
	unsigned long flags, id;
	int count, i;

	if (!csr)
		return;
	words = of_get_property(source->dev.parent->of_node, "atid", &count);
	for (i = 0; words && i < count / sizeof(*words); i++) {
		id = be32_to_cpup(words + i);
		if (id && id < KALAMA_ATIDS)
			__set_bit(id, ids);
	}
	if (trace_id && trace_id < KALAMA_ATIDS)
		__set_bit(trace_id, ids);

	raw_spin_lock_irqsave(&csr_lock, flags);
	CS_UNLOCK(csr->base);
	for_each_set_bit(id, ids, KALAMA_ATIDS) {
		void __iomem *reg = csr->base + csr->atid_off + (id / 32) * 4;
		u32 bit = BIT(id % 32);

		if (enable) {
			if (!csr->users[id]++)
				writel(readl(reg) | bit, reg);
		} else if (csr->users[id] && !--csr->users[id]) {
			writel(readl(reg) & ~bit, reg);
		}
	}
	CS_LOCK(csr->base);
	raw_spin_unlock_irqrestore(&csr_lock, flags);
}
