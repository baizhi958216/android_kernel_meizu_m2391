# SPDX-License-Identifier: Apache-2.0
# Source replacements and external build groups for m2391.
M2391_SOURCE_MODULES_OUT = $(M2391_KERNEL_BUILD_OUT)/m2391-modules

# Built-in replacements exclude legacy blobs but never produce fictitious .ko files.
M2391_SOURCE_BUILTIN_REPLACEMENT_NAMES := mz_vip_task.ko
M2391_SOURCE_BUILTIN_ABI_OBJECTS := kernel/sched/mz_vip_task.lto.o

M2391_SOURCE_MODULE_PATHS := \
    drivers/char/frpc-adsprpc.ko \
    drivers/scsi/ufs/ufs_qcom.ko \
    drivers/scsi/ufs/ufshcd-crypto-qti.ko \
    kernel/sched/walt/sched-walt.ko \
    drivers/pci/controller/pci-msm-drv.ko \
    drivers/platform/msm/ep_pcie/ep_pcie_drv.ko \
    drivers/virt/gunyah/gunyah.ko \
    drivers/mmc/host/sdhci-msm.ko \
    drivers/soc/qcom/panel_event_notifier.ko \
    drivers/usb/dwc3/dwc3-msm.ko \
    drivers/soc/qcom/mem_buf/mem_buf.ko \
    drivers/soc/qcom/mem_buf/mem_buf_dev.ko \
    drivers/soc/qcom/mem_buf/mem_buf_msgq.ko \
    drivers/iommu/qcom_iommu_util.ko \
    drivers/soc/qcom/msm_performance.ko \
    drivers/power/supply/qti_battery_charger.ko \
    drivers/interconnect/qcom/icc-debug.ko \
    drivers/hwmon/qti_amoled_ecm.ko \
    drivers/soc/qcom/pdr_interface.ko \
    arch/arm64/gunyah/gh_arm_drv.ko \
    drivers/base/regmap/qti-regmap-debugfs.ko \
    drivers/base/regmap/regmap-spmi.ko \
    drivers/bus/mhi/controllers/mhi_cntrl_qcom.ko \
    drivers/bus/mhi/devices/mhi_dev_dtr.ko \
    drivers/bus/mhi/devices/mhi_dev_netdev.ko \
    drivers/bus/mhi/devices/mhi_dev_satellite.ko \
    drivers/bus/mhi/devices/mhi_dev_uci.ko \
    drivers/bus/mhi/host/mhi.ko \
    drivers/char/rdbg.ko \
    drivers/clk/qcom/clk-rpmh.ko \
    drivers/clk/qcom/gdsc-regulator.ko \
    drivers/cpufreq/qcom-cpufreq-hw-debug.ko \
    drivers/cpufreq/qcom-cpufreq-hw.ko \
    drivers/cpuidle/governors/qcom_lpm.ko \
    drivers/dma-buf/heaps/qcom_dma_heaps.ko \
    drivers/dma/qcom/bam_dma.ko \
    drivers/dma/qcom/msm_gpi.ko \
    drivers/edac/qcom_edac.ko \
    drivers/firmware/arm_scmi/c1dcvs_vendor.ko \
    drivers/firmware/arm_scmi/cpufreq_stats_vendor.ko \
    drivers/firmware/arm_scmi/memlat_vendor.ko \
    drivers/firmware/arm_scmi/plh_vendor.ko \
    drivers/firmware/arm_scmi/pmu_vendor.ko \
    drivers/firmware/qcom-scm.ko \
    drivers/gpu/drm/bridge/lt9611uxc.ko \
    drivers/hwmon/hwmon.ko \
    drivers/hwspinlock/qcom_hwspinlock.ko \
    drivers/hwtracing/coresight/coresight-csr.ko \
    drivers/hwtracing/coresight/coresight-cti.ko \
    drivers/hwtracing/coresight/coresight-dummy.ko \
    drivers/hwtracing/coresight/coresight-funnel.ko \
    drivers/hwtracing/coresight/coresight-hwevent.ko \
    drivers/hwtracing/coresight/coresight-remote-etm.ko \
    drivers/hwtracing/coresight/coresight-replicator.ko \
    drivers/hwtracing/coresight/coresight-stm.ko \
    drivers/hwtracing/coresight/coresight-tgu.ko \
    drivers/hwtracing/coresight/coresight-tmc.ko \
    drivers/hwtracing/coresight/coresight-tpda.ko \
    drivers/hwtracing/coresight/coresight-tpdm.ko \
    drivers/hwtracing/coresight/coresight.ko \
    drivers/hwtracing/stm/stm_console.ko \
    drivers/hwtracing/stm/stm_core.ko \
    drivers/hwtracing/stm/stm_ftrace.ko \
    drivers/hwtracing/stm/stm_p_basic.ko \
    drivers/hwtracing/stm/stm_p_ost.ko \
    drivers/i2c/busses/i2c-msm-geni.ko \
    drivers/i3c/master/i3c-master-msm-geni.ko \
    drivers/iio/adc/qcom-spmi-adc5-gen3.ko \
    drivers/iio/adc/qcom-vadc-common.ko \
    drivers/iio/adc/qti-glink-adc.ko \
    drivers/iio/buffer/industrialio-buffer-cb.ko \
    drivers/input/misc/pm8941-pwrkey.ko \
    drivers/input/misc/qcom-hv-haptics.ko \
    drivers/interconnect/icc-test.ko \
    drivers/iommu/arm/arm-smmu/arm_smmu.ko \
    drivers/iommu/iommu-logger.ko \
    drivers/iommu/msm_dma_iommu_mapping.ko \
    drivers/iommu/qcom_iommu_debug.ko \
    drivers/irqchip/msm_show_resume_irq.ko \
    drivers/irqchip/qcom-pdc.ko \
    drivers/leds/leds-qpnp-vibrator-ldo.ko \
    drivers/leds/leds-qti-flash.ko \
    drivers/leds/leds-qti-tri-led.ko \
    drivers/mailbox/msm_qmp.ko \
    drivers/media/platform/msm/synx/qcom_ipc_lite.ko \
    drivers/media/platform/msm/synx/synx-driver.ko \
    drivers/mfd/qcom-i2c-pmic.ko \
    drivers/mfd/qcom-spmi-pmic.ko \
    drivers/misc/qseecom_proxy.ko \
    drivers/mmc/host/cqhci.ko \
    drivers/nvme/host/nvme-core.ko \
    drivers/nvme/host/nvme.ko \
    drivers/nvmem/nvmem_qcom-spmi-sdam.ko \
    drivers/nvmem/nvmem_qfprom.ko \
    drivers/perf/qcom_llcc_pmu.ko \
    drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-crow.ko \
    drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-kalama.ko \
    drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-khaje.ko \
    drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-kona.ko \
    drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-lahaina.ko \
    drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-waipio.ko \
    drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4.ko \
    drivers/phy/qualcomm/phy-qcom-ufs-qrbtc-sdm845.ko \
    drivers/phy/qualcomm/phy-qcom-ufs.ko \
    drivers/pinctrl/qcom/pinctrl-crow.ko \
    drivers/pinctrl/qcom/pinctrl-kalama.ko \
    drivers/pinctrl/qcom/pinctrl-msm.ko \
    drivers/pinctrl/qcom/pinctrl-spmi-gpio.ko \
    drivers/pinctrl/qcom/pinctrl-spmi-mpp.ko \
    drivers/platform/msm/ipa_fmwk/ipa_fmwk.ko \
    drivers/platform/msm/mhi_dev/mhi_dev_drv.ko \
    drivers/platform/msm/mhi_dev/mhi_dev_net.ko \
    drivers/power/reset/qcom-dload-mode.ko \
    drivers/power/reset/qcom-pon.ko \
    drivers/power/reset/qcom-reboot-reason.ko \
    drivers/power/reset/reboot-mode.ko \
    drivers/pwm/pwm-qti-lpg.ko \
    drivers/regulator/debug-regulator.ko \
    drivers/regulator/proxy-consumer.ko \
    drivers/regulator/qcom_pm8008-regulator.ko \
    drivers/regulator/qpnp-amoled-regulator.ko \
    drivers/regulator/qti-fixed-regulator.ko \
    drivers/regulator/qti-ocp-notifier.ko \
    drivers/regulator/rpmh-regulator.ko \
    drivers/regulator/stub-regulator.ko \
    drivers/tty/serial/msm_geni_serial.ko \
    drivers/remoteproc/qcom_esoc.ko \
    drivers/remoteproc/qcom_pil_info.ko \
    drivers/remoteproc/qcom_q6v5.ko \
    drivers/remoteproc/qcom_q6v5_pas.ko \
    drivers/remoteproc/qcom_spss.ko \
    drivers/remoteproc/qcom_sysmon.ko \
    drivers/remoteproc/rproc_qcom_common.ko \
    drivers/rpmsg/qcom_glink.ko \
    drivers/rpmsg/qcom_glink_smem.ko \
    drivers/rpmsg/qcom_glink_spss.ko \
    drivers/rpmsg/qcom_smd.ko \
    drivers/rtc/rtc-pm8xxx.ko \
    drivers/scsi/sg.ko \
    drivers/slimbus/slim-qcom-ngd-ctrl.ko \
    drivers/slimbus/slimbus.ko \
    drivers/soc/qcom/adsp_sleepmon.ko \
    drivers/soc/qcom/altmode-glink.ko \
    drivers/soc/qcom/boot_stats.ko \
    drivers/soc/qcom/cdsprm.ko \
    drivers/soc/qcom/charger-ulog-glink.ko \
    drivers/soc/qcom/cmd-db.ko \
    drivers/soc/qcom/core_hang_detect.ko \
    drivers/soc/qcom/crypto-qti-common.ko \
    drivers/soc/qcom/crypto-qti-hwkm.ko \
    drivers/soc/qcom/dcc_v2.ko \
    drivers/soc/qcom/dcvs/bwmon.ko \
    drivers/soc/qcom/dcvs/c1dcvs_scmi.ko \
    drivers/soc/qcom/dcvs/cpufreq_stats_scmi.ko \
    drivers/soc/qcom/dcvs/dcvs_fp.ko \
    drivers/soc/qcom/dcvs/memlat.ko \
    drivers/soc/qcom/dcvs/memlat_scmi.ko \
    drivers/soc/qcom/dcvs/pmu_scmi.ko \
    drivers/soc/qcom/dcvs/qcom-dcvs.ko \
    drivers/soc/qcom/dcvs/qcom-pmu-lib.ko \
    drivers/soc/qcom/debug_symbol.ko \
    drivers/soc/qcom/dmesg_dumper.ko \
    drivers/soc/qcom/eud.ko \
    drivers/soc/qcom/fsa4480-i2c.ko \
    drivers/soc/qcom/gh_tlmm_vm_mem_access.ko \
    drivers/soc/qcom/glink_pkt.ko \
    drivers/soc/qcom/glink_probe.ko \
    drivers/soc/qcom/hung_task_enh.ko \
    drivers/soc/qcom/hwkm.ko \
    drivers/soc/qcom/llcc-qcom.ko \
    drivers/soc/qcom/llcc_perfmon.ko \
    drivers/soc/qcom/mdt_loader.ko \
    drivers/soc/qcom/mem-hooks.ko \
    drivers/soc/qcom/mem-offline.ko \
    drivers/soc/qcom/memory_dump_v2.ko \
    drivers/soc/qcom/memshare/heap_mem_ext_v01.ko \
    drivers/soc/qcom/memshare/msm_memshare.ko \
    drivers/soc/qcom/microdump_collector.ko \
    drivers/soc/qcom/minidump.ko \
    drivers/soc/qcom/mz_info.ko \
    drivers/soc/qcom/msm_show_epoch.ko \
    drivers/soc/qcom/mz_enhance_qcom_wdog.ko \
    drivers/soc/qcom/plh_scmi.ko \
    drivers/soc/qcom/pmic-glink-debug.ko \
    drivers/soc/qcom/pmic-pon-log.ko \
    drivers/soc/qcom/pmic_glink.ko \
    drivers/soc/qcom/qcom_cpu_vendor_hooks.ko \
    drivers/soc/qcom/qcom_cpuss_sleep_stats.ko \
    drivers/soc/qcom/qcom_ipcc.ko \
    drivers/soc/qcom/qcom_logbuf_vh.ko \
    drivers/soc/qcom/qcom_ramdump.ko \
    drivers/soc/qcom/qcom_rimps.ko \
    drivers/soc/qcom/qcom_rpmh.ko \
    drivers/soc/qcom/qcom_va_minidump.ko \
    drivers/soc/qcom/qcom_wdt_core.ko \
    drivers/soc/qcom/qdsp6v2/cdsp-loader.ko \
    drivers/soc/qcom/qdss_bridge.ko \
    drivers/soc/qcom/qfprom-sys.ko \
    drivers/soc/qcom/qmi_helpers.ko \
    drivers/soc/qcom/qsee_ipc_irq_bridge.ko \
    drivers/soc/qcom/qti_battery_debug.ko \
    drivers/soc/qcom/rimps_log.ko \
    drivers/soc/qcom/rq_stats.ko \
    drivers/soc/qcom/secure_buffer.ko \
    drivers/soc/qcom/smem.ko \
    drivers/soc/qcom/smp2p.ko \
    drivers/soc/qcom/smp2p_sleepstate.ko \
    drivers/soc/qcom/soc_sleep_stats.ko \
    drivers/soc/qcom/socinfo.ko \
    drivers/soc/qcom/spcom.ko \
    drivers/soc/qcom/sps/sps_drv.ko \
    drivers/soc/qcom/spss_utils.ko \
    drivers/soc/qcom/subsystem_sleep_stats.ko \
    drivers/soc/qcom/sys_pm_vx.ko \
    drivers/soc/qcom/sysmon_subsystem_stats.ko \
    drivers/soc/qcom/tmecom/tmecom-intf.ko \
    drivers/soc/qcom/usb_bam.ko \
    drivers/soc/qcom/wcd_usbss_i2c.ko \
    drivers/spi/spi-msm-geni.ko \
    drivers/spmi/spmi-pmic-arb-debug.ko \
    drivers/spmi/spmi-pmic-arb.ko \
    drivers/thermal/qcom/bcl_pmic5.ko \
    drivers/thermal/qcom/bcl_soc.ko \
    drivers/thermal/qcom/cpu_hotplug.ko \
    drivers/thermal/qcom/cpu_voltage_cooling.ko \
    drivers/thermal/qcom/ddr_cdev.ko \
    drivers/thermal/qcom/max31760_fan.ko \
    drivers/thermal/qcom/msm_lmh_dcvs.ko \
    drivers/thermal/qcom/policy_engine.ko \
    drivers/thermal/qcom/qcom-spmi-temp-alarm.ko \
    drivers/thermal/qcom/qcom_tsens.ko \
    drivers/thermal/qcom/qti_devfreq_cdev.ko \
    drivers/thermal/qcom/mz_thermal_boost.ko \
    drivers/thermal/qcom/qti_cpufreq_cdev.ko \
    drivers/thermal/qcom/qti_qmi_cdev.ko \
    drivers/thermal/qcom/qti_qmi_sensor_v2.ko \
    drivers/thermal/qcom/qti_userspace_cdev.ko \
    drivers/thermal/qcom/sdpm_clk.ko \
    drivers/thermal/qcom/thermal_minidump.ko \
    drivers/thermal/qcom/thermal_pause.ko \
    drivers/tty/hvc/hvc_gunyah.ko \
    drivers/uio/msm_sharedmem/msm_sharedmem.ko \
    drivers/usb/gadget/function/f_fs_ipc_log.ko \
    drivers/usb/gadget/function/usb_f_ccid.ko \
    drivers/usb/gadget/function/usb_f_cdev.ko \
    drivers/usb/gadget/function/usb_f_diag.ko \
    drivers/usb/gadget/function/usb_f_gsi.ko \
    drivers/usb/gadget/function/usb_f_qdss.ko \
    drivers/usb/misc/ehset.ko \
    drivers/usb/mon/usbmon.ko \
    drivers/usb/phy/phy-generic.ko \
    drivers/usb/phy/phy-msm-m31-eusb2.ko \
    drivers/usb/phy/phy-msm-snps-eusb2.ko \
    drivers/usb/phy/phy-msm-ssusb-qmp.ko \
    drivers/usb/phy/phy-qcom-emu.ko \
    drivers/usb/redriver/nb7vpq904m.ko \
    drivers/usb/redriver/redriver.ko \
    drivers/usb/repeater/repeater-i2c-eusb2.ko \
    drivers/usb/repeater/repeater-qti-pmic-eusb2.ko \
    drivers/usb/repeater/repeater.ko \
    drivers/usb/typec/ucsi/ucsi_glink.ko \
    drivers/virt/gunyah/gh_ctrl.ko \
    drivers/virt/gunyah/gh_dbl.ko \
    drivers/virt/gunyah/gh_irq_lend.ko \
    drivers/virt/gunyah/gh_mem_notifier.ko \
    drivers/virt/gunyah/gh_msgq.ko \
    drivers/virt/gunyah/gh_rm_drv.ko \
    drivers/virt/gunyah/gh_virt_wdt.ko \
    kernel/msm_sysstats.ko \
    kernel/trace/qcom_ipc_logging.ko \
    drivers/clk/qcom/clk-qcom.ko \
    drivers/clk/qcom/clk-dummy.ko \
    drivers/clk/qcom/debugcc-crow.ko \
    drivers/clk/qcom/debugcc-kalama.ko \
    drivers/clk/qcom/camcc-crow.ko \
    drivers/clk/qcom/camcc-kalama.ko \
    drivers/clk/qcom/dispcc-crow.ko \
    drivers/clk/qcom/dispcc-kalama.ko \
    drivers/clk/qcom/gcc-crow.ko \
    drivers/clk/qcom/gcc-kalama.ko \
    drivers/clk/qcom/gpucc-crow.ko \
    drivers/clk/qcom/gpucc-kalama.ko \
    drivers/clk/qcom/tcsrcc-kalama.ko \
    drivers/clk/qcom/videocc-crow.ko \
    drivers/clk/qcom/videocc-kalama.ko \
    drivers/soc/qcom/qbt_handler.ko \
    drivers/soc/qcom/qcom_aoss.ko \
    drivers/interconnect/qcom/qnoc-qos.ko \
    drivers/interconnect/qcom/icc-rpmh.ko \
    drivers/interconnect/qcom/icc-bcm-voter.ko \
    drivers/interconnect/qcom/qnoc-crow.ko \
    drivers/interconnect/qcom/qnoc-kalama.ko \
    lib/kunit/kunit.ko \
    net/mac80211/mac80211.ko \
    net/qrtr/qrtr-gunyah.ko \
    net/qrtr/qrtr-mhi.ko \
    net/qrtr/qrtr-smd.ko \
    net/qrtr/qrtr.ko \
    net/wireless/cfg80211.ko \
    sound/soc/codecs/snd-soc-hdmi-codec.ko \
    sound/usb/snd-usb-audio-qmi.ko

# External groups are built in dependency order. Entries use installed-name:source-path, with paths relative to
# kernel/meizu/m2391-modules/qcom/opensource. This preserves the Wi-Fi module alias.
M2391_EXTERNAL_GROUPS := \
    ../../meizu/smart-zsmalloc \
    ../../meizu/smartswap-zram \
    ../../meizu/aks-adc \
    ../../meizu/aks-joystick \
    ../../meizu/audio-dummy \
    dataipa/drivers/platform/msm \
    datarmnet-ext/sch \
    datarmnet/core \
    ../../meizu/boot-mode \
    ../../meizu/system \
    ../../meizu/debug-vendor-hooks \
    ../../meizu/reclaim-opt \
    ../../meizu/kshrink-slabd \
    ../../meizu/freq-qos \
    ../../meizu/multiple-rail-governor \
    ../../meizu/probe \
    ../../meizu/compaction-opt \
    ../../meizu/hang-dump \
    ../../meizu/audit \
    ../../meizu/pwm-ir \
    ../../meizu/storage \
    ../../meizu/cirrus-calibration \
    ../../meizu/pressure-m68 \
    ../../meizu/pressure-f71 \
    ../../meizu/v4l2loopback \
    ../../meizu/haptic-hv \
    ../../meizu/reset-info \
    ../../meizu/bootloader-lock \
    ../../meizu/key-value \
    ../../meizu/rpmb-control \
    ../../meizu/power-notify \
    ../../meizu/power-logger \
    ../../meizu/bootloader-log \
    ../../meizu/power-interface \
    ../../meizu/power-manager \
    ../../meizu/power-thermal \
    ../../meizu/power-battery-logger \
    ../../meizu/thermal-log \
    ../../meizu/tof-ai11ba \
    ../../meizu/tof-vl53l1 \
    ../../st/opensource/st54se-gpio \
    securemsm-kernel \
    ../../st/opensource/nfc \
    ../../nxp/opensource/driver \
    ../../nxp/opensource/uwb \
    touch-drivers/mz_gesture \
    touch-drivers \
    mmrm-driver \
    mm-drivers/sync_fence \
    graphics-kernel \
    camera-kernel \
    eva-kernel \
    video-driver \
    mm-drivers/hw_fence \
    mm-drivers/msm_ext_display \
    display-drivers \
    audio-kernel \
    ../../meizu/cs35l45 \
    datarmnet-ext/aps \
    datarmnet-ext/offload \
    datarmnet-ext/perf \
    datarmnet-ext/perf_tether \
    datarmnet-ext/shs \
    datarmnet-ext/wlan \
    wlan/platform \
    bt-kernel \
    wlan/qcacld-3.0

# Recovered OEM drivers live outside qcom/opensource in the same checkout.
M2391_EXTERNAL_MODULES.../../meizu/smart-zsmalloc := \
    smart_zsmalloc.ko:../../meizu/smart-zsmalloc/smart_zsmalloc.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/smart-zsmalloc := smart_zsmalloc.ko

M2391_EXTERNAL_MODULES.../../meizu/smartswap-zram := \
    smartswap_zram.ko:../../meizu/smartswap-zram/smartswap_zram.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/smartswap-zram := smartswap_zram.ko

M2391_EXTERNAL_MODULES.../../meizu/aks-adc := \
    aks-ads1015.ko:../../meizu/aks-adc/aks-ads1015.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/aks-adc := aks-ads1015.ko

M2391_EXTERNAL_MODULES.../../meizu/aks-joystick := \
    aks-adc-joystick.ko:../../meizu/aks-joystick/aks-adc-joystick.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/aks-joystick := aks-adc-joystick.ko

M2391_EXTERNAL_MODULES.../../meizu/audio-dummy := \
    dummy_dlkm.ko:../../meizu/audio-dummy/dummy_dlkm.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/audio-dummy := dummy_dlkm.ko

M2391_EXTERNAL_MODULES.../../meizu/boot-mode := \
    boot_mode.ko:../../meizu/boot-mode/boot_mode.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/boot-mode := boot_mode.ko

M2391_EXTERNAL_MODULES.../../meizu/system := \
    meizu-sys.ko:../../meizu/system/meizu-sys.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/system := meizu-sys.ko

M2391_EXTERNAL_MODULES.../../meizu/pressure-m68 := \
    cs_press_m68.ko:../../meizu/pressure-m68/cs_press_m68.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/pressure-m68 := cs_press_m68.ko

M2391_EXTERNAL_MODULES.../../meizu/pressure-f71 := \
    cs_press_f71.ko:../../meizu/pressure-f71/cs_press_f71.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/pressure-f71 := cs_press_f71.ko

M2391_EXTERNAL_MODULES.../../meizu/v4l2loopback := \
    v4l2loopback.ko:../../meizu/v4l2loopback/v4l2loopback.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/v4l2loopback := v4l2loopback.ko

M2391_EXTERNAL_MODULES.../../meizu/kshrink-slabd := \
    mz_kshrink_slabd.ko:../../meizu/kshrink-slabd/mz_kshrink_slabd.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/kshrink-slabd := mz_kshrink_slabd.ko

M2391_EXTERNAL_MODULES.../../meizu/freq-qos := \
    mz_freq_qos.ko:../../meizu/freq-qos/mz_freq_qos.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/freq-qos := mz_freq_qos.ko

M2391_EXTERNAL_MODULES.../../meizu/multiple-rail-governor := \
    mz_mrg.ko:../../meizu/multiple-rail-governor/mz_mrg.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/multiple-rail-governor := mz_mrg.ko

M2391_EXTERNAL_MODULES.../../meizu/probe := \
    mzprobe.ko:../../meizu/probe/mzprobe.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/probe := mzprobe.ko

M2391_EXTERNAL_MODULES.../../meizu/cirrus-calibration := \
    cirrus_cali_dlkm.ko:../../meizu/cirrus-calibration/cirrus_cali_dlkm.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/cirrus-calibration := cirrus_cali_dlkm.ko

M2391_EXTERNAL_MODULES.../../meizu/cs35l45 := \
    cs35l45_dlkm.ko:../../meizu/cs35l45/cs35l45_dlkm.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/cs35l45 := cs35l45_dlkm.ko

M2391_EXTERNAL_MODULES.../../meizu/reclaim-opt := \
    mz_reclaim_opt.ko:../../meizu/reclaim-opt/mz_reclaim_opt.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/reclaim-opt := mz_reclaim_opt.ko

M2391_EXTERNAL_MODULES.../../meizu/compaction-opt := \
    mz_compaction_opt.ko:../../meizu/compaction-opt/mz_compaction_opt.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/compaction-opt := mz_compaction_opt.ko

M2391_EXTERNAL_MODULES.../../meizu/hang-dump := \
    mz_hang_dump.ko:../../meizu/hang-dump/mz_hang_dump.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/hang-dump := mz_hang_dump.ko

M2391_EXTERNAL_MODULES.../../meizu/audit := \
    maudit.ko:../../meizu/audit/maudit.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/audit := maudit.ko

M2391_EXTERNAL_MODULES.../../meizu/pwm-ir := \
    pwm_ir.ko:../../meizu/pwm-ir/pwm_ir.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/pwm-ir := pwm_ir.ko

M2391_EXTERNAL_MODULES.../../meizu/debug-vendor-hooks := \
    mz_debug_vendor_hooks.ko:../../meizu/debug-vendor-hooks/mz_debug_vendor_hooks.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/debug-vendor-hooks := mz_debug_vendor_hooks.ko

M2391_EXTERNAL_MODULES.../../meizu/storage := \
    meizu_private.ko:../../meizu/storage/meizu_private.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/storage := meizu_private.ko

M2391_EXTERNAL_MODULES.../../meizu/haptic-hv := \
    haptic_hv.ko:../../meizu/haptic-hv/haptic_hv.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/haptic-hv := haptic_hv.ko

M2391_EXTERNAL_MODULES.../../meizu/reset-info := \
    mz_rstinfo.ko:../../meizu/reset-info/mz_rstinfo.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/reset-info := mz_rstinfo.ko

M2391_EXTERNAL_MODULES.../../meizu/bootloader-lock := \
    mz_bl_lock.ko:../../meizu/bootloader-lock/mz_bl_lock.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/bootloader-lock := mz_bl_lock.ko

M2391_EXTERNAL_MODULES.../../meizu/power-notify := \
    xcharge_notify_subsys.ko:../../meizu/power-notify/xcharge_notify_subsys.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/power-notify := xcharge_notify_subsys.ko

M2391_EXTERNAL_MODULES.../../meizu/power-manager := \
    xcharge_chg_manager.ko:../../meizu/power-manager/xcharge_chg_manager.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/power-manager := xcharge_chg_manager.ko

M2391_EXTERNAL_MODULES.../../meizu/power-thermal := \
    xcharge_thermal.ko:../../meizu/power-thermal/xcharge_thermal.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/power-thermal := xcharge_thermal.ko

M2391_EXTERNAL_MODULES.../../meizu/power-battery-logger := \
    xcharge_mbattery_logger.ko:../../meizu/power-battery-logger/xcharge_mbattery_logger.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/power-battery-logger := xcharge_mbattery_logger.ko

M2391_EXTERNAL_MODULES.../../meizu/power-logger := \
    mpower_logger.ko:../../meizu/power-logger/mpower_logger.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/power-logger := mpower_logger.ko

M2391_EXTERNAL_MODULES.../../meizu/bootloader-log := \
    bootloader_log.ko:../../meizu/bootloader-log/bootloader_log.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/bootloader-log := bootloader_log.ko

M2391_EXTERNAL_MODULES.../../meizu/key-value := \
    meizu_key_value.ko:../../meizu/key-value/meizu_key_value.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/key-value := meizu_key_value.ko

M2391_EXTERNAL_MODULES.../../meizu/rpmb-control := \
    mz_rpmb_ctl.ko:../../meizu/rpmb-control/mz_rpmb_ctl.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/rpmb-control := mz_rpmb_ctl.ko

M2391_EXTERNAL_MODULES.../../meizu/power-interface := \
    xcharge_chg_interface.ko:../../meizu/power-interface/xcharge_chg_interface.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/power-interface := xcharge_chg_interface.ko

M2391_EXTERNAL_MODULES.../../meizu/thermal-log := \
    mz_thermal_log.ko:../../meizu/thermal-log/mz_thermal_log.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/thermal-log := mz_thermal_log.ko

M2391_EXTERNAL_MODULES.../../meizu/tof-ai11ba := \
    ai11ba.ko:../../meizu/tof-ai11ba/ai11ba.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/tof-ai11ba := ai11ba.ko

M2391_EXTERNAL_MODULES.../../meizu/tof-vl53l1 := \
    vl53l1.ko:../../meizu/tof-vl53l1/vl53l1.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../meizu/tof-vl53l1 := vl53l1.ko

M2391_EXTERNAL_MODULES.../../st/opensource/st54se-gpio := \
    stm_st54se_gpio.ko:../../st/opensource/st54se-gpio/stm_st54se_gpio.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../st/opensource/st54se-gpio := stm_st54se_gpio.ko

M2391_EXTERNAL_MODULES.../../st/opensource/nfc := \
    stm_nfc_i2c.ko:../../st/opensource/nfc/stm_nfc_i2c.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../st/opensource/nfc := stm_nfc_i2c.ko

M2391_EXTERNAL_MODULES.../../nxp/opensource/driver := \
    nxp-nci.ko:../../nxp/opensource/driver/nxp-nci.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../nxp/opensource/driver := nxp-nci.ko

M2391_EXTERNAL_MODULES.../../nxp/opensource/uwb := \
    nxp-sr100.ko:../../nxp/opensource/uwb/nxp-sr100.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.../../nxp/opensource/uwb := nxp-sr100.ko

M2391_EXTERNAL_MODULES.touch-drivers/mz_gesture := \
    mz_gesture_ts.ko:touch-drivers/mz_gesture/mz_gesture_ts.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.touch-drivers/mz_gesture := mz_gesture_ts.ko

M2391_EXTERNAL_MODULES.touch-drivers := \
    qts.ko:touch-drivers/qts.ko \
    goodix_ts.ko:touch-drivers/goodix_ts.ko \
    nt36xxx-i2c.ko:touch-drivers/nt36xxx-i2c.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.touch-drivers := qts.ko goodix_ts.ko nt36xxx-i2c.ko

M2391_EXTERNAL_MODULES.graphics-kernel := \
    msm_kgsl.ko:graphics-kernel/msm_kgsl.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.graphics-kernel := msm_kgsl.ko

M2391_EXTERNAL_MODULES.video-driver := \
    msm_video.ko:video-driver/msm_video.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.video-driver := msm_video.ko

M2391_EXTERNAL_MODULES.eva-kernel := \
    msm-eva.ko:eva-kernel/msm/msm-eva.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.eva-kernel := msm-eva.ko

M2391_EXTERNAL_MODULES.camera-kernel := \
    camera.ko:camera-kernel/camera.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.camera-kernel := camera.ko

M2391_EXTERNAL_MODULES.display-drivers := \
    msm_drm.ko:display-drivers/msm/msm_drm.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.display-drivers := msm_drm.ko

M2391_EXTERNAL_MODULES.mmrm-driver := \
    msm-mmrm.ko:mmrm-driver/driver/msm-mmrm.ko \
    mmrm_test_module.ko:mmrm-driver/test/mmrm_test_module.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.mmrm-driver := msm-mmrm.ko mmrm_test_module.ko

M2391_EXTERNAL_MODULES.mm-drivers/sync_fence := \
    sync_fence.ko:mm-drivers/sync_fence/sync_fence.ko
M2391_EXTERNAL_BUILT_MODULE_NAMES.mm-drivers/sync_fence := sync_fence.ko

M2391_EXTERNAL_MODULES.dataipa/drivers/platform/msm := \
    gsim.ko:dataipa/drivers/platform/msm/gsi/gsim.ko \
    ipa_clientsm.ko:dataipa/drivers/platform/msm/ipa/ipa_clients/ipa_clientsm.ko \
    ipam.ko:dataipa/drivers/platform/msm/ipa/ipam.ko \
    ipanetm.ko:dataipa/drivers/platform/msm/ipa/ipanetm.ko \
    rndisipam.ko:dataipa/drivers/platform/msm/ipa/ipa_clients/rndisipam.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.dataipa/drivers/platform/msm := \
    gsim.ko \
    ipa_clientsm.ko \
    ipam.ko \
    ipanetm.ko \
    rndisipam.ko

M2391_EXTERNAL_MODULES.datarmnet-ext/sch := \
    rmnet_sch.ko:datarmnet-ext/sch/rmnet_sch.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.datarmnet-ext/sch := \
    rmnet_sch.ko

M2391_EXTERNAL_MODULES.datarmnet/core := \
    rmnet_core.ko:datarmnet/core/rmnet_core.ko \
    rmnet_ctl.ko:datarmnet/core/rmnet_ctl.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.datarmnet/core := \
    rmnet_core.ko \
    rmnet_ctl.ko

M2391_EXTERNAL_MODULES.mm-drivers/hw_fence := \
    msm_hw_fence.ko:mm-drivers/hw_fence/msm_hw_fence.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.mm-drivers/hw_fence := \
    msm_hw_fence.ko

M2391_EXTERNAL_MODULES.mm-drivers/msm_ext_display := \
    msm_ext_display.ko:mm-drivers/msm_ext_display/msm_ext_display.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.mm-drivers/msm_ext_display := \
    msm_ext_display.ko

M2391_EXTERNAL_MODULES.securemsm-kernel := \
    hdcp_qseecom_dlkm.ko:securemsm-kernel/hdcp_qseecom_dlkm.ko \
    qce50_dlkm.ko:securemsm-kernel/qce50_dlkm.ko \
    qcedev-mod_dlkm.ko:securemsm-kernel/qcedev-mod_dlkm.ko \
    qcrypto-msm_dlkm.ko:securemsm-kernel/qcrypto-msm_dlkm.ko \
    qrng_dlkm.ko:securemsm-kernel/qrng_dlkm.ko \
    smcinvoke_dlkm.ko:securemsm-kernel/smcinvoke_dlkm.ko \
    tz_log_dlkm.ko:securemsm-kernel/tz_log_dlkm.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.securemsm-kernel := \
    hdcp_qseecom_dlkm.ko \
    qce50_dlkm.ko \
    qcedev-mod_dlkm.ko \
    qcrypto-msm_dlkm.ko \
    qrng_dlkm.ko \
    smcinvoke_dlkm.ko \
    tz_log_dlkm.ko

M2391_EXTERNAL_MODULES.audio-kernel := \
    machine_dlkm.ko:audio-kernel/asoc/machine_dlkm.ko \
    adsp_loader_dlkm.ko:audio-kernel/dsp/adsp_loader_dlkm.ko \
    audio_pkt_dlkm.ko:audio-kernel/ipc/audio_pkt_dlkm.ko \
    audio_prm_dlkm.ko:audio-kernel/dsp/audio_prm_dlkm.ko \
    audpkt_ion_dlkm.ko:audio-kernel/dsp/audpkt_ion_dlkm.ko \
    gpr_dlkm.ko:audio-kernel/ipc/gpr_dlkm.ko \
    hdmi_dlkm.ko:audio-kernel/asoc/codecs/hdmi_dlkm.ko \
    lpass_cdc_dlkm.ko:audio-kernel/asoc/codecs/lpass-cdc/lpass_cdc_dlkm.ko \
    lpass_cdc_rx_macro_dlkm.ko:audio-kernel/asoc/codecs/lpass-cdc/lpass_cdc_rx_macro_dlkm.ko \
    lpass_cdc_tx_macro_dlkm.ko:audio-kernel/asoc/codecs/lpass-cdc/lpass_cdc_tx_macro_dlkm.ko \
    lpass_cdc_va_macro_dlkm.ko:audio-kernel/asoc/codecs/lpass-cdc/lpass_cdc_va_macro_dlkm.ko \
    lpass_cdc_wsa2_macro_dlkm.ko:audio-kernel/asoc/codecs/lpass-cdc/lpass_cdc_wsa2_macro_dlkm.ko \
    lpass_cdc_wsa_macro_dlkm.ko:audio-kernel/asoc/codecs/lpass-cdc/lpass_cdc_wsa_macro_dlkm.ko \
    mbhc_dlkm.ko:audio-kernel/asoc/codecs/mbhc_dlkm.ko \
    pinctrl_lpi_dlkm.ko:audio-kernel/soc/pinctrl_lpi_dlkm.ko \
    q6_dlkm.ko:audio-kernel/dsp/q6_dlkm.ko \
    q6_notifier_dlkm.ko:audio-kernel/dsp/q6_notifier_dlkm.ko \
    q6_pdr_dlkm.ko:audio-kernel/dsp/q6_pdr_dlkm.ko \
    snd_event_dlkm.ko:audio-kernel/soc/snd_event_dlkm.ko \
    spf_core_dlkm.ko:audio-kernel/dsp/spf_core_dlkm.ko \
    stub_dlkm.ko:audio-kernel/asoc/codecs/stub_dlkm.ko \
    swr_ctrl_dlkm.ko:audio-kernel/soc/swr_ctrl_dlkm.ko \
    swr_dlkm.ko:audio-kernel/soc/swr_dlkm.ko \
    swr_dmic_dlkm.ko:audio-kernel/asoc/codecs/swr_dmic_dlkm.ko \
    swr_haptics_dlkm.ko:audio-kernel/asoc/codecs/swr_haptics_dlkm.ko \
    wcd937x_dlkm.ko:audio-kernel/asoc/codecs/wcd937x/wcd937x_dlkm.ko \
    wcd937x_slave_dlkm.ko:audio-kernel/asoc/codecs/wcd937x/wcd937x_slave_dlkm.ko \
    wcd938x_dlkm.ko:audio-kernel/asoc/codecs/wcd938x/wcd938x_dlkm.ko \
    wcd938x_slave_dlkm.ko:audio-kernel/asoc/codecs/wcd938x/wcd938x_slave_dlkm.ko \
    wcd939x_dlkm.ko:audio-kernel/asoc/codecs/wcd939x/wcd939x_dlkm.ko \
    wcd939x_slave_dlkm.ko:audio-kernel/asoc/codecs/wcd939x/wcd939x_slave_dlkm.ko \
    wcd9xxx_dlkm.ko:audio-kernel/asoc/codecs/wcd9xxx_dlkm.ko \
    wcd_core_dlkm.ko:audio-kernel/asoc/codecs/wcd_core_dlkm.ko \
    wsa883x_dlkm.ko:audio-kernel/asoc/codecs/wsa883x/wsa883x_dlkm.ko \
    wsa884x_dlkm.ko:audio-kernel/asoc/codecs/wsa884x/wsa884x_dlkm.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.audio-kernel := \
    adsp_loader_dlkm.ko \
    audio_pkt_dlkm.ko \
    audio_prm_dlkm.ko \
    audpkt_ion_dlkm.ko \
    gpr_dlkm.ko \
    hdmi_dlkm.ko \
    lpass_cdc_dlkm.ko \
    lpass_cdc_rx_macro_dlkm.ko \
    lpass_cdc_tx_macro_dlkm.ko \
    lpass_cdc_va_macro_dlkm.ko \
    lpass_cdc_wsa2_macro_dlkm.ko \
    lpass_cdc_wsa_macro_dlkm.ko \
    machine_dlkm.ko \
    mbhc_dlkm.ko \
    pinctrl_lpi_dlkm.ko \
    q6_dlkm.ko \
    q6_notifier_dlkm.ko \
    q6_pdr_dlkm.ko \
    snd_event_dlkm.ko \
    spf_core_dlkm.ko \
    stub_dlkm.ko \
    swr_ctrl_dlkm.ko \
    swr_dlkm.ko \
    swr_dmic_dlkm.ko \
    swr_haptics_dlkm.ko \
    wcd937x_dlkm.ko \
    wcd937x_slave_dlkm.ko \
    wcd938x_dlkm.ko \
    wcd938x_slave_dlkm.ko \
    wcd939x_dlkm.ko \
    wcd939x_slave_dlkm.ko \
    wcd9xxx_dlkm.ko \
    wcd_core_dlkm.ko \
    wsa883x_dlkm.ko \
    wsa884x_dlkm.ko

M2391_EXTERNAL_MODULES.datarmnet-ext/aps := \
    rmnet_aps.ko:datarmnet-ext/aps/rmnet_aps.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.datarmnet-ext/aps := \
    rmnet_aps.ko

M2391_EXTERNAL_MODULES.datarmnet-ext/offload := \
    rmnet_offload.ko:datarmnet-ext/offload/rmnet_offload.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.datarmnet-ext/offload := \
    rmnet_offload.ko

M2391_EXTERNAL_MODULES.datarmnet-ext/perf := \
    rmnet_perf.ko:datarmnet-ext/perf/rmnet_perf.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.datarmnet-ext/perf := \
    rmnet_perf.ko

M2391_EXTERNAL_MODULES.datarmnet-ext/perf_tether := \
    rmnet_perf_tether.ko:datarmnet-ext/perf_tether/rmnet_perf_tether.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.datarmnet-ext/perf_tether := \
    rmnet_perf_tether.ko

M2391_EXTERNAL_MODULES.datarmnet-ext/shs := \
    rmnet_shs.ko:datarmnet-ext/shs/rmnet_shs.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.datarmnet-ext/shs := \
    rmnet_shs.ko

M2391_EXTERNAL_MODULES.datarmnet-ext/wlan := \
    rmnet_wlan.ko:datarmnet-ext/wlan/rmnet_wlan.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.datarmnet-ext/wlan := \
    rmnet_wlan.ko

M2391_EXTERNAL_MODULES.wlan/platform := \
    cnss2.ko:wlan/platform/cnss2/cnss2.ko \
    cnss_nl.ko:wlan/platform/cnss_genl/cnss_nl.ko \
    cnss_plat_ipc_qmi_svc.ko:wlan/platform/cnss_utils/cnss_plat_ipc_qmi_svc.ko \
    cnss_prealloc.ko:wlan/platform/cnss_prealloc/cnss_prealloc.ko \
    cnss_utils.ko:wlan/platform/cnss_utils/cnss_utils.ko \
    wlan_firmware_service.ko:wlan/platform/cnss_utils/wlan_firmware_service.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.wlan/platform := \
    cnss2.ko \
    cnss_nl.ko \
    cnss_plat_ipc_qmi_svc.ko \
    cnss_prealloc.ko \
    cnss_utils.ko \
    wlan_firmware_service.ko

M2391_EXTERNAL_MODULES.bt-kernel := \
    bt_fm_slim.ko:bt-kernel/slimbus/bt_fm_slim.ko \
    btpower.ko:bt-kernel/pwr/btpower.ko \
    radio-i2c-rtc6226-qca.ko:bt-kernel/rtc6226/radio-i2c-rtc6226-qca.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.bt-kernel := \
    bt_fm_slim.ko \
    btpower.ko \
    radio-i2c-rtc6226-qca.ko

M2391_EXTERNAL_MODULES.wlan/qcacld-3.0 := \
    qca_cld3_kiwi_v2.ko:wlan/qcacld-3.0/kiwi_v2.ko

M2391_EXTERNAL_BUILT_MODULE_NAMES.wlan/qcacld-3.0 := \
    kiwi_v2.ko

M2391_SOURCE_EXTERNAL_MODULE_NAMES := $(sort $(foreach group,$(M2391_EXTERNAL_GROUPS),$(foreach module,$(M2391_EXTERNAL_MODULES.$(group)),$(word 1,$(subst :, ,$(module))))))

M2391_SOURCE_VENDOR_MODULE_NAMES := \
    smartswap_zram.ko \
    machine_dlkm.ko \
    cs35l45_dlkm.ko \
    msm_video.ko \
    frpc-adsprpc.ko \
    msm-eva.ko \
    cirrus_cali_dlkm.ko \
    mzprobe.ko \
    smart_zsmalloc.ko \
    nt36xxx-i2c.ko \
    aks-ads1015.ko \
    aks-adc-joystick.ko \
    cs_press_m68.ko \
    cs_press_f71.ko \
    v4l2loopback.ko \
    dummy_dlkm.ko \
    pci-msm-drv.ko \
    ep_pcie_drv.ko \
    msm_geni_serial.ko \
    haptic_hv.ko \
    qcom_q6v5.ko \
    mz_rstinfo.ko \
    panel_event_notifier.ko \
    dwc3-msm.ko \
    mmrm_test_module.ko \
    pwm_ir.ko \
    maudit.ko \
    msm_performance.ko \
    qti_battery_charger.ko \
    mz_hang_dump.ko \
    qti_amoled_ecm.ko \
    pdr_interface.ko \
    mz_reclaim_opt.ko \
    mz_debug_vendor_hooks.ko \
    adsp_loader_dlkm.ko \
    adsp_sleepmon.ko \
    altmode-glink.ko \
    audio_pkt_dlkm.ko \
    audio_prm_dlkm.ko \
    audpkt_ion_dlkm.ko \
    bam_dma.ko \
    bcl_soc.ko \
    boot_mode.ko \
    boot_stats.ko \
    bt_fm_slim.ko \
    btpower.ko \
    cdsp-loader.ko \
    cdsprm.ko \
    cfg80211.ko \
    charger-ulog-glink.ko \
    cnss2.ko \
    cnss_nl.ko \
    cnss_plat_ipc_qmi_svc.ko \
    cnss_prealloc.ko \
    cnss_utils.ko \
    core_hang_detect.ko \
    coresight-csr.ko \
    coresight-cti.ko \
    coresight-dummy.ko \
    coresight-funnel.ko \
    coresight-hwevent.ko \
    coresight-remote-etm.ko \
    coresight-replicator.ko \
    coresight-stm.ko \
    coresight-tgu.ko \
    coresight-tmc.ko \
    coresight-tpda.ko \
    coresight-tpdm.ko \
    coresight.ko \
    cpu_voltage_cooling.ko \
    cpufreq_stats_scmi.ko \
    cpufreq_stats_vendor.ko \
    ddr_cdev.ko \
    dmesg_dumper.ko \
    ehset.ko \
    eud.ko \
    f_fs_ipc_log.ko \
    fsa4480-i2c.ko \
    gh_irq_lend.ko \
    gh_mem_notifier.ko \
    gh_tlmm_vm_mem_access.ko \
    glink_pkt.ko \
    glink_probe.ko \
    gpr_dlkm.ko \
    gsim.ko \
    hdcp_qseecom_dlkm.ko \
    hdmi_dlkm.ko \
    heap_mem_ext_v01.ko \
    hung_task_enh.ko \
    hvc_gunyah.ko \
    hwmon.ko \
    i2c-msm-geni.ko \
    i3c-master-msm-geni.ko \
    icc-test.ko \
    industrialio-buffer-cb.ko \
    ipa_clientsm.ko \
    ipa_fmwk.ko \
    ipam.ko \
    ipanetm.ko \
    kunit.ko \
    leds-qpnp-vibrator-ldo.ko \
    leds-qti-flash.ko \
    leds-qti-tri-led.ko \
    llcc_perfmon.ko \
    lpass_cdc_dlkm.ko \
    lpass_cdc_rx_macro_dlkm.ko \
    lpass_cdc_tx_macro_dlkm.ko \
    lpass_cdc_va_macro_dlkm.ko \
    lpass_cdc_wsa2_macro_dlkm.ko \
    lpass_cdc_wsa_macro_dlkm.ko \
    lt9611uxc.ko \
    mac80211.ko \
    max31760_fan.ko \
    mbhc_dlkm.ko \
    memlat.ko \
    memlat_scmi.ko \
    memlat_vendor.ko \
    meizu_private.ko \
    mz_bl_lock.ko \
    mhi.ko \
    mhi_cntrl_qcom.ko \
    mhi_dev_drv.ko \
    mhi_dev_dtr.ko \
    mhi_dev_net.ko \
    mhi_dev_netdev.ko \
    mhi_dev_satellite.ko \
    mhi_dev_uci.ko \
    microdump_collector.ko \
    msm-mmrm.ko \
    msm_ext_display.ko \
    msm_gpi.ko \
    msm_hw_fence.ko \
    msm_lmh_dcvs.ko \
    msm_memshare.ko \
    msm_sharedmem.ko \
    msm_show_epoch.ko \
    msm_show_resume_irq.ko \
    msm_sysstats.ko \
    nb7vpq904m.ko \
    nvmem_qfprom.ko \
    phy-generic.ko \
    phy-msm-m31-eusb2.ko \
    phy-msm-snps-eusb2.ko \
    phy-msm-ssusb-qmp.ko \
    phy-qcom-emu.ko \
    phy-qcom-ufs-qmp-v4-khaje.ko \
    phy-qcom-ufs-qmp-v4-kona.ko \
    phy-qcom-ufs-qmp-v4-lahaina.ko \
    phy-qcom-ufs-qmp-v4-waipio.ko \
    phy-qcom-ufs-qmp-v4.ko \
    pinctrl-spmi-gpio.ko \
    pinctrl-spmi-mpp.ko \
    pinctrl_lpi_dlkm.ko \
    plh_scmi.ko \
    plh_vendor.ko \
    pmic-glink-debug.ko \
    pmic-pon-log.ko \
    pmic_glink.ko \
    policy_engine.ko \
    pwm-qti-lpg.ko \
    q6_dlkm.ko \
    q6_notifier_dlkm.ko \
    q6_pdr_dlkm.ko \
    qca_cld3_kiwi_v2.ko \
    qce50_dlkm.ko \
    qcedev-mod_dlkm.ko \
    qcom-cpufreq-hw-debug.ko \
    qcom-hv-haptics.ko \
    qcom-pon.ko \
    qcom-spmi-adc5-gen3.ko \
    qcom-spmi-temp-alarm.ko \
    qcom-vadc-common.ko \
    qcom_cpuss_sleep_stats.ko \
    qcom_edac.ko \
    qcom_esoc.ko \
    qcom_glink.ko \
    qcom_glink_smem.ko \
    qcom_glink_spss.ko \
    qcom_iommu_debug.ko \
    qcom_ipc_lite.ko \
    qcom_lpm.ko \
    qcom_pil_info.ko \
    qcom_q6v5_pas.ko \
    qcom_ramdump.ko \
    qcom_smd.ko \
    qcom_spss.ko \
    qcom_sysmon.ko \
    qcom_va_minidump.ko \
    qcrypto-msm_dlkm.ko \
    qdss_bridge.ko \
    qfprom-sys.ko \
    qmi_helpers.ko \
    qpnp-amoled-regulator.ko \
    qrng_dlkm.ko \
    qrtr-gunyah.ko \
    qrtr-mhi.ko \
    qrtr-smd.ko \
    qsee_ipc_irq_bridge.ko \
    qseecom_proxy.ko \
    qti-fixed-regulator.ko \
    qti-glink-adc.ko \
    qti-ocp-notifier.ko \
    qti_battery_debug.ko \
    qti_devfreq_cdev.ko \
    qti_qmi_cdev.ko \
    qti_qmi_sensor_v2.ko \
    qti_userspace_cdev.ko \
    radio-i2c-rtc6226-qca.ko \
    rdbg.ko \
    reboot-mode.ko \
    redriver.ko \
    repeater-i2c-eusb2.ko \
    repeater-qti-pmic-eusb2.ko \
    repeater.ko \
    rimps_log.ko \
    rmnet_aps.ko \
    rmnet_core.ko \
    rmnet_ctl.ko \
    rmnet_offload.ko \
    rmnet_perf.ko \
    rmnet_perf_tether.ko \
    rmnet_sch.ko \
    rmnet_shs.ko \
    rmnet_wlan.ko \
    rndisipam.ko \
    rproc_qcom_common.ko \
    rq_stats.ko \
    sdpm_clk.ko \
    sg.ko \
    slim-qcom-ngd-ctrl.ko \
    slimbus.ko \
    smcinvoke_dlkm.ko \
    smp2p.ko \
    smp2p_sleepstate.ko \
    snd-soc-hdmi-codec.ko \
    snd-usb-audio-qmi.ko \
    snd_event_dlkm.ko \
    soc_sleep_stats.ko \
    spcom.ko \
    spf_core_dlkm.ko \
    spi-msm-geni.ko \
    spmi-pmic-arb-debug.ko \
    sps_drv.ko \
    spss_utils.ko \
    stm_console.ko \
    stm_core.ko \
    stm_ftrace.ko \
    stm_p_basic.ko \
    stm_p_ost.ko \
    stub_dlkm.ko \
    subsystem_sleep_stats.ko \
    swr_ctrl_dlkm.ko \
    swr_dlkm.ko \
    swr_dmic_dlkm.ko \
    swr_haptics_dlkm.ko \
    sync_fence.ko \
    synx-driver.ko \
    sys_pm_vx.ko \
    sysmon_subsystem_stats.ko \
    tz_log_dlkm.ko \
    ucsi_glink.ko \
    usb_bam.ko \
    usb_f_ccid.ko \
    usb_f_cdev.ko \
    usb_f_diag.ko \
    usb_f_gsi.ko \
    usb_f_qdss.ko \
    usbmon.ko \
    wcd937x_dlkm.ko \
    wcd937x_slave_dlkm.ko \
    wcd938x_dlkm.ko \
    wcd938x_slave_dlkm.ko \
    wcd939x_dlkm.ko \
    wcd939x_slave_dlkm.ko \
    wcd9xxx_dlkm.ko \
    wcd_core_dlkm.ko \
    wcd_usbss_i2c.ko \
    wlan_firmware_service.ko \
    wsa883x_dlkm.ko \
    wsa884x_dlkm.ko \
    xcharge_chg_manager.ko \
    mpower_logger.ko \
    bootloader_log.ko \
    meizu_key_value.ko \
    mz_rpmb_ctl.ko \
    camcc-crow.ko \
    gpucc-crow.ko \
    gpucc-kalama.ko \
    videocc-crow.ko \
    mz_thermal_log.ko \
    mz_gesture_ts.ko \
    goodix_ts.ko \
    msm_kgsl.ko \
    camera.ko \
    msm_drm.ko \
    qts.ko \
    debugcc-crow.ko \
    debugcc-kalama.ko \
    qti_cpufreq_cdev.ko \
    pm8941-pwrkey.ko \
    qbt_handler.ko \
    ai11ba.ko \
    vl53l1.ko \
    stm_st54se_gpio.ko \
    stm_nfc_i2c.ko \
    nxp-nci.ko \
    nxp-sr100.ko

M2391_SOURCE_RAMDISK_MODULE_NAMES := \
    smartswap_zram.ko \
    frpc-adsprpc.ko \
    mzprobe.ko \
    smart_zsmalloc.ko \
    meizu-sys.ko \
    ufs_qcom.ko \
    ufshcd-crypto-qti.ko \
    sched-walt.ko \
    pci-msm-drv.ko \
    mz_kshrink_slabd.ko \
    mz_freq_qos.ko \
    mz_mrg.ko \
    ep_pcie_drv.ko \
    gunyah.ko \
    msm_geni_serial.ko \
    sdhci-msm.ko \
    qcom_q6v5.ko \
    mz_rstinfo.ko \
    panel_event_notifier.ko \
    dwc3-msm.ko \
    mem_buf.ko \
    mem_buf_dev.ko \
    mem_buf_msgq.ko \
    qcom_iommu_util.ko \
    maudit.ko \
    msm_performance.ko \
    qti_battery_charger.ko \
    mz_hang_dump.ko \
    icc-debug.ko \
    msm_drm.ko \
    qti_amoled_ecm.ko \
    mz_compaction_opt.ko \
    pdr_interface.ko \
    mz_reclaim_opt.ko \
    qcom-dload-mode.ko \
    mz_debug_vendor_hooks.ko \
    minidump.ko \
    mz_enhance_qcom_wdog.ko \
    qcom_wdt_core.ko \
    bootloader_log.ko \
    adsp_sleepmon.ko \
    altmode-glink.ko \
    arm_smmu.ko \
    bam_dma.ko \
    bcl_pmic5.ko \
    bcl_soc.ko \
    boot_mode.ko \
    boot_stats.ko \
    bwmon.ko \
    c1dcvs_scmi.ko \
    c1dcvs_vendor.ko \
    cdsp-loader.ko \
    cdsprm.ko \
    cfg80211.ko \
    charger-ulog-glink.ko \
    clk-rpmh.ko \
    cmd-db.ko \
    core_hang_detect.ko \
    coresight-csr.ko \
    coresight-cti.ko \
    coresight-dummy.ko \
    coresight-funnel.ko \
    coresight-hwevent.ko \
    coresight-remote-etm.ko \
    coresight-replicator.ko \
    coresight-stm.ko \
    coresight-tgu.ko \
    coresight-tmc.ko \
    coresight-tpda.ko \
    coresight-tpdm.ko \
    coresight.ko \
    cpu_hotplug.ko \
    cpu_voltage_cooling.ko \
    cpufreq_stats_scmi.ko \
    cpufreq_stats_vendor.ko \
    cqhci.ko \
    crypto-qti-common.ko \
    crypto-qti-hwkm.ko \
    dcc_v2.ko \
    dcvs_fp.ko \
    ddr_cdev.ko \
    debug-regulator.ko \
    debug_symbol.ko \
    dmesg_dumper.ko \
    ehset.ko \
    eud.ko \
    f_fs_ipc_log.ko \
    fsa4480-i2c.ko \
    gdsc-regulator.ko \
    gh_arm_drv.ko \
    gh_ctrl.ko \
    gh_dbl.ko \
    gh_irq_lend.ko \
    gh_mem_notifier.ko \
    gh_msgq.ko \
    gh_rm_drv.ko \
    gh_tlmm_vm_mem_access.ko \
    gh_virt_wdt.ko \
    glink_pkt.ko \
    glink_probe.ko \
    hdcp_qseecom_dlkm.ko \
    heap_mem_ext_v01.ko \
    hung_task_enh.ko \
    hvc_gunyah.ko \
    hwkm.ko \
    hwmon.ko \
    i2c-msm-geni.ko \
    i3c-master-msm-geni.ko \
    icc-test.ko \
    industrialio-buffer-cb.ko \
    iommu-logger.ko \
    ipa_fmwk.ko \
    kunit.ko \
    leds-qpnp-vibrator-ldo.ko \
    leds-qti-flash.ko \
    leds-qti-tri-led.ko \
    llcc-qcom.ko \
    llcc_perfmon.ko \
    lt9611uxc.ko \
    mac80211.ko \
    max31760_fan.ko \
    mdt_loader.ko \
    mem-hooks.ko \
    mem-offline.ko \
    memlat.ko \
    memlat_scmi.ko \
    memlat_vendor.ko \
    meizu_private.ko \
    mz_bl_lock.ko \
    memory_dump_v2.ko \
    mhi.ko \
    mhi_cntrl_qcom.ko \
    mhi_dev_drv.ko \
    mhi_dev_dtr.ko \
    mhi_dev_net.ko \
    mhi_dev_netdev.ko \
    mhi_dev_satellite.ko \
    mhi_dev_uci.ko \
    microdump_collector.ko \
    msm_dma_iommu_mapping.ko \
    msm-mmrm.ko \
    msm_ext_display.ko \
    msm_gpi.ko \
    msm_hw_fence.ko \
    msm_lmh_dcvs.ko \
    msm_memshare.ko \
    msm_qmp.ko \
    msm_sharedmem.ko \
    msm_show_epoch.ko \
    msm_show_resume_irq.ko \
    msm_sysstats.ko \
    nb7vpq904m.ko \
    nvme-core.ko \
    nvme.ko \
    nvmem_qcom-spmi-sdam.ko \
    nvmem_qfprom.ko \
    phy-generic.ko \
    phy-msm-m31-eusb2.ko \
    phy-msm-snps-eusb2.ko \
    phy-msm-ssusb-qmp.ko \
    phy-qcom-emu.ko \
    phy-qcom-ufs-qmp-v4-crow.ko \
    phy-qcom-ufs-qmp-v4-kalama.ko \
    phy-qcom-ufs-qmp-v4-khaje.ko \
    phy-qcom-ufs-qmp-v4-kona.ko \
    phy-qcom-ufs-qmp-v4-lahaina.ko \
    phy-qcom-ufs-qmp-v4-waipio.ko \
    phy-qcom-ufs-qmp-v4.ko \
    phy-qcom-ufs-qrbtc-sdm845.ko \
    phy-qcom-ufs.ko \
    pinctrl-crow.ko \
    pinctrl-kalama.ko \
    pinctrl-msm.ko \
    pinctrl-spmi-gpio.ko \
    pinctrl-spmi-mpp.ko \
    plh_scmi.ko \
    plh_vendor.ko \
    pmic-glink-debug.ko \
    pmic-pon-log.ko \
    pmic_glink.ko \
    pmu_scmi.ko \
    pmu_vendor.ko \
    policy_engine.ko \
    proxy-consumer.ko \
    pwm-qti-lpg.ko \
    qcom-cpufreq-hw-debug.ko \
    qcom-cpufreq-hw.ko \
    qcom-dcvs.ko \
    qcom-hv-haptics.ko \
    qcom-i2c-pmic.ko \
    qcom-pdc.ko \
    qcom-pmu-lib.ko \
    qcom-pon.ko \
    qcom-reboot-reason.ko \
    qcom-scm.ko \
    qcom-spmi-adc5-gen3.ko \
    qcom-spmi-pmic.ko \
    qcom-spmi-temp-alarm.ko \
    qcom-vadc-common.ko \
    qcom_cpu_vendor_hooks.ko \
    qcom_cpuss_sleep_stats.ko \
    qcom_dma_heaps.ko \
    qcom_edac.ko \
    qcom_esoc.ko \
    qcom_glink.ko \
    qcom_glink_smem.ko \
    qcom_glink_spss.ko \
    qcom_hwspinlock.ko \
    qcom_iommu_debug.ko \
    qcom_ipc_lite.ko \
    qcom_ipcc.ko \
    qcom_llcc_pmu.ko \
    qcom_logbuf_vh.ko \
    qcom_lpm.ko \
    qcom_pil_info.ko \
    qcom_pm8008-regulator.ko \
    qcom_q6v5_pas.ko \
    qcom_ramdump.ko \
    qcom_rimps.ko \
    qcom_rpmh.ko \
    qcom_smd.ko \
    qcom_spss.ko \
    qcom_sysmon.ko \
    qcom_tsens.ko \
    qcom_va_minidump.ko \
    qdss_bridge.ko \
    qfprom-sys.ko \
    qmi_helpers.ko \
    qpnp-amoled-regulator.ko \
    qrng_dlkm.ko \
    qrtr-gunyah.ko \
    qrtr-mhi.ko \
    qrtr-smd.ko \
    qrtr.ko \
    qsee_ipc_irq_bridge.ko \
    qseecom_proxy.ko \
    qti-fixed-regulator.ko \
    qti-glink-adc.ko \
    qti-ocp-notifier.ko \
    qti-regmap-debugfs.ko \
    qti_battery_debug.ko \
    qti_devfreq_cdev.ko \
    qti_qmi_cdev.ko \
    qti_qmi_sensor_v2.ko \
    qti_userspace_cdev.ko \
    rdbg.ko \
    reboot-mode.ko \
    redriver.ko \
    regmap-spmi.ko \
    repeater-i2c-eusb2.ko \
    repeater-qti-pmic-eusb2.ko \
    repeater.ko \
    rimps_log.ko \
    rpmh-regulator.ko \
    rproc_qcom_common.ko \
    rq_stats.ko \
    rtc-pm8xxx.ko \
    sdpm_clk.ko \
    secure_buffer.ko \
    sg.ko \
    slim-qcom-ngd-ctrl.ko \
    slimbus.ko \
    smcinvoke_dlkm.ko \
    smem.ko \
    smp2p.ko \
    smp2p_sleepstate.ko \
    snd-soc-hdmi-codec.ko \
    snd-usb-audio-qmi.ko \
    soc_sleep_stats.ko \
    socinfo.ko \
    spcom.ko \
    spi-msm-geni.ko \
    spmi-pmic-arb-debug.ko \
    spmi-pmic-arb.ko \
    sps_drv.ko \
    spss_utils.ko \
    stm_console.ko \
    stm_core.ko \
    stm_ftrace.ko \
    stm_p_basic.ko \
    stm_p_ost.ko \
    stub-regulator.ko \
    subsystem_sleep_stats.ko \
    sync_fence.ko \
    synx-driver.ko \
    sys_pm_vx.ko \
    sysmon_subsystem_stats.ko \
    thermal_minidump.ko \
    thermal_pause.ko \
    tmecom-intf.ko \
    tz_log_dlkm.ko \
    ucsi_glink.ko \
    usb_bam.ko \
    usb_f_ccid.ko \
    usb_f_cdev.ko \
    usb_f_diag.ko \
    usb_f_gsi.ko \
    usb_f_qdss.ko \
    usbmon.ko \
    wcd_usbss_i2c.ko \
    xcharge_notify_subsys.ko \
    xcharge_chg_manager.ko \
    xcharge_thermal.ko \
    xcharge_mbattery_logger.ko \
    mpower_logger.ko \
    mz_info.ko \
    meizu_key_value.ko \
    qcom_ipc_logging.ko \
    mz_rpmb_ctl.ko \
    qcom_aoss.ko \
    qnoc-qos.ko \
    icc-rpmh.ko \
    icc-bcm-voter.ko \
    clk-qcom.ko \
    camcc-crow.ko \
    camcc-kalama.ko \
    dispcc-crow.ko \
    dispcc-kalama.ko \
    gcc-crow.ko \
    gcc-kalama.ko \
    gpucc-crow.ko \
    gpucc-kalama.ko \
    tcsrcc-kalama.ko \
    videocc-crow.ko \
    videocc-kalama.ko \
    xcharge_chg_interface.ko \
    mz_thermal_log.ko \
    mz_gesture_ts.ko \
    goodix_ts.ko \
    qts.ko \
    clk-dummy.ko \
    debugcc-crow.ko \
    debugcc-kalama.ko \
    qnoc-crow.ko \
    qnoc-kalama.ko \
    mz_thermal_boost.ko \
    qti_cpufreq_cdev.ko \
    pm8941-pwrkey.ko \
    qbt_handler.ko

