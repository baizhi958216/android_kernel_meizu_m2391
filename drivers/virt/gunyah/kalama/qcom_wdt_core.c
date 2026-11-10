// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm Gunyah apps watchdog policy used by the M2391 boot firmware. */
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/cpuhotplug.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kdebug.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/panic_notifier.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/soc/qcom/apss_minidump.h>
#include <linux/timekeeping.h>
#include <linux/suspend.h>
#include "kalama-watchdog.h"
#include "mz_enhance_qcom_wdog.h"

/* Flyme 12.6 qcom_wdt_register stores these millisecond values as a pair. */
#define KALAMA_PET_MS	15360
#define KALAMA_BARK_MS	23000
#define KALAMA_BITE_MS	(KALAMA_BARK_MS + 3000)

/* The bootloader copies just this pointer-free record on an APSS dump. */
struct kalama_wdt_dump {
	u32 version, size;
	u32 pet_ms, bark_ms, bite_ms, irq;
	u64 ping_started_ns, last_pet_ns, bark_ns;
	u64 expected_cpus, acknowledged_cpus;
	s32 last_pet_ret;
	u32 bark_elapsed_ms;
};

struct kalama_wdt {
	struct device *dev;
	const struct kalama_wdt_ops *ops;
	struct mutex lock;
	struct kthread_worker *worker;
	struct kthread_delayed_work pet;
	struct notifier_block cpu_nb, die_nb, panic_nb;
	struct hlist_node hp_node;
	struct kalama_wdt * __percpu *ppi_data;
	int irq, hp_state;
	bool armed, suspended, wakeup, pm_stopped, disabled;
	int idle[NR_CPUS];
	cpumask_t alive;
	struct kalama_wdt_dump dump;
};

static int kalama_wdt_start(struct kalama_wdt *wdt);

static int kalama_wdt_heartbeat(struct kalama_wdt *wdt)
{
	int ret = wdt->ops->pet();

	WRITE_ONCE(wdt->dump.last_pet_ret, ret);
	if (!ret)
		WRITE_ONCE(wdt->dump.last_pet_ns, ktime_get_boottime_ns());
	return ret;
}

static void kalama_wdt_ack(void *data)
{
	struct kalama_wdt *wdt = data;

	cpumask_set_cpu(smp_processor_id(), &wdt->alive);
	WRITE_ONCE(wdt->dump.acknowledged_cpus, cpumask_bits(&wdt->alive)[0]);
}

static void kalama_wdt_pet(struct kthread_work *work)
{
	struct kalama_wdt *wdt = container_of(work, struct kalama_wdt, pet.work);
	cpumask_t ping;
	int cpu, ret = 0;

	mutex_lock(&wdt->lock);
	if (wdt->disabled || wdt->suspended)
		goto unlock;
	if (wdt->pm_stopped) {
		ret = kalama_wdt_start(wdt);
		if (ret)
			goto requeue;
		wdt->pm_stopped = false;
	}
	if (!wdt->armed)
		goto unlock;

	cpus_read_lock();
	mz_get_cpu_ping_mask(&ping, wdt->idle);
	cpumask_clear(&wdt->alive);
	WRITE_ONCE(wdt->dump.acknowledged_cpus, 0);
	WRITE_ONCE(wdt->dump.expected_cpus, cpumask_bits(&ping)[0]);
	WRITE_ONCE(wdt->dump.ping_started_ns, ktime_get_boottime_ns());
	for_each_cpu(cpu, &ping) {
		ret = smp_call_function_single(cpu, kalama_wdt_ack, wdt, true);
		if (ret)
			break;
	}
	cpus_read_unlock();

	if (!ret)
		ret = kalama_wdt_heartbeat(wdt);
	requeue:
	if (ret)
		dev_err_ratelimited(wdt->dev, "heartbeat failed: %d\n", ret);

	/* Retry a transient transport failure before the bark deadline. */
	kthread_queue_delayed_work(wdt->worker, &wdt->pet,
				   msecs_to_jiffies(ret ? 1000 : KALAMA_PET_MS));
unlock:
	mutex_unlock(&wdt->lock);
}

static int kalama_wdt_cpu_pm(struct notifier_block *nb, unsigned long event, void *data)
{
	struct kalama_wdt *wdt = container_of(nb, struct kalama_wdt, cpu_nb);
	int *idle = &wdt->idle[raw_smp_processor_id()];

	if (event == CPU_PM_ENTER)
		WRITE_ONCE(*idle, 1);
	else if (event == CPU_PM_ENTER_FAILED || event == CPU_PM_EXIT)
		WRITE_ONCE(*idle, 0);
	return NOTIFY_OK;
}

static irqreturn_t kalama_wdt_bark(int irq, void *data)
{
	struct kalama_wdt *wdt = data;
	bool enabled;
	u32 elapsed;

	WRITE_ONCE(wdt->dump.bark_ns, ktime_get_boottime_ns());
	if (!wdt->ops->status(&enabled, &elapsed)) {
		WRITE_ONCE(wdt->dump.bark_elapsed_ms, elapsed);
		dev_emerg(wdt->dev, "watchdog bark, enabled %d, elapsed %u ms\n",
			  enabled, elapsed);
	}
	dev_emerg(wdt->dev, "last CPU acknowledgments %*pbl\n", cpumask_pr_args(&wdt->alive));
	mz_dump_wdog_cpu(wdt->worker->task);
	panic("Kalama Gunyah apps watchdog bark");
}

static irqreturn_t kalama_wdt_percpu_bark(int irq, void *data)
{
	return kalama_wdt_bark(irq, *(struct kalama_wdt **)data);
}

static int kalama_wdt_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct kalama_wdt *wdt = hlist_entry(node, struct kalama_wdt, hp_node);

	enable_percpu_irq(wdt->irq, IRQ_TYPE_NONE);
	return 0;
}

static int kalama_wdt_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct kalama_wdt *wdt = hlist_entry(node, struct kalama_wdt, hp_node);

	disable_percpu_irq(wdt->irq);
	return 0;
}

static void kalama_wdt_free_ppi(void *data)
{
	struct kalama_wdt *wdt = data;

	cpuhp_state_remove_instance(wdt->hp_state, &wdt->hp_node);
	cpuhp_remove_multi_state(wdt->hp_state);
	free_percpu_irq(wdt->irq, wdt->ppi_data);
}

static int kalama_wdt_request_irq(struct kalama_wdt *wdt)
{
	int cpu, ret;

	if (!irq_is_percpu(wdt->irq))
		return devm_request_irq(wdt->dev, wdt->irq, kalama_wdt_bark,
					IRQF_NO_SUSPEND, "apps_wdog_bark", wdt);

	wdt->ppi_data = devm_alloc_percpu(wdt->dev, struct kalama_wdt *);
	if (!wdt->ppi_data)
		return -ENOMEM;
	for_each_possible_cpu(cpu)
		*per_cpu_ptr(wdt->ppi_data, cpu) = wdt;
	ret = request_percpu_irq(wdt->irq, kalama_wdt_percpu_bark,
				 "apps_wdog_bark", wdt->ppi_data);
	if (ret)
		return ret;
	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "gunyah/watchdog:online",
				      kalama_wdt_cpu_online, kalama_wdt_cpu_offline);
	if (ret < 0)
		goto free_irq;
	wdt->hp_state = ret;
	ret = cpuhp_state_add_instance(wdt->hp_state, &wdt->hp_node);
	if (ret) {
		cpuhp_remove_multi_state(wdt->hp_state);
		goto free_irq;
	}
	return devm_add_action_or_reset(wdt->dev, kalama_wdt_free_ppi, wdt);
free_irq:
	free_percpu_irq(wdt->irq, wdt->ppi_data);
	return ret;
}

static int kalama_wdt_die(struct notifier_block *nb, unsigned long event, void *data)
{
	struct kalama_wdt *wdt = container_of(nb, struct kalama_wdt, die_nb);

	if (READ_ONCE(wdt->armed)) {
		wdt->ops->set_time(15000, 15000);
		kalama_wdt_heartbeat(wdt);
	}
	return NOTIFY_DONE;
}

static int kalama_wdt_panic(struct notifier_block *nb, unsigned long event, void *data)
{
	struct kalama_wdt *wdt = container_of(nb, struct kalama_wdt, panic_nb);
	u16 timeout;

	if (!READ_ONCE(wdt->armed))
		return NOTIFY_DONE;
	if (!panic_timeout) {
		wdt->ops->enable(false);
	} else {
		/* 0xffff tells Gunyah to leave an individual timeout unchanged. */
		timeout = min_t(u32, max(panic_timeout, 0), 55) * 1000 + 10000;
		wdt->ops->set_time(timeout, timeout);
		kalama_wdt_heartbeat(wdt);
	}
	return NOTIFY_DONE;
}

static void kalama_wdt_unregister_notifiers(void *data)
{
	struct kalama_wdt *wdt = data;

	atomic_notifier_chain_unregister(&panic_notifier_list, &wdt->panic_nb);
	unregister_die_notifier(&wdt->die_nb);
	cpu_pm_unregister_notifier(&wdt->cpu_nb);
}

static int kalama_wdt_register_notifiers(struct kalama_wdt *wdt)
{
	int ret;

	wdt->cpu_nb.notifier_call = kalama_wdt_cpu_pm;
	wdt->die_nb.notifier_call = kalama_wdt_die;
	wdt->die_nb.priority = INT_MAX - 1;
	wdt->panic_nb.notifier_call = kalama_wdt_panic;
	ret = cpu_pm_register_notifier(&wdt->cpu_nb);
	if (ret)
		return ret;
	ret = register_die_notifier(&wdt->die_nb);
	if (ret)
		goto unregister_cpu;
	ret = atomic_notifier_chain_register(&panic_notifier_list, &wdt->panic_nb);
	if (ret) {
		unregister_die_notifier(&wdt->die_nb);
		goto unregister_cpu;
	}
	return devm_add_action_or_reset(wdt->dev, kalama_wdt_unregister_notifiers, wdt);
unregister_cpu:
	cpu_pm_unregister_notifier(&wdt->cpu_nb);
	return ret;
}

static int kalama_wdt_start(struct kalama_wdt *wdt)
{
	bool enabled;
	u32 elapsed;
	int ret;

	ret = wdt->ops->status(&enabled, &elapsed);
	if (ret)
		return ret;
	ret = wdt->ops->set_time(KALAMA_BARK_MS, KALAMA_BITE_MS);
	if (ret)
		return ret;
	if (!enabled) {
		ret = wdt->ops->enable(true);
		if (ret)
			return ret;
	}
	WRITE_ONCE(wdt->armed, true);
	ret = kalama_wdt_heartbeat(wdt);
	if (ret && !wdt->ops->enable(false))
		WRITE_ONCE(wdt->armed, false);
	return ret;
}

static void kalama_wdt_stop(void *data)
{
	struct kalama_wdt *wdt = data;

	mutex_lock(&wdt->lock);
	wdt->suspended = true;
	wdt->pm_stopped = false;
	if (wdt->armed && wdt->ops->enable(false))
		dev_err(wdt->dev, "failed to stop virtual watchdog\n");
	WRITE_ONCE(wdt->armed, false);
	mutex_unlock(&wdt->lock);
	kthread_cancel_delayed_work_sync(&wdt->pet);
}

void kalama_wdt_shutdown(struct platform_device *pdev)
{
	kalama_wdt_stop(platform_get_drvdata(pdev));
}
EXPORT_SYMBOL_GPL(kalama_wdt_shutdown);

static ssize_t disable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kalama_wdt *wdt = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", READ_ONCE(wdt->disabled));
}

static ssize_t disable_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct kalama_wdt *wdt = dev_get_drvdata(dev);
	u8 value;
	int ret;

	ret = kstrtou8(buf, 10, &value);
	if (ret || value != 1)
		return ret ?: -EINVAL;
	mutex_lock(&wdt->lock);
	if (wdt->disabled) {
		ret = 0;
	} else if (wdt->suspended) {
		ret = -EBUSY;
	} else {
		ret = qcom_scm_sec_wdog_deactivate();
		if (!ret)
			ret = wdt->ops->enable(false);
		if (!ret) {
			WRITE_ONCE(wdt->disabled, true);
			WRITE_ONCE(wdt->armed, false);
		}
	}
	mutex_unlock(&wdt->lock);
	return ret ?: count;
}
static DEVICE_ATTR_ADMIN_RW(disable);

static ssize_t wakeup_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kalama_wdt *wdt = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", READ_ONCE(wdt->wakeup));
}

static ssize_t wakeup_enable_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct kalama_wdt *wdt = dev_get_drvdata(dev);
	u8 value;
	int ret = kstrtou8(buf, 10, &value);

	if (ret || value > 1)
		return ret ?: -EINVAL;
	mutex_lock(&wdt->lock);
	WRITE_ONCE(wdt->wakeup, !!value);
	mutex_unlock(&wdt->lock);
	return count;
}
static DEVICE_ATTR_ADMIN_RW(wakeup_enable);

static struct attribute *kalama_wdt_attrs[] = {
	&dev_attr_disable.attr, &dev_attr_wakeup_enable.attr, NULL,
};
static const struct attribute_group kalama_wdt_group = {
	.attrs = kalama_wdt_attrs,
};

static int kalama_wdt_pause(struct device *dev, bool freeze)
{
	struct kalama_wdt *wdt = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&wdt->lock);
	if (wdt->armed) {
		ret = kalama_wdt_heartbeat(wdt);
		if (!ret && (!wdt->wakeup || freeze)) {
			ret = wdt->ops->enable(false);
			if (!ret) {
				wdt->pm_stopped = true;
				WRITE_ONCE(wdt->armed, false);
			}
		}
	}
	if (!ret)
		wdt->suspended = true;
	mutex_unlock(&wdt->lock);
	if (!ret)
		kthread_cancel_delayed_work_sync(&wdt->pet);
	return ret;
}

static int kalama_wdt_suspend(struct device *dev)
{
	return kalama_wdt_pause(dev, pm_suspend_via_firmware());
}

static int kalama_wdt_freeze(struct device *dev)
{
	return kalama_wdt_pause(dev, true);
}

static int kalama_wdt_resume(struct device *dev)
{
	struct kalama_wdt *wdt = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&wdt->lock);
	if (wdt->pm_stopped) {
		ret = kalama_wdt_start(wdt);
		if (!ret)
			wdt->pm_stopped = false;
	} else if (wdt->armed) {
		ret = kalama_wdt_heartbeat(wdt);
	}
	wdt->suspended = false;
	if (wdt->armed || wdt->pm_stopped)
		kthread_queue_delayed_work(wdt->worker, &wdt->pet,
					   msecs_to_jiffies(ret ? 1000 : KALAMA_PET_MS));
	mutex_unlock(&wdt->lock);
	return ret;
}

const struct dev_pm_ops kalama_wdt_pm = {
	.suspend_late = kalama_wdt_suspend,
	.resume_early = kalama_wdt_resume,
	.freeze_late = kalama_wdt_freeze,
	.thaw_early = kalama_wdt_resume,
	.poweroff_late = kalama_wdt_freeze,
	.restore_early = kalama_wdt_resume,
};
EXPORT_SYMBOL_GPL(kalama_wdt_pm);

static void kalama_wdt_destroy_worker(void *data)
{
	kthread_destroy_worker(data);
}

int kalama_wdt_register(struct platform_device *pdev, const struct kalama_wdt_ops *ops)
{
	struct device *dev = &pdev->dev;
	struct kalama_wdt *wdt;
	int ret;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;
	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;
	wdt->dev = dev;
	wdt->ops = ops;
	wdt->wakeup = true;
	wdt->irq = platform_get_irq(pdev, 0);
	if (wdt->irq < 0)
		return wdt->irq;
	wdt->dump.version = 1;
	wdt->dump.size = sizeof(wdt->dump);
	wdt->dump.pet_ms = KALAMA_PET_MS;
	wdt->dump.bark_ms = KALAMA_BARK_MS;
	wdt->dump.bite_ms = KALAMA_BITE_MS;
	wdt->dump.irq = wdt->irq;
	wdt->dump.bark_elapsed_ms = U32_MAX;
	ret = qcom_devm_minidump_add(dev, "KWDOGDATA", &wdt->dump, sizeof(wdt->dump));
	if (ret)
		dev_warn(dev, "watchdog crash data unavailable: %d\n", ret);
	mutex_init(&wdt->lock);
	platform_set_drvdata(pdev, wdt);
	wdt->worker = kthread_run_worker(0, "gh-watchdog");
	if (IS_ERR(wdt->worker))
		return PTR_ERR(wdt->worker);
	sched_set_fifo(wdt->worker->task);
	kthread_init_delayed_work(&wdt->pet, kalama_wdt_pet);
	ret = devm_add_action_or_reset(dev, kalama_wdt_destroy_worker, wdt->worker);
	if (ret)
		return ret;
	ret = kalama_wdt_request_irq(wdt);
	if (ret)
		return ret;
	ret = kalama_wdt_register_notifiers(wdt);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, kalama_wdt_stop, wdt);
	if (ret)
		return ret;
	ret = kalama_wdt_start(wdt);
	if (ret)
		return dev_err_probe(dev, ret, "starting Gunyah watchdog\n");
	kthread_queue_delayed_work(wdt->worker, &wdt->pet, msecs_to_jiffies(KALAMA_PET_MS));
	return devm_device_add_group(dev, &kalama_wdt_group);
}
EXPORT_SYMBOL_GPL(kalama_wdt_register);

MODULE_DESCRIPTION("Kalama apps watchdog heartbeat and crash diagnostics");
MODULE_LICENSE("GPL");
