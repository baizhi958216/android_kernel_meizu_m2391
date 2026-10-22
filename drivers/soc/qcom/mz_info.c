// SPDX-License-Identifier: GPL-2.0-only
/*
 * M2391 mz_info recovery from stock init.text+0..0x3e8 and the 25 proc
 * callbacks in text+0x424..0x10e8. Export types come from original split BTF.
 * /bootinfo names, mandatory read order, fallbacks, unusual spellings and
 * zero padding below are the original userspace protocol, not new IDs.
 * Bounded copies replace stock strcpy; DT references and partial proc setup
 * are released. No partition, bootloader lock or device register is written.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/meizu_info.h>

static char sn[16], chipid[64], psn[32], imei1[16], imei2[16], meid[16];
static char socid[12], sw_version[16], bl_version[16], color[5];
static char ddr_manufacturer[16];
static u32 bl_unlock, sec, sec_stage, panel_id, rf_id;
static u32 project_id, hw_version, dp_exist, rtx, ssr_flag;
static struct proc_dir_entry *info_dir;

const char *meizu_get_sn(void) { return sn; }
EXPORT_SYMBOL(meizu_get_sn);
const char *meizu_get_chipid(void) { return chipid; }
EXPORT_SYMBOL(meizu_get_chipid);
int bootloader_is_unlock(void) { return bl_unlock == 1 || bl_unlock == 2; }
EXPORT_SYMBOL(bootloader_is_unlock);
int is_secure_chip(void) { return sec == 1; }
EXPORT_SYMBOL(is_secure_chip);
u32 nfc_read_rf_id(void) { return rf_id; }
EXPORT_SYMBOL(nfc_read_rf_id);
bool is_minidump(void) { return dp_exist == 0; }
EXPORT_SYMBOL(is_minidump);
unsigned int cs_get_lcd_id(void) { return panel_id; }
EXPORT_SYMBOL(cs_get_lcd_id);
/* Stock checks only the low byte here, unlike the full-word lcd_id getter. */
bool is_white_lcd(void) { return (u8)panel_id == 1; }
EXPORT_SYMBOL(is_white_lcd);

enum info_entry {
	INFO_DDR, INFO_LCD, INFO_CHIPID, INFO_COLOR, INFO_RTX, INFO_SSR,
	INFO_DP_TYPE, INFO_DP_EXIST, INFO_UNLOCK, INFO_RF, INFO_MP, INFO_PROJECT,
	INFO_HW, INFO_MODEL, INFO_SW, INFO_BL, INFO_SOC, INFO_SOCUID, INFO_MEID,
	INFO_IMEI2, INFO_IMEI1, INFO_SN, INFO_STAGE, INFO_SEC, INFO_PSN,
};
struct info_descriptor { const char *name; enum info_entry entry; };
static const struct info_descriptor entries[] = {
	{ "ddr_manufacturer", INFO_DDR }, { "lcd_id", INFO_LCD },
	{ "chipid", INFO_CHIPID }, { "colortype", INFO_COLOR },
	{ "rtx", INFO_RTX }, { "ssr_flag", INFO_SSR },
	{ "dp_type", INFO_DP_TYPE }, { "dp_exist", INFO_DP_EXIST },
	{ "bl_unlock", INFO_UNLOCK }, { "rf_id", INFO_RF },
	{ "is_mp", INFO_MP }, { "project_id", INFO_PROJECT },
	{ "hw_version", INFO_HW }, { "model_name", INFO_MODEL },
	{ "sw_version", INFO_SW }, { "bl_version", INFO_BL },
	{ "soc_version", INFO_SOC }, { "socuid", INFO_SOCUID },
	{ "meid", INFO_MEID }, { "imei2", INFO_IMEI2 },
	{ "imei1", INFO_IMEI1 }, { "sn", INFO_SN },
	{ "sec_stage", INFO_STAGE }, { "sec", INFO_SEC }, { "psn", INFO_PSN },
};

static int info_show(struct seq_file *seq, void *unused)
{
	const struct info_descriptor *desc = seq->private;
	const char *text = NULL;

	switch (desc->entry) {
	case INFO_DDR: text = ddr_manufacturer; break;
	case INFO_CHIPID: text = chipid; break;
	case INFO_COLOR: text = color; break;
	case INFO_MODEL: text = "m2391"; break;
	case INFO_SW: text = sw_version; break;
	case INFO_BL: text = bl_version; break;
	case INFO_SOC: text = "SM8550"; break;
	case INFO_MEID: text = meid; break;
	case INFO_IMEI2: text = imei2; break;
	case INFO_IMEI1: text = imei1; break;
	case INFO_SN: text = sn; break;
	case INFO_PSN: text = psn; break;
	case INFO_STAGE:
		text = sec_stage == 1 ? "stage1" : sec_stage == 2 ? "stage2" : "unkonwn";
		break;
	case INFO_SEC: text = sec ? "Secure Chip" : "Non-Secure Chip"; break;
	case INFO_LCD: seq_printf(seq, "0x%06x\n", panel_id); break;
	case INFO_RTX: seq_printf(seq, "%d\n", rtx); break;
	case INFO_SSR: seq_printf(seq, "%d\n", ssr_flag); break;
	case INFO_DP_EXIST: seq_printf(seq, "%d\n", dp_exist); break;
	case INFO_UNLOCK: seq_printf(seq, "%d\n", bl_unlock); break;
	case INFO_RF: seq_printf(seq, "0x%x\n", rf_id); break;
	case INFO_MP: seq_printf(seq, "%d\n", hw_version > 14); break;
	case INFO_PROJECT: seq_printf(seq, "0x%02x\n", project_id); break;
	case INFO_HW: seq_printf(seq, "0x%x\n", hw_version); break;
	case INFO_DP_TYPE:
		seq_puts(seq, dp_exist == 1 ? "fulldump\n" :
				dp_exist == 0 ? "minidump\n" : "unknown\n");
		break;
	case INFO_SOCUID: seq_printf(seq, "%s%053X\n", socid, 0); break;
	}
	if (text)
		seq_printf(seq, "%s\n", text);
	return 0;
}
static int info_open(struct inode *inode, struct file *file)
{
	return single_open(file, info_show, PDE_DATA(inode));
}
static const struct proc_ops info_ops = {
	.proc_open = info_open, .proc_read = seq_read,
	.proc_lseek = seq_lseek, .proc_release = single_release,
};

static int read_string(struct device_node *node, const char *name,
		char *buffer, size_t size)
{
	const char *value;
	int ret = of_property_read_string(node, name, &value);

	if (ret)
		return ret;
	/* Reject malformed overlong boot properties instead of corrupting the
	 * adjacent security/identity state as stock unbounded copies could do.
	 */
	return strscpy(buffer, value, size) < 0 ? -E2BIG : 0;
}

static int __init mz_info_init(void)
{
	struct device_node *node = of_find_node_by_path("/bootinfo");
	u32 software;
	size_t i;
	int ret;

	if (!node)
		return -ENODEV;
#define READ_VALUE(name, dest) do { \
	ret = of_property_read_u32(node, name, &(dest)); \
	if (ret) goto put_node; \
} while (0)
#define READ_STRING(name, dest) do { \
	ret = read_string(node, name, dest, sizeof(dest)); \
	if (ret) goto put_node; \
} while (0)
	READ_VALUE("sec", sec);
	READ_VALUE("sec_stage", sec_stage);
	READ_STRING("psn", psn);
	READ_STRING("sn", sn);
	READ_STRING("imei1", imei1);
	READ_STRING("imei2", imei2);
	READ_STRING("meid", meid);
	READ_STRING("socid", socid);
	READ_VALUE("rf_id", rf_id);
	READ_VALUE("project_id", project_id);
	READ_VALUE("hw_version", hw_version);
	READ_VALUE("sw_version", software);
	snprintf(sw_version, sizeof(sw_version), "0x%08X", software);
	READ_VALUE("bl_unlock", bl_unlock);
	READ_VALUE("dp_exist", dp_exist);
	READ_VALUE("lock_flag", rtx);
	READ_STRING("color", color);
	ret = read_string(node, "bl_version", bl_version, sizeof(bl_version));
	if (ret == -E2BIG)
		goto put_node;
	if (ret)
		strscpy(bl_version, "unknown", sizeof(bl_version));
	if (of_property_read_u32(node, "dis_id", &panel_id))
		panel_id = 0;
	ret = read_string(node, "ddr_manufacturer", ddr_manufacturer,
			sizeof(ddr_manufacturer));
	if (ret == -E2BIG)
		goto put_node;
	if (ret)
		strscpy(ddr_manufacturer, "Unkown", sizeof(ddr_manufacturer));
	of_property_read_u32(node, "ssr_flag", &ssr_flag);
	snprintf(chipid, sizeof(chipid), "%s%s%021X", psn, socid, 0);
	ret = 0;
put_node:
	of_node_put(node);
	if (ret)
		return ret;
#undef READ_VALUE
#undef READ_STRING
	info_dir = proc_mkdir("mz_info", NULL);
	if (!info_dir)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		if (!proc_create_data(entries[i].name, 0444, info_dir, &info_ops,
				(void *)&entries[i])) {
			proc_remove(info_dir);
			return -ENOMEM;
		}
	}
	return 0;
}
static void __exit mz_info_exit(void)
{
	proc_remove(info_dir);
}
module_init(mz_info_init);
module_exit(mz_info_exit);
MODULE_LICENSE("GPL");
