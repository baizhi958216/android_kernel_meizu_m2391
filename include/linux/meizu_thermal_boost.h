/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_THERMAL_BOOST_H
#define _MEIZU_THERMAL_BOOST_H
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
void mz_thermal_sched_boost_request(int enable);
void mz_thermal_boost_store(struct freq_qos_request *qos_req,
		struct cpufreq_policy *policy, unsigned int *frequency);
void mz_thermal_boost_restore(struct cpufreq_policy *policy, unsigned int frequency);
void mz_thermal_boost_request(struct cpufreq_policy *policy, unsigned int frequency);
/* Source consumers must detach before freq_qos_remove_request/freeing storage.
 * Sleeps until already queued updates finish; do not invoke from the worker.
 */
void mz_thermal_boost_forget(struct freq_qos_request *qos_req);
#endif
