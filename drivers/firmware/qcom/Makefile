# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-$(CONFIG_QCOM_SCM)		+= qcom-scm.o
qcom-scm-objs += qcom_scm.o qcom_scm-smc.o qcom_scm-legacy.o
# The SHM bridge allocator calls SCM to register its own shared buffers.
ifeq ($(CONFIG_QCOM_SCM):$(CONFIG_QCOM_TZMEM_MODE_SHMBRIDGE),m:y)
qcom-scm-objs += qcom_tzmem.o
else
obj-$(CONFIG_QCOM_TZMEM)	+= qcom_tzmem.o
endif
obj-$(CONFIG_QCOM_QSEECOM)	+= qcom_qseecom.o
obj-$(CONFIG_QCOM_QSEECOM_UEFISECAPP) += qcom_qseecom_uefisecapp.o
