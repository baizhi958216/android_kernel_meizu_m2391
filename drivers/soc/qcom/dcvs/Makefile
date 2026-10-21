# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_QCOM_DCVS_FP) += dcvs_fp.o
obj-$(CONFIG_QCOM_DCVS) += qcom-dcvs.o
qcom-dcvs-y := dcvs.o dcvs_icc.o dcvs_epss.o trace-dcvs.o
CFLAGS_trace-dcvs.o := -I$(src)
