// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm CPUCP per-clock-domain frequency and residency counters. */
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/scmi_cpufreq_stats.h>
#include <linux/scmi_protocol.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#define QCOM_STATS_SIGNATURE 0x50455246

struct stats_header {
	__le32 signature;
	__le16 revision, attributes, domains, reserved;
	__le32 sequence;
	__le32 domain_offsets[];	/* Measured in 32-bit words. */
};
struct level {
	__le32 frequency, reserved;
	__le32 usage_low, usage_high;
	__le32 residency_low, residency_high;
};
struct domain_header {
	__le16 levels, current_index;
	__le32 ext_offset;
	__le64 last_change;
	struct level table[];
};

struct domain_file {
	struct stats_header __iomem *header;
	struct domain_header __iomem *domain;
	u16 levels;
	bool residency;
};

struct level_sample {
	u32 frequency;
	u64 counter;
};

static int domain_show(struct seq_file *seq, void *unused)
{
	struct domain_file *file = seq->private;
	struct level_sample *sample __free(kfree) = NULL;

	sample = kcalloc(file->levels, sizeof(*sample), GFP_KERNEL);
	if (!sample)
		return -ENOMEM;

	/* CPUCP brackets counter updates with an odd/even sequence. */
	for (int attempt = 0; attempt < 32; attempt++) {
		u32 start = readl(&file->header->sequence);

		if (start & 1) {
			cond_resched();
			continue;
		}
		for (u16 i = 0; i < file->levels; i++) {
			struct level __iomem *l = &file->domain->table[i];
			u32 low, high;

			sample[i].frequency = readl(&l->frequency);
			if (file->residency) {
				low = readl(&l->residency_low);
				high = readl(&l->residency_high);
			} else {
				low = readl(&l->usage_low);
				high = readl(&l->usage_high);
			}
			sample[i].counter = (u64)high << 32 | low;
		}
		if (readl(&file->header->sequence) != start)
			continue;
		for (u16 i = 0; i < file->levels; i++)
			seq_printf(seq, "%u %llu\n", sample[i].frequency, sample[i].counter);
		return 0;
	}
	return -EAGAIN;
}
DEFINE_SHOW_ATTRIBUTE(domain);

static void stats_debugfs_remove(void *dir)
{
	debugfs_remove(dir);
}

static int stats_probe(struct scmi_device *sdev)
{
	const struct cpufreq_stats_ops *ops;
	struct scmi_protocol_handle *ph;
	struct stats_header __iomem *header;
	struct domain_file *files;
	struct dentry *root;
	phys_addr_t base, end;
	u32 size;
	u16 domains;
	int ret;

	ops = sdev->handle->devm_protocol_get(sdev, QCOM_SCMI_CPUFREQ_STATS, &ph);
	if (IS_ERR(ops))
		return PTR_ERR(ops);
	ret = ops->get_memory(ph, &base, &size);
	if (ret)
		return ret;
	if (!base || !IS_ALIGNED(base, 4) || size < sizeof(*header) ||
	    check_add_overflow(base, (phys_addr_t)size - 1, &end))
		return -EPROTO;
	header = devm_ioremap(&sdev->dev, base, size);
	if (!header)
		return -ENOMEM;
	domains = readw(&header->domains);
	if (readl(&header->signature) != QCOM_STATS_SIGNATURE ||
	    readw(&header->revision) != 1 || !domains ||
	    struct_size(header, domain_offsets, domains) > size)
		return -EPROTO;
	files = devm_kcalloc(&sdev->dev, domains * 2, sizeof(*files), GFP_KERNEL);
	if (!files)
		return -ENOMEM;

	for (u16 i = 0; i < domains; i++) {
		struct domain_header __iomem *domain;
		u64 offset = 4ULL * readl(&header->domain_offsets[i]);
		u16 levels;

		if (offset < struct_size(header, domain_offsets, domains) || offset > size ||
		    size - offset < sizeof(*domain))
			return -EPROTO;
		domain = (void __iomem *)header + offset;
		levels = readw(&domain->levels);
		if (struct_size(domain, table, levels) > size - offset)
			return -EPROTO;
		files[2 * i] = (struct domain_file) {
			.header = header, .domain = domain, .levels = levels,
		};
		files[2 * i + 1] = files[2 * i];
		files[2 * i + 1].residency = true;
	}

	root = debugfs_create_dir("cpufreq_stats", NULL);
	if (IS_ERR(root))
		return PTR_ERR(root) == -ENODEV ? 0 : PTR_ERR(root);
	ret = devm_add_action_or_reset(&sdev->dev, stats_debugfs_remove, root);
	if (ret)
		return ret;
	for (u16 i = 0; i < domains; i++) {
		struct dentry *dir;
		char name[16];

		snprintf(name, sizeof(name), "clkdom%u", i);
		dir = debugfs_create_dir(name, root);
		if (IS_ERR(dir))
			return PTR_ERR(dir);
		debugfs_create_file("usage", 0400, dir, &files[2 * i], &domain_fops);
		debugfs_create_file("time_in_state", 0400, dir, &files[2 * i + 1], &domain_fops);
	}
	return 0;
}

static const struct scmi_device_id stats_ids[] = {
	{ QCOM_SCMI_CPUFREQ_STATS, "scmi_cpufreq_stats_protocol" }, {}
};
MODULE_DEVICE_TABLE(scmi, stats_ids);
static struct scmi_driver stats_driver = {
	.name = "scmi-cpufreq-stats-driver", .probe = stats_probe, .id_table = stats_ids,
};
module_scmi_driver(stats_driver);
MODULE_SOFTDEP("pre: cpufreq_stats_vendor");
MODULE_DESCRIPTION("Qualcomm CPUCP per-domain frequency statistics");
MODULE_LICENSE("GPL");
