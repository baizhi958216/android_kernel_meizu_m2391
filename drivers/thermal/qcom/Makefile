# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_QCOM_TSENS)	+= qcom_tsens.o

qcom_tsens-y			+= tsens.o tsens-v2.o tsens-v1.o tsens-v0_1.o \
				   tsens-8960.o
obj-$(CONFIG_QCOM_SPMI_ADC_TM5_GEN3) += qcom-spmi-adc-tm5-gen3.o
obj-$(CONFIG_QCOM_SPMI_ADC_TM5)	+= qcom-spmi-adc-tm5.o
obj-$(CONFIG_QCOM_SPMI_TEMP_ALARM)	+= qcom-spmi-temp-alarm.o
obj-$(CONFIG_QCOM_LMH)		+= lmh.o
include $(srctree)/drivers/thermal/qcom/Makefile.m2391-sensors
obj-$(CONFIG_MEIZU_THERMAL_BOOST) += mz_thermal_boost.o
obj-$(CONFIG_QTI_CPUFREQ_CDEV) += qti_cpufreq_cdev.o
obj-$(CONFIG_QTI_CPU_VOLTAGE_COOLING_DEVICE) += cpu_voltage_cooling.o
