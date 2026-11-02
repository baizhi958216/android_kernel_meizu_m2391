# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(srctree) -I$(src)
KCOV_INSTRUMENT := n
KCSAN_SANITIZE := n
obj-$(CONFIG_SCHED_WALT) += sched-walt.o
sched-walt-y := walt.o boost.o sched_avg.o walt_halt.o \
	core_ctl.o trace.o input-boost.o sysctl.o \
	cpufreq_walt.o fixup.o walt_lb.o walt_rt.o \
	walt_cfs.o walt_tp.o walt_config.o voter.o \
	walt_cpufreq_cycle_cntr_driver.o walt_gclk_cycle_counter_driver.o walt_cycles.o debugfs.o \
	pipeline.o smart_freq.o walt_storage_lb.o mvp_locking.o
sched-walt-$(CONFIG_SCHED_WALT_STATS) += sysctl_walt_stats.o
