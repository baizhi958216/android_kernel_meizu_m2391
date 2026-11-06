// SPDX-License-Identifier: GPL-2.0-only
//Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

/*
 * This driver exposes API for aDSP service layers to intimate session
 * start and stop. Based on the aDSP sessions and activity information
 * derived from SMEM statistics, the driver detects and acts on
 * possible aDSP sleep (voting related) issues.
 */

#define pr_fmt(fmt) "adsp_sleepmon: " fmt

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/rpmsg.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/sysmon_subsystem_stats.h>
#include <linux/uaccess.h>
#include <uapi/misc/adsp_sleepmon.h>

#define QDSPPM_CLIENT_NAME_SIZE_SLEEPMON 24
#define QDSPPM_NUM_OF_CLIENTS_SLEEPMON 16
#define SLEEPMON_PROCESS_NAME_SIZE 20
#define SLEEPMON_VERSION 1
#define SLEEPMON_CLIENTS 2
#define SLEEPMON_LPI_DETECT 3
#define SLEEPMON_SSR 4
#define SLEEPMON_LPI_PANIC 5
#define SLEEPMON_USLEEP_CLIENT 6
#define SLEEPMON_DEVICE "msm_adsp_sleepmon"

struct sleepmon_mmpmmipsreqtype {
	u32 total;
	u32 per_thread;
};

struct sleepmon_mmpmbusbwdatausagetype {
	u64 bytes_persec;
	u32 usage_percentage;
};

struct sleepmon_mmpmmppsreqtype {
	u32 total;
	u32 floor_clock;
};

enum islandparticipationoptionstype {
	MMPM_ISLAND_PARTICIPATION_NONE            = 0x0,
	MMPM_ISLAND_PARTICIPATION_LPI             = 0x1,
	MMPM_ISLAND_PARTICIPATION_DISALLOW_LPI    = 0x2,
	MMPM_ISLAND_PARTICIPATION_UIMAGE          = 0x4,
	MMPM_ISLAND_PARTICIPATION_DISALLOW_UIMAGE = 0x8,
};

enum dsppm_client_data_fetch_status {
	DSPPM_CLIENT_DATA_FETCH_SUCCESS = 0x1,
	DSPPM_CLIENT_DATA_FAILED_ONGOING_COHERENT = 0x2,
	DSPPM_CLIENT_DATA_FAILED_DSPPM_RETURNED_NULL = 0x3,
};

struct islandparticipationtype {
	u32 islandopt; //islandparticipationoptionstype
};

struct sleepmon_qdsppmrequest {
	char clientname[QDSPPM_CLIENT_NAME_SIZE_SLEEPMON];
	u32 clientid;
	u64 timestamp;
	u8 b_valid;
	u32 poweron;
	struct sleepmon_mmpmmipsreqtype mips;
	struct sleepmon_mmpmmppsreqtype mpps;
	struct sleepmon_mmpmbusbwdatausagetype bw;
	u32 sleeplatency;
	char process[SLEEPMON_PROCESS_NAME_SIZE];
	struct islandparticipationtype islandparticipation;
	u32 reserved_1;
	u32 reserved_2;
	u32 reserved_3;
	u32 reserved_4;
	u32 reserved_5;
	u32 reserved_6;
};

struct sleepmon_mmpmaggregatedmipsdata {
	u32 mips;
	u32 mpps;
	u32 clock;
	u32 kfactor;
};

struct sleepmon_mmpmaggregatedbwtype {
	u64 adsp_ddr_ab;
	u64 adsp_ddr_ib;
	u64 snoc_ddr_ab;
	u64 snoc_ddr_ib;
	u32 ddr_latency;
	u32 snoc_latency;
};

struct sleepmon_mmpmaggregatedahbtype {
	u32 ahbe_hz;
	u32 ahbi_hz;
};

struct sleepmon_qdsppm_aggdata {
	struct sleepmon_mmpmaggregatedmipsdata agg_mips;
	struct sleepmon_mmpmaggregatedbwtype agg_bw;
	struct sleepmon_mmpmaggregatedahbtype agg_ahb;
	u32 agg_latency;
	u32 reserved_3;
	u32 reserved_4;
	u32 reserved_5;
	u32 reserved_6;
	u32 reserved_7;
	u32 reserved_8;
};

struct sleepmon_qdsppm_clients {
	u8 update_flag;
	u64 timestamp;
	struct sleepmon_qdsppmrequest clients[QDSPPM_NUM_OF_CLIENTS_SLEEPMON];
	struct sleepmon_qdsppm_aggdata agg_data;
	u8 result;
	u32 num_clients;
	u32 sysmon_qdsppm_version;
	u32 reserved_1;
	u32 reserved_2;
	u32 reserved_3;
	u32 reserved_4;
	u32 reserved_5;
	u32 reserved_6;
	u32 reserved_7;
	u32 reserved_8;
};

struct sleepmon_usleep_npa_clients {
	char clientname[67];
	u32 request;
};

struct sleepmon_rx_msg_t {
	u32 size;
	u32 ver_info;
	u32 feature_id;
	union {
		struct sleepmon_qdsppm_clients dsppm_clients;
		int lpi_panic;
		struct sleepmon_usleep_npa_clients sleepmon_usleep_npa;
	} featureStruct;
};

struct sleepmon_lpi_issue {
	u8 lpi_issue_detect;
	u8 panic_enable;
	u8 ssr_enable;
};

struct sleepmon_tx_msg_t {
	u32 size;
	u32 adsp_ver_info;
	u32 feature_id;
	union {
		u32 dsppm_client_signal;
		struct sleepmon_lpi_issue sleepmon_lpi_detect;
		u32 send_ssr_command;
	} fs;
};

struct sleepmon_client {
	struct list_head node;
	u32 normal;
	u32 island;
};

static DEFINE_MUTEX(monitor_lock);
static DEFINE_SPINLOCK(rx_lock);
static LIST_HEAD(audio_clients);
static struct rpmsg_device *endpoint;
static u32 version, generation, processed_generation;
static bool pending_panic;
static bool remote_policy[3], default_ap_policy[2], ap_policy[2];
static struct sleepmon_qdsppm_clients dsppm_clients;
static DECLARE_COMPLETION(clients_reply);
static unsigned int wait_seconds[] = { 5, 15 };
static struct class *monitor_class;
static dev_t monitor_devno;
static struct cdev monitor_cdev;
static struct dentry *monitor_dir;
static u32 overall_time = 60, min_resumes = 5, failed_resumes;
static u64 failed_ns, check_since, baseline;
static int expected_mode = -1;
static bool have_baseline;
static void sleepmon_check(struct work_struct *unused);
static void sleepmon_events(struct work_struct *unused);
static DECLARE_WORK(events_work, sleepmon_events);
static DECLARE_DELAYED_WORK(monitor_work, sleepmon_check);

static int residency(int mode, u64 *accumulated)
{
	struct sleep_stats lpm;
	struct sleep_stats_island lpi;
	int ret;

	if (mode) {
		ret = sysmon_stats_query_sleep(ADSP, NULL, &lpi);
		if (!ret)
			*accumulated = lpi.accumulated;
	} else {
		ret = sysmon_stats_query_sleep(ADSP, &lpm, NULL);
		if (!ret)
			*accumulated = lpm.accumulated;
	}
	return ret;
}

static void arm_monitor(void)
{
	struct sleepmon_client *client;
	bool any_lpi = false;
	int mode = 0;

	list_for_each_entry(client, &audio_clients, node) {
		if (client->normal) {
			mode = -1;
			break;
		}
		any_lpi |= !!client->island;
	}
	if (!mode && any_lpi)
		mode = 1;
	if (expected_mode != mode || mode != 0) {
		failed_ns = 0;
		failed_resumes = 0;
	}
	expected_mode = mode;
	cancel_delayed_work(&monitor_work);
	have_baseline = mode >= 0 && endpoint && !residency(mode, &baseline);
	if (have_baseline) {
		check_since = ktime_get_boottime_ns();
		mod_delayed_work(system_freezable_wq, &monitor_work, secs_to_jiffies(wait_seconds[mode]));
	}
}

static int send_request(u32 feature, u32 parameter)
{
	struct sleepmon_tx_msg_t msg = {
		.size = sizeof(msg), .adsp_ver_info = 2, .feature_id = feature,
		.fs.dsppm_client_signal = parameter,
	};

	lockdep_assert_held(&monitor_lock);
	if (!endpoint || READ_ONCE(version) < (feature == SLEEPMON_CLIENTS ? 1 : 2))
		return -ENOTCONN;
	return rpmsg_send(endpoint->ept, &msg, sizeof(msg));
}

static bool audio_pd_active(void)
{
	struct pd_stats { u32 version, latency, timestamp; struct { u32 pid, active; } pd[5]; };
	const struct pd_stats *stats;
	size_t size;
	int i;

	stats = qcom_smem_get(ADSP, 624, &size);
	if (IS_ERR(stats) || size < sizeof(*stats))
		return false;
	for (i = 0; i < ARRAY_SIZE(stats->pd); i++)
		if ((READ_ONCE(stats->pd[i].pid) & 0x7f) == 2 && READ_ONCE(stats->pd[i].active))
			return true;
	return false;
}

static void session_reset(void)
{
	struct sleepmon_client *client;

	list_for_each_entry(client, &audio_clients, node) {
		client->normal = 0;
		client->island = 0;
	}
	failed_ns = 0;
	failed_resumes = 0;
	arm_monitor();
}

static void update_session_generation(void)
{
	u32 current_gen = READ_ONCE(generation);

	if (processed_generation != current_gen) {
		processed_generation = current_gen;
		session_reset();
	}
}

static void check_residency(bool resume)
{
	int mode = expected_mode;
	u64 accumulated, now, elapsed;

	if (!have_baseline || !endpoint || mode < 0)
		return;
	now = ktime_get_boottime_ns();
	elapsed = now - check_since;
	if (elapsed < (u64)wait_seconds[mode] * NSEC_PER_SEC)
		return;
	if (residency(mode, &accumulated))
		return;

	if (baseline == accumulated) {
		dev_warn_ratelimited(&endpoint->dev, "no ADSP %s residency over %llu ms\n",
				     mode ? "island" : "deep sleep", div_u64(elapsed, NSEC_PER_MSEC));
		send_request(SLEEPMON_CLIENTS, 1);
		if (mode) {
			send_request(SLEEPMON_LPI_DETECT, 1 | ((ap_policy[1] || remote_policy[1]) << 8));
		} else if ((remote_policy[0] || ap_policy[0]) && audio_pd_active()) {
			if (remote_policy[0])
				send_request(SLEEPMON_SSR, 1);
			else if (ap_policy[0])
				panic("ADSP sleep issue detected");
		} else {
			failed_ns += elapsed;
			failed_resumes += resume;
			if (remote_policy[2] && failed_ns >= (u64)overall_time * NSEC_PER_SEC &&
			    failed_resumes >= min_resumes) {
				send_request(SLEEPMON_SSR, 1);
				failed_ns = 0;
				failed_resumes = 0;
			}
		}
	} else {
		failed_ns = 0;
		failed_resumes = 0;
	}
	baseline = accumulated;
	check_since = now;
}

static void sleepmon_events(struct work_struct *unused)
{
	bool anomaly;
	unsigned long flags;

	mutex_lock(&monitor_lock);
	spin_lock_irqsave(&rx_lock, flags);
	anomaly = pending_panic;
	pending_panic = false;
	spin_unlock_irqrestore(&rx_lock, flags);
	update_session_generation();
	if (anomaly && endpoint) {
		if (ap_policy[1])
			panic("ADSP island sleep issue detected");
		if (remote_policy[1])
			send_request(SLEEPMON_SSR, 1);
	}
	mutex_unlock(&monitor_lock);
}

static void sleepmon_check(struct work_struct *unused)
{
	mutex_lock(&monitor_lock);
	update_session_generation();
	check_residency(false);
	mutex_unlock(&monitor_lock);
}

static int sleepmon_resume(struct notifier_block *nb, unsigned long event, void *unused)
{
	if (event == PM_POST_SUSPEND) {
		mutex_lock(&monitor_lock);
		update_session_generation();
		check_residency(true);
		mutex_unlock(&monitor_lock);
	}
	return NOTIFY_DONE;
}
static struct notifier_block resume_nb = { .notifier_call = sleepmon_resume };

static int monitor_open(struct inode *inode, struct file *file)
{
	struct sleepmon_client *client;
	u64 sample;
	int ret;

	ret = residency(0, &sample);
	if (ret)
		return -ENODEV;
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	mutex_lock(&monitor_lock);
	list_add_tail(&client->node, &audio_clients);
	file->private_data = client;
	mutex_unlock(&monitor_lock);
	return 0;
}

static int monitor_release(struct inode *inode, struct file *file)
{
	struct sleepmon_client *client = file->private_data;

	mutex_lock(&monitor_lock);
	list_del(&client->node);
	if (client->normal || client->island)
		arm_monitor();
	mutex_unlock(&monitor_lock);
	kfree(client);
	return 0;
}

static long monitor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sleepmon_client *client = file->private_data;
	struct adspsleepmon_ioctl_audio activity;
	struct adspsleepmon_ioctl_panic policy;
	void __user *ptr = (void __user *)arg;
	int ret = 0;

	switch (cmd) {
	case ADSPSLEEPMON_IOCTL_AUDIO_ACTIVITY:
		if (copy_from_user(&activity, ptr, sizeof(activity)))
			return -EFAULT;
		if (activity.version < ADSPSLEEPMON_IOCTL_AUDIO_VER_1)
			return -ENOTTY;
		mutex_lock(&monitor_lock);
		update_session_generation();
		switch (activity.command) {
		case ADSPSLEEPMON_AUDIO_ACTIVITY_START:
			if (client->normal == U32_MAX)
				ret = -EOVERFLOW;
			else
				client->normal++;
			break;
		case ADSPSLEEPMON_AUDIO_ACTIVITY_STOP:
			if (client->normal)
				client->normal--;
			break;
		case ADSPSLEEPMON_AUDIO_ACTIVITY_LPI_START:
			if (client->island == U32_MAX)
				ret = -EOVERFLOW;
			else
				client->island++;
			break;
		case ADSPSLEEPMON_AUDIO_ACTIVITY_LPI_STOP:
			if (client->island)
				client->island--;
			break;
		case ADSPSLEEPMON_AUDIO_ACTIVITY_RESET:
			client->normal = 0;
			client->island = 0;
			break;
		default:
			ret = -EINVAL;
		}
		if (!ret)
			arm_monitor();
		mutex_unlock(&monitor_lock);
		return ret;
	case ADSPSLEEPMON_IOCTL_CONFIGURE_PANIC:
		if (copy_from_user(&policy, ptr, sizeof(policy)))
			return -EFAULT;
		if (policy.version < ADSPSLEEPMON_IOCTL_CONFIG_PANIC_VER_1)
			return -ENOTTY;
		mutex_lock(&monitor_lock);
		switch (policy.command) {
		case ADSPSLEEPMON_DISABLE_PANIC_LPM:
			ap_policy[0] = false;
			break;
		case ADSPSLEEPMON_DISABLE_PANIC_LPI:
			ap_policy[1] = false;
			break;
		case ADSPSLEEPMON_RESET_PANIC_LPM:
			ap_policy[0] = default_ap_policy[0];
			break;
		case ADSPSLEEPMON_RESET_PANIC_LPI:
			ap_policy[1] = default_ap_policy[1];
			break;
		default:
			ret = -EINVAL;
		}
		mutex_unlock(&monitor_lock);
		return ret;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations monitor_fops = {
	.owner = THIS_MODULE, .open = monitor_open, .release = monitor_release,
	.unlocked_ioctl = monitor_ioctl, .compat_ioctl = compat_ptr_ioctl,
};

static int sleepmon_rx(struct rpmsg_device *rpdev, void *data, int len, void *priv, u32 addr)
{
	const struct sleepmon_rx_msg_t *msg = data;
	unsigned long flags;
	size_t header = offsetof(struct sleepmon_rx_msg_t, feature_id) + sizeof(msg->feature_id);

	if (len < (int)header || msg->size < header || msg->size > len)
		return -EBADMSG;
	spin_lock_irqsave(&rx_lock, flags);
	if (endpoint != rpdev)
		goto out;
	switch (msg->feature_id) {
	case SLEEPMON_VERSION:
		version = msg->ver_info;
		generation++;
		memset(&dsppm_clients, 0, sizeof(dsppm_clients));
		schedule_work(&events_work);
		break;
	case SLEEPMON_CLIENTS:
		if (msg->size < offsetofend(struct sleepmon_rx_msg_t, featureStruct.dsppm_clients))
			break;
		dsppm_clients = msg->featureStruct.dsppm_clients;
		complete(&clients_reply);
		break;
	case SLEEPMON_LPI_PANIC:
		pending_panic = true;
		schedule_work(&events_work);
		break;
	case SLEEPMON_USLEEP_CLIENT:
		if (msg->size >= offsetofend(struct sleepmon_rx_msg_t, featureStruct.sleepmon_usleep_npa))
			dev_dbg(&rpdev->dev, "island client %.*s vote %u\n", 67,
				msg->featureStruct.sleepmon_usleep_npa.clientname,
				msg->featureStruct.sleepmon_usleep_npa.request);
		break;
	}
out:
	spin_unlock_irqrestore(&rx_lock, flags);
	return 0;
}

static int monitor_stats_show(struct seq_file *s, void *unused)
{
	struct sleepmon_qdsppm_clients *stats;
	struct sysmon_smem_q6_event_stats votes;
	unsigned long flags;
	u64 count;
	unsigned int i;
	bool fresh;

	stats = kmalloc(sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return -ENOMEM;
	mutex_lock(&monitor_lock);
	reinit_completion(&clients_reply);
	fresh = !send_request(SLEEPMON_CLIENTS, 1) &&
		wait_for_completion_timeout(&clients_reply, msecs_to_jiffies(100));
	spin_lock_irqsave(&rx_lock, flags);
	*stats = dsppm_clients;
	spin_unlock_irqrestore(&rx_lock, flags);
	mutex_unlock(&monitor_lock);
	if (fresh && stats->result == DSPPM_CLIENT_DATA_FETCH_SUCCESS) {
		seq_printf(s, "DSPPM timestamp %llu clock %u latency %u\n", stats->timestamp,
			   stats->agg_data.agg_mips.clock, stats->agg_data.agg_latency);
		for (i = 0; i < min_t(u32, ARRAY_SIZE(stats->clients), stats->num_clients); i++) {
			const struct sleepmon_qdsppmrequest *client = &stats->clients[i];

			seq_printf(s, "client %u %.*s process %.*s active %u latency %u\n",
				   client->clientid, (int)sizeof(client->clientname), client->clientname,
				   (int)sizeof(client->process), client->process, client->poweron,
				   client->sleeplatency);
		}
	}
	kfree(stats);
	if (!sysmon_stats_query_q6_votes(ADSP, &votes))
		seq_printf(s, "Core clock %u kHz latency %u us\n", votes.QDSP6_clk, votes.Sleep_latency);
	if (!residency(0, &count))
		seq_printf(s, "LPM accumulated %llu\n", count);
	if (!residency(1, &count))
		seq_printf(s, "LPI accumulated %llu\n", count);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(monitor_stats);

static int policy_get(void *remote, u64 *val)
{
	mutex_lock(&monitor_lock);
	if (remote)
		*val = remote_policy[0] | (remote_policy[1] << 1) | (remote_policy[2] << 2);
	else
		*val = ap_policy[0] | (ap_policy[1] << 1);
	mutex_unlock(&monitor_lock);
	return 0;
}

static int policy_set(void *remote, u64 val)
{
	int ret = 0;

	mutex_lock(&monitor_lock);
	if (!remote || val > 7)
		ret = -EINVAL;
	else if (!endpoint || READ_ONCE(version) < 2)
		ret = -ENOTCONN;
	else {
		remote_policy[0] = val & BIT(0);
		remote_policy[1] = val & BIT(1);
		remote_policy[2] = val & BIT(2);
	}
	mutex_unlock(&monitor_lock);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(policy_fops, policy_get, policy_set, "%llu\n");

static void load_policy(struct device_node *edge)
{
	struct device_node *np = of_get_compatible_child(edge, "qcom,adsp-sleepmon");

	wait_seconds[0] = 5;
	wait_seconds[1] = 15;
	overall_time = 60;
	min_resumes = 5;
	if (np) {
		of_property_read_u32(np, "qcom,wait_time_lpm", &wait_seconds[0]);
		of_property_read_u32(np, "qcom,wait_time_lpi", &wait_seconds[1]);
		of_property_read_u32(np, "qcom,wait_time_lpm_overall", &overall_time);
		of_property_read_u32(np, "qcom,min_required_resumes", &min_resumes);
	}
	default_ap_policy[0] = of_property_read_bool(np, "qcom,enable_panic_lpm");
	default_ap_policy[1] = of_property_read_bool(np, "qcom,enable_panic_lpi");
	memcpy(ap_policy, default_ap_policy, sizeof(ap_policy));
	remote_policy[0] = of_property_read_bool(np, "qcom,enable_adsp_panic_lpm");
	remote_policy[1] = of_property_read_bool(np, "qcom,enable_adsp_panic_lpi");
	remote_policy[2] = of_property_read_bool(np, "qcom,enable_adsp_panic_lpm_overall");
	of_node_put(np);
}

static int sleepmon_probe(struct rpmsg_device *rpdev)
{
	unsigned long flags;
	int ret = 0;

	mutex_lock(&monitor_lock);
	spin_lock_irqsave(&rx_lock, flags);
	if (endpoint)
		ret = -EBUSY;
	else {
		endpoint = rpdev;
		version = 0;
		generation++;
	}
	spin_unlock_irqrestore(&rx_lock, flags);
	if (!ret) {
		load_policy(rpdev->dev.of_node);
		update_session_generation();
	}
	mutex_unlock(&monitor_lock);
	return ret;
}

static void sleepmon_remove(struct rpmsg_device *rpdev)
{
	unsigned long flags;

	mutex_lock(&monitor_lock);
	spin_lock_irqsave(&rx_lock, flags);
	endpoint = NULL;
	version = 0;
	pending_panic = false;
	generation++;
	spin_unlock_irqrestore(&rx_lock, flags);
	update_session_generation();
	mutex_unlock(&monitor_lock);
}

static const struct of_device_id sleepmon_of[] = {
	{ .compatible = "qcom,msm-adspsleepmon-rpmsg" }, {}
};
MODULE_DEVICE_TABLE(of, sleepmon_of);
static const struct rpmsg_device_id sleepmon_ids[] = {
	{ "sleepmonglink-apps-adsp" }, {}
};
MODULE_DEVICE_TABLE(rpmsg, sleepmon_ids);
static struct rpmsg_driver sleepmon_driver = {
	.drv = { .name = "adsp_sleepmon", .of_match_table = sleepmon_of },
	.id_table = sleepmon_ids, .callback = sleepmon_rx, .probe = sleepmon_probe,
	.remove = sleepmon_remove,
};

static int __init monitor_init(void)
{
	struct device *dev;
	int ret;

	ret = register_pm_notifier(&resume_nb);
	if (ret)
		return ret;
	ret = register_rpmsg_driver(&sleepmon_driver);
	if (ret)
		goto notifier;
	ret = alloc_chrdev_region(&monitor_devno, 0, 1, SLEEPMON_DEVICE);
	if (ret)
		goto unregister;
	monitor_class = class_create("adspsleepmon");
	if (IS_ERR(monitor_class)) {
		ret = PTR_ERR(monitor_class);
		goto region;
	}
	cdev_init(&monitor_cdev, &monitor_fops);
	monitor_cdev.owner = THIS_MODULE;
	ret = cdev_add(&monitor_cdev, monitor_devno, 1);
	if (ret)
		goto destroy_class;
	dev = device_create(monitor_class, NULL, monitor_devno, NULL, SLEEPMON_DEVICE);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto del;
	}
	monitor_dir = debugfs_create_dir("adspsleepmon", NULL);
	debugfs_create_file("master_stats", 0444, monitor_dir, NULL, &monitor_stats_fops);
	debugfs_create_file("adsp_panic_state", 0644, monitor_dir, (void *)1, &policy_fops);
	debugfs_create_file("read_adsp_panic_state", 0444, monitor_dir, (void *)1, &policy_fops);
	debugfs_create_file("read_panic_state", 0444, monitor_dir, NULL, &policy_fops);
	return 0;
del:
	cdev_del(&monitor_cdev);
destroy_class:
	class_destroy(monitor_class);
region:
	unregister_chrdev_region(monitor_devno, 1);
unregister:
	unregister_rpmsg_driver(&sleepmon_driver);
notifier:
	unregister_pm_notifier(&resume_nb);
	cancel_work_sync(&events_work);
	cancel_delayed_work_sync(&monitor_work);
	return ret;
}
module_init(monitor_init);

static void __exit monitor_exit(void)
{
	debugfs_remove_recursive(monitor_dir);
	device_destroy(monitor_class, monitor_devno);
	cdev_del(&monitor_cdev);
	class_destroy(monitor_class);
	unregister_chrdev_region(monitor_devno, 1);
	unregister_rpmsg_driver(&sleepmon_driver);
	unregister_pm_notifier(&resume_nb);
	cancel_work_sync(&events_work);
	cancel_delayed_work_sync(&monitor_work);
}
module_exit(monitor_exit);

MODULE_DESCRIPTION("Qualcomm ADSP audio session and sleep monitor");
MODULE_LICENSE("GPL");
