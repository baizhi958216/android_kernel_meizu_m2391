// SPDX-License-Identifier: GPL-2.0-only
/* M2391 Samsung NAND health command and sysfs format, recovered from stock.
 * No vendor command runs until userspace reads the original health attribute.
 */
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include "ufshcd.h"
#include "ufs-meizu.h"

static int samsung_command(struct scsi_device *sdev, const u8 *cdb,
		void *data, unsigned int length)
{
	return scsi_execute(sdev, cdb, length ? DMA_FROM_DEVICE : DMA_NONE,
		data, length, NULL, NULL, 30 * HZ, 3, 0, 0, NULL);
}

static int samsung_nand_info(struct ufs_hba *hba, struct scsi_device *sdev,
		u8 *data)
{
	const u8 enter[16] = { 0xc0, 0x00, 0x5c, 0x38, 0x23, 0xae, 0x67, 0x68,
		0x72 };
	const u8 set_password[16] = { 0xc0, 0x03, 0x67, 0x68, 0x72 };
	const u8 read_health[16] = { 0xc0, 0x40, 0, 0, 1, 0x0a,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0xaa };
	const u8 exit[16] = { 0xc0, 0x01 };
	bool old_noresume;
	int ret, exit_ret;

	/* host_sem, held by our caller, also serializes UFS power transitions. */
	old_noresume = hba->host->eh_noresume;
	hba->host->eh_noresume = 1;
	ret = samsung_command(sdev, enter, NULL, 0);
	if (ret) {
		pr_err("ufs: enter vendor mode fail, program key and try again\n");
		ret = samsung_command(sdev, set_password, NULL, 0);
		if (ret)
			goto restore;
		ret = samsung_command(sdev, enter, NULL, 0);
		if (ret)
			goto restore;
	}
	ret = samsung_command(sdev, read_health, data, 0xaa);
	/* A failed read must still leave vendor mode; never format fake zeros. */
	exit_ret = samsung_command(sdev, exit, NULL, 0);
	if (!ret)
		ret = exit_ret;
restore:
	hba->host->eh_noresume = old_noresume;
	return ret ? -EIO : 0;
}

static ssize_t health_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct scsi_device *sdev;
	u8 *data;
	ssize_t ret;
	int length = 0;
	static const struct {
		const char *name;
		u8 offset;
	} counters[] = {
		{ "max_slc_erase_cycle", 8 },
		{ "min_slc_erase_cycle", 12 },
		{ "average_slc_erase_cycle", 16 },
		{ "max_mlc_erase_cycle", 20 },
		{ "min_mlc_erase_cycle", 24 },
		{ "average_mlc_erase_cycle", 28 },
		{ "read_reclaim_count", 32 },
		{ "initial_bad_block", 36 },
		{ "run_time_bad_block", 40 },
		{ "remain_reserved_block", 44 },
	};
	int i;

	down(&hba->host_sem);
	if (!ufshcd_is_user_access_allowed(hba)) {
		ret = -EBUSY;
		goto unlock;
	}
	/* Original health selection is the first enumerated SCSI LU. */
	sdev = __scsi_iterate_devices(hba->host, NULL);
	if (!sdev) {
		ret = 0;
		goto unlock;
	}
	ret = scsi_autopm_get_device(sdev);
	if (ret)
		goto put_device;
	if (strncmp(sdev->vendor, "SAMSUNG", 7) || !scsi_device_online(sdev)) {
		ret = -ENODEV;
		goto put_pm;
	}
	data = kzalloc(0xaa, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto put_pm;
	}
	ret = samsung_nand_info(hba, sdev, data);
	if (!ret) {
		length += sysfs_emit_at(buf, length, "length: %u\n",
			get_unaligned_be32(data));
		length += sysfs_emit_at(buf, length, "descriptor_type: %u\n", data[4]);
		length += sysfs_emit_at(buf, length, "ssr_version: %u\n", data[5]);
		for (i = 0; i < ARRAY_SIZE(counters); i++)
			length += sysfs_emit_at(buf, length, "%s: %u\n",
				counters[i].name,
				get_unaligned_be32(data + counters[i].offset));
		ret = length;
	}
	kfree(data);
put_pm:
	scsi_autopm_put_device(sdev);
put_device:
	scsi_device_put(sdev);
unlock:
	up(&hba->host_sem);
	return ret;
}
static DEVICE_ATTR_RO(health);

static struct attribute *samsung_health_sysfs[] = {
	&dev_attr_health.attr, NULL,
};
static const struct attribute_group samsung_health_sysfs_group = {
	.name = "ufs_health", .attrs = samsung_health_sysfs,
};
static const struct attribute_group *samsung_health_sysfs_groups[] = {
	&samsung_health_sysfs_group, NULL,
};

int ufs_health_init_sysfs(struct ufs_hba *hba)
{
	return sysfs_create_groups(&hba->dev->kobj, samsung_health_sysfs_groups);
}

void ufs_health_exit(struct ufs_hba *hba)
{
	sysfs_remove_groups(&hba->dev->kobj, samsung_health_sysfs_groups);
}
