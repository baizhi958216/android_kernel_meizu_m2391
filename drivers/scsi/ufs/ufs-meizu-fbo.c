// SPDX-License-Identifier: GPL-2.0-only
/* M2391 FBO and HPB userspace protocol recovered from the original driver.
 * These are device queries, not cached or synthetic health/status values.
 */
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include "ufshcd.h"
#include "ufs-meizu.h"

static int fbo_transfer(struct scsi_device *sdev, void *buffer, size_t length,
			bool write)
{
	u8 cdb[10] = { write ? WRITE_BUFFER : READ_BUFFER, 2,
		write ? 1 : 2 };
	struct scsi_sense_hdr sense = {};
	int ret;

	if (length > 0xffffff)
		return -EINVAL;
	cdb[6] = length >> 16;
	cdb[7] = length >> 8;
	cdb[8] = length;
	ret = scsi_execute(sdev, cdb, write ? DMA_TO_DEVICE : DMA_FROM_DEVICE,
		buffer, length, NULL, &sense, 15 * HZ, 0, 0, 0, NULL);
	if (ret)
		pr_err("%s: %s Buffer failed,sense key:0x%x;asc:0x%x;ascq:0x%x\n",
			write ? "ufsfbo_write_frag_buffer" : "ufsfbo_read_frag_buffer",
			write ? "Write" : "Read", sense.sense_key, sense.asc, sense.ascq);
	return ret;
}

int ufsfbo_read_frag_buffer(struct scsi_device *sdev, void *buffer, size_t length)
{
	return fbo_transfer(sdev, buffer, length, false);
}

int ufsfbo_write_frag_buffer(struct scsi_device *sdev, void *buffer, size_t length)
{
	return fbo_transfer(sdev, buffer, length, true);
}

/* Preserve the original LU selection. scsi iteration owns the returned ref. */
static struct scsi_device *fbo_sdev(struct ufs_hba *hba)
{
	struct scsi_device *sdev;

	shost_for_each_device(sdev, hba->host)
		if (!strcmp(dev_name(&sdev->sdev_gendev), "0:0:0:0"))
			return sdev;
	return NULL;
}

static ssize_t fbo_buffer_file_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t pos, size_t count)
{
	struct ufs_hba *hba = dev_get_drvdata(kobj_to_dev(kobj));
	struct scsi_device *sdev;
	int ret;

	if (count != 4096)
		return -EINVAL;
	sdev = fbo_sdev(hba);
	if (!sdev)
		return -EINVAL;
	ret = ufsfbo_read_frag_buffer(sdev, buf, count);
	scsi_device_put(sdev);
	return ret < 0 ? -EINVAL : count;
}

static ssize_t fbo_buffer_file_write(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t pos, size_t count)
{
	struct ufs_hba *hba = dev_get_drvdata(kobj_to_dev(kobj));
	struct scsi_device *sdev;
	u8 *transfer;
	int ret;

	if (count > 4096)
		return -EINVAL;
	/* The device transaction is always 4K. Zero unused bytes on short writes. */
	transfer = kzalloc(4096, GFP_KERNEL);
	if (!transfer)
		return -ENOMEM;
	memcpy(transfer, buf, count);
	sdev = fbo_sdev(hba);
	if (!sdev) {
		ret = -EINVAL;
		goto free;
	}
	ret = ufsfbo_write_frag_buffer(sdev, transfer, 4096);
	scsi_device_put(sdev);
	ret = ret < 0 ? -EINVAL : count;
free:
	kfree(transfer);
	return ret;
}

static struct bin_attribute fbo_sysfs_bin_file = {
	.attr = { .name = "fbo_buffer", .mode = 0666 },
	.size = 4096,
	.read = fbo_buffer_file_read,
	.write = fbo_buffer_file_write,
};

static ssize_t fbo_attr_show(struct device *dev, char *buf, u8 idn)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	u32 value = 0;
	ssize_t ret;

	down(&hba->host_sem);
	if (!ufshcd_is_user_access_allowed(hba) ||
	    !READ_ONCE(hba->sdev_ufs_device)) {
		ret = -EBUSY;
		goto unlock;
	}
	ufshcd_rpm_get_sync(hba);
	ret = ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR, idn, 0, 0, &value);
	ufshcd_rpm_put_sync(hba);
	ret = ret ? -EINVAL : sysfs_emit(buf, "0x%08X\n", value);
unlock:
	up(&hba->host_sem);
	return ret;
}

static ssize_t fbo_attr_store(struct device *dev, const char *buf,
		size_t count, u8 idn)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	u32 value;
	ssize_t ret;

	if (kstrtouint(buf, 0, &value))
		return -EINVAL;
	down(&hba->host_sem);
	if (!ufshcd_is_user_access_allowed(hba) ||
	    !READ_ONCE(hba->sdev_ufs_device)) {
		ret = -EBUSY;
		goto unlock;
	}
	ufshcd_rpm_get_sync(hba);
	ret = ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_WRITE_ATTR, idn, 0, 0, &value);
	ufshcd_rpm_put_sync(hba);
	ret = ret ? -EINVAL : count;
unlock:
	up(&hba->host_sem);
	return ret;
}

static ssize_t fbo_progress_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return fbo_attr_show(dev, buf, 0x33);
}
static DEVICE_ATTR_RO(fbo_progress_state);

static ssize_t fbo_execute_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return fbo_attr_show(dev, buf, 0x32);
}
static ssize_t fbo_execute_threshold_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return fbo_attr_store(dev, buf, count, 0x32);
}
static DEVICE_ATTR_RW(fbo_execute_threshold);

static ssize_t fbo_control_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return fbo_attr_store(dev, buf, count, 0x31);
}
static DEVICE_ATTR_WO(fbo_control);

static ssize_t ufs_sysfs_read_desc_param(struct ufs_hba *hba, u8 offset,
		char *buf, u8 width)
{
	u8 *desc;
	int length = 255;
	u32 value;
	ssize_t ret;

	down(&hba->host_sem);
	if (!ufshcd_is_user_access_allowed(hba) ||
	    !READ_ONCE(hba->sdev_ufs_device)) {
		ret = -EBUSY;
		goto unlock;
	}
	ufshcd_rpm_get_sync(hba);
	desc = kzalloc(length, GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto put_pm;
	}
	ret = ufshcd_query_descriptor_retry(hba, UPIU_QUERY_OPCODE_READ_DESC,
		0x0a, 0, 0, desc, &length);
	if (ret || length <= offset || desc[0] <= offset) {
		ret = -EINVAL;
		goto free;
	}
	/* The original accepts truncated descriptors and zero-extends the tail. */
	value = 0;
	if (width == 1 || width == 2 || width == 4) {
		u8 data[4] = {};

		memcpy(data, desc + offset,
			min_t(unsigned int, width, min(length, (int)desc[0]) - offset));
		value = width == 1 ? data[0] : width == 2 ?
			get_unaligned_be16(data) : get_unaligned_be32(data);
		ret = sysfs_emit(buf, width == 1 ? "0x%02X\n" :
			width == 2 ? "0x%04X\n" : "0x%08X\n", value);
	} else {
		ret = 0;
	}
free:
	kfree(desc);
put_pm:
	ufshcd_rpm_put_sync(hba);
unlock:
	up(&hba->host_sem);
	return ret;
}

#define FBO_DESC(name, offset, width) \
static ssize_t name##_show(struct device *dev, \
	struct device_attribute *attr, char *buf) \
{ \
	return ufs_sysfs_read_desc_param(dev_get_drvdata(dev), offset, buf, width); \
} \
static DEVICE_ATTR_RO(name)
FBO_DESC(fbo_version, 1, 2);
FBO_DESC(fbo_recommended_lba_range_size, 3, 4);
FBO_DESC(fbo_max_lba_range_size, 7, 4);
FBO_DESC(fbo_min_lba_range_size, 11, 4);
FBO_DESC(fbo_max_lba_range_count, 15, 1);
FBO_DESC(fbo_lba_range_alignment, 16, 2);

static ssize_t hpb_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	bool enabled = false;
	ssize_t ret;

	down(&hba->host_sem);
	if (!ufshcd_is_user_access_allowed(hba) ||
	    !READ_ONCE(hba->sdev_ufs_device)) {
		ret = -EBUSY;
		goto unlock;
	}
	ufshcd_rpm_get_sync(hba);
	ret = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_READ_FLAG, 0x12, 0, &enabled);
	ufshcd_rpm_put_sync(hba);
	ret = ret ? -EINVAL : sysfs_emit(buf, "%s\n", enabled ? "true" : "false");
unlock:
	up(&hba->host_sem);
	return ret;
}

static ssize_t hpb_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	u32 value;
	ssize_t ret;

	if (kstrtouint(buf, 0, &value))
		return -EINVAL;
	down(&hba->host_sem);
	if (!ufshcd_is_user_access_allowed(hba) ||
	    !READ_ONCE(hba->sdev_ufs_device)) {
		ret = -EBUSY;
		goto unlock;
	}
	ufshcd_rpm_get_sync(hba);
	ret = ufshcd_query_flag(hba, value ? UPIU_QUERY_OPCODE_SET_FLAG :
		UPIU_QUERY_OPCODE_CLEAR_FLAG, 0x12, 0, NULL);
	ufshcd_rpm_put_sync(hba);
	ret = ret ? -EINVAL : count;
unlock:
	up(&hba->host_sem);
	return ret;
}
static DEVICE_ATTR_RW(hpb_enable);

static struct attribute *ufs_sysfs_hpb_attr_descriptor[] = {
	&dev_attr_hpb_enable.attr, NULL,
};
static const struct attribute_group ufs_sysfs_hpb_attr_descriptor_group = {
	.name = "mz_hpb_attr", .attrs = ufs_sysfs_hpb_attr_descriptor,
};
static struct attribute *ufs_sysfs_fbo_descriptor[] = {
	&dev_attr_fbo_version.attr,
	&dev_attr_fbo_recommended_lba_range_size.attr,
	&dev_attr_fbo_max_lba_range_size.attr,
	&dev_attr_fbo_min_lba_range_size.attr,
	&dev_attr_fbo_max_lba_range_count.attr,
	&dev_attr_fbo_lba_range_alignment.attr, NULL,
};
static const struct attribute_group ufs_sysfs_fbo_descriptor_group = {
	.name = "fbo_descriptor", .attrs = ufs_sysfs_fbo_descriptor,
};
static struct attribute *ufs_sysfs_fbo_attr_descriptor[] = {
	&dev_attr_fbo_control.attr,
	&dev_attr_fbo_execute_threshold.attr,
	&dev_attr_fbo_progress_state.attr, NULL,
};
static const struct attribute_group ufs_sysfs_fbo_attr_descriptor_group = {
	.name = "fbo_attr", .attrs = ufs_sysfs_fbo_attr_descriptor,
};
static const struct attribute_group *ufs_fbo_sysfs_groups[] = {
	&ufs_sysfs_hpb_attr_descriptor_group,
	&ufs_sysfs_fbo_descriptor_group,
	&ufs_sysfs_fbo_attr_descriptor_group, NULL,
};

int ufs_fbo_init(struct ufs_hba *hba)
{
	int ret = sysfs_create_groups(&hba->dev->kobj, ufs_fbo_sysfs_groups);

	if (ret)
		return ret;
	ret = device_create_bin_file(hba->dev, &fbo_sysfs_bin_file);
	if (ret)
		sysfs_remove_groups(&hba->dev->kobj, ufs_fbo_sysfs_groups);
	return ret;
}

void ufs_fbo_exit(struct ufs_hba *hba)
{
	/* Drain the original binary node as well as the attribute groups. */
	device_remove_bin_file(hba->dev, &fbo_sysfs_bin_file);
	sysfs_remove_groups(&hba->dev->kobj, ufs_fbo_sysfs_groups);
}
