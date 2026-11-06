// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm DSP firmware statistics. Only the remote processor writes these items. */
#include <asm/arch_timer.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/sysmon_subsystem_stats.h>
#include <linux/unaligned.h>

#define SYSMON_FEATURES 634
#define DSPPM_STATS 624
#define ISLAND_STATS 613

enum sysmon_feature { Q6_LOAD = 1, Q6_EVENTS, POWER, HMX, HVX };

static struct dentry *stats_dir;

static bool valid_dsp(enum dsp_id_t dsp)
{
	return dsp == ADSP || dsp == CDSP || dsp == SLPI;
}

static int feature_copy(enum dsp_id_t dsp, enum sysmon_feature type, void *dest, size_t bytes)
{
	const u8 *data;
	size_t left;

	if (!dest || !valid_dsp(dsp))
		return -EINVAL;
	data = qcom_smem_get(dsp, SYSMON_FEATURES, &left);
	if (IS_ERR(data))
		return PTR_ERR(data);
	while (left >= sizeof(u32)) {
		u32 header = get_unaligned_le32(data);
		size_t length = (header >> 16) & 0xfff;

		if (length < sizeof(header) || length > left)
			return -EBADMSG;
		if (header >> 28 == type) {
			if (bytes > length - sizeof(header))
				return -EMSGSIZE;
			memcpy(dest, data + sizeof(header), bytes);
			return 0;
		}
		left -= length;
		data += length;
	}
	return left ? -EBADMSG : -ENOKEY;
}

int sysmon_stats_query_power_residency(enum dsp_id_t dsp, struct sysmon_smem_power_stats *stats)
{
	return feature_copy(dsp, POWER, stats, sizeof(*stats));
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_power_residency);

int sysmon_stats_query_q6_votes(enum dsp_id_t dsp, struct sysmon_smem_q6_event_stats *stats)
{
	return feature_copy(dsp, Q6_EVENTS, stats, sizeof(*stats));
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_q6_votes);

static int query_sample(enum dsp_id_t dsp, enum sysmon_feature type, u32 *result)
{
	struct { u32 value, lo, hi; } sample;
	u64 stamp, now;
	int ret;

	if (!result)
		return -EINVAL;
	ret = feature_copy(dsp, type, &sample, sizeof(sample));
	if (ret)
		return ret;
	stamp = (u64)sample.hi << 32 | sample.lo;
	now = arch_timer_read_counter();
	/* The firmware timestamps use the 19.2 MHz Qualcomm sleep counter. */
	if (now > stamp && div_u64(now - stamp, 19200) > 100)
		sample.value = 0;
	if (type != Q6_LOAD && sample.value == 0xffff) {
		*result = 0;
		return DSP_PMU_COUNTER_NA;
	}
	*result = sample.value;
	return 0;
}

int sysmon_stats_query_q6_load(enum dsp_id_t dsp, u32 *load)
{
	return query_sample(dsp, Q6_LOAD, load);
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_q6_load);

int sysmon_stats_query_hvx_utlization(u32 *load)
{
	return query_sample(CDSP, HVX, load);
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_hvx_utlization);

int sysmon_stats_query_hmx_utlization(u32 *load)
{
	return query_sample(CDSP, HMX, load);
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_hmx_utlization);

static int item_copy(enum dsp_id_t dsp, u32 item, void *dest, size_t bytes)
{
	size_t size;
	const void *data = qcom_smem_get(dsp, item, &size);

	if (IS_ERR(data))
		return PTR_ERR(data);
	if (size < bytes)
		return -EMSGSIZE;
	memcpy(dest, data, bytes);
	return 0;
}

static int read_sleep(enum dsp_id_t dsp, u32 item, struct sleep_stats *stats)
{
	u64 now;
	int ret = item_copy(dsp, item, stats, sizeof(*stats));

	if (ret)
		return ret;
	now = arch_timer_read_counter();
	if (stats->last_entered_at > stats->last_exited_at && now > stats->last_entered_at)
		stats->accumulated += now - stats->last_entered_at;
	return 0;
}

int sysmon_stats_query_sleep(enum dsp_id_t dsp, struct sleep_stats *lpm, struct sleep_stats_island *lpi)
{
	static const u16 sleep_item[] = { [ADSP] = 606, [CDSP] = 607, [SLPI] = 608 };
	struct sleep_stats sample;
	int ret;

	if ((!lpm && !lpi) || !valid_dsp(dsp))
		return -EINVAL;
	if (lpm) {
		ret = read_sleep(dsp, sleep_item[dsp], lpm);
		if (ret)
			return ret;
	}
	if (lpi && dsp != CDSP) {
		ret = read_sleep(dsp, ISLAND_STATS, &sample);
		if (ret)
			return ret;
		static_assert(sizeof(*lpi) == sizeof(sample));
		memcpy(lpi, &sample, sizeof(sample));
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_sleep);

static int master_show(struct seq_file *s, void *unused)
{
	enum dsp_id_t dsp = (uintptr_t)s->private;
	struct sysmon_smem_q6_event_stats votes;
	struct sysmon_smem_power_stats power;
	struct { u32 version, latency, stamp; struct { u32 pid, active; } pd[5]; } clients;
	struct sleep_stats lpm;
	struct sleep_stats_island lpi;
	u32 value;
	int i;

	if (!item_copy(dsp, DSPPM_STATS, &clients, sizeof(clients))) {
		seq_printf(s, "DSPPM version %u latency %u timestamp %u\n",
			   clients.version, clients.latency, clients.stamp);
		for (i = 0; i < ARRAY_SIZE(clients.pd); i++)
			if (clients.pd[i].pid)
				seq_printf(s, "PID %u active %u\n", clients.pd[i].pid, clients.pd[i].active);
	}
	if (!sysmon_stats_query_q6_votes(dsp, &votes))
		seq_printf(s, "Core clock %u kHz AB %llu IB %llu latency %u us\n", votes.QDSP6_clk,
			   (u64)votes.Ab_vote_msb << 32 | votes.Ab_vote_lsb,
			   (u64)votes.Ib_vote_msb << 32 | votes.Ib_vote_lsb, votes.Sleep_latency);
	if (!sysmon_stats_query_q6_load(dsp, &value))
		seq_printf(s, "Q6 load %u\n", value);
	if (!sysmon_stats_query_power_residency(dsp, &power)) {
		for (i = 0; i < ARRAY_SIZE(power.clk_arr); i++)
			if (power.clk_arr[i])
				seq_printf(s, "%u kHz %u s\n", power.clk_arr[i], power.active_time[i]);
		seq_printf(s, "PC %u s island %u s\n", power.pc_time, power.lpi_time);
	}
	if (!sysmon_stats_query_sleep(dsp, &lpm, NULL))
		seq_printf(s, "LPM count %u accumulated %llu\n", lpm.count, lpm.accumulated);
	if (dsp == ADSP && !sysmon_stats_query_sleep(dsp, NULL, &lpi))
		seq_printf(s, "LPI count %u accumulated %llu\n", lpi.count, lpi.accumulated);
	if (dsp == CDSP) {
		if (!sysmon_stats_query_hvx_utlization(&value))
			seq_printf(s, "HVX %u%%\n", value);
		if (!sysmon_stats_query_hmx_utlization(&value))
			seq_printf(s, "HMX %u%%\n", value);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(master);

static int __init sysmon_stats_init(void)
{
	stats_dir = debugfs_create_dir("sysmon_subsystem_stats", NULL);
	debugfs_create_file("master_adsp_stats", 0444, stats_dir, (void *)ADSP, &master_fops);
	debugfs_create_file("master_cdsp_stats", 0444, stats_dir, (void *)CDSP, &master_fops);
	return 0;
}
module_init(sysmon_stats_init);

static void __exit sysmon_stats_exit(void)
{
	debugfs_remove_recursive(stats_dir);
}
module_exit(sysmon_stats_exit);

MODULE_DESCRIPTION("Qualcomm DSP firmware power, load and sleep statistics");
MODULE_LICENSE("GPL");
