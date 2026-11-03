# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for cpuidle governors.
#

obj-$(CONFIG_CPU_IDLE_GOV_LADDER) += ladder.o
obj-$(CONFIG_CPU_IDLE_GOV_MENU) += menu.o
obj-$(CONFIG_CPU_IDLE_GOV_TEO) += teo.o
obj-$(CONFIG_CPU_IDLE_GOV_HALTPOLL) += haltpoll.o
obj-$(CONFIG_CPU_IDLE_GOV_QCOM_LPM) += qcom_lpm.o
qcom_lpm-y := qcom-lpm.o qcom-cluster-lpm.o qcom-lpm-sysfs.o
CFLAGS_qcom-lpm.o := -I$(src)
CFLAGS_qcom-cluster-lpm.o := -I$(src)
