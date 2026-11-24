# SPDX-License-Identifier: Apache-2.0
TARGET_KERNEL_SOURCE := kernel/meizu/m2391
TARGET_KERNEL_CONFIG := gki_defconfig m2391.config
TARGET_KERNEL_VERSION := 6.18
TARGET_KERNEL_CLANG_VERSION := r596125
BOARD_KERNEL_IMAGE_NAME := Image
TARGET_KERNEL_DTB := dtbs
TARGET_DTB_LIST_WILDCARD := meizu/m2391

BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_CMDLINE := video=vfb:640x400,bpp=32,memsize=3072000 nosoftlockup \
    page_poison=1 sysrq_always_enabled firmware_class.path=/vendor/firmware bootconfig
BOARD_BOOTCONFIG := androidboot.hardware=qcom androidboot.memcg=1 \
    androidboot.usbcontroller=a600000.dwc3
BOARD_MKBOOTIMG_ARGS += --header_version 4 --kernel_offset 0x00008000 \
    --ramdisk_offset 0x01000000 --tags_offset 0x00000100 --dtb_offset 0x01f00000
BOARD_INCLUDE_DTB_IN_BOOTIMG := true
BOARD_KERNEL_SEPARATED_DTBO := true
BOARD_DTBO_CFG := $(TARGET_KERNEL_SOURCE)/arch/arm64/boot/dts/meizu/dtbo.cfg

# Kernel modules (inline out-of-tree build)
TARGET_AUTO_COLLECT_KERNEL_MODULE_DEPS := true
TARGET_MODULE_ALIASES := kiwi_v2.ko:qca_cld3_kiwi_v2.ko

BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/configs/modules/modules.load.vendor_ramdisk))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/configs/modules/modules.load.recovery))
BOOT_KERNEL_MODULES := $(sort $(BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD) $(BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD))
BOARD_VENDOR_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/configs/modules/modules.load.vendor_dlkm))
BOARD_SYSTEM_KERNEL_MODULES_LOAD := false
BOARD_DO_NOT_STRIP_VENDOR_MODULES := true
BOARD_DO_NOT_STRIP_VENDOR_RAMDISK_MODULES := true
BOARD_VENDOR_KERNEL_MODULES_BLOCKLIST_FILE := $(DEVICE_PATH)/configs/modules/modules.blocklist.vendor_dlkm
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_BLOCKLIST_FILE := $(DEVICE_PATH)/configs/modules/modules.blocklist.vendor_ramdisk

TARGET_KERNEL_EXT_MODULE_ROOT := kernel/meizu/m2391-modules
TARGET_KERNEL_EXT_MODULES := \
    meizu/system \
    qcom/opensource/touch-drivers/mz_gesture \
    meizu/power-notify \
    meizu/storage \
    meizu/tof-ai11ba \
    meizu/tof-vl53l1 \
    meizu/pwm-ir \
    meizu/audio-dummy \
    meizu/cs35l45 \
    meizu/cirrus-calibration \
    meizu/power-logger \
    meizu/power-thermal \
    meizu/power-interface \
    meizu/power-battery-logger \
    meizu/power-manager \
    meizu/power-charger \
    qcom/opensource/pmic-haptics \
    qcom/opensource/touch-drivers \
    qcom/opensource/mmrm-driver \
    qcom/opensource/securemsm-kernel/smcinvoke \
    qcom/opensource/securemsm-kernel/qrng \
    qcom/opensource/securemsm-kernel/crypto-qti \
    qcom/opensource/securemsm-kernel/hdcp \
    meizu/boot-mode \
    meizu/haptic-hv \
    nxp/opensource/uwb \
    nxp/opensource/driver \
    qcom/opensource/mm-drivers/msm_ext_display \
    qcom/opensource/mm-drivers/sync_fence \
    qcom/opensource/mm-drivers/hw_fence \
    qcom/opensource/display-drivers/msm \
    qcom/opensource/graphics-kernel \
    qcom/opensource/audio-kernel \
    qcom/opensource/dataipa \
    qcom/opensource/camera-kernel \
    qcom/opensource/video-driver \
    qcom/opensource/eva-kernel \
    qcom/opensource/wlan/platform \
    qcom/opensource/wlan/qcacld-3.0 \
    qcom/opensource/bt-kernel \
    qcom/opensource/datarmnet/core \
    qcom/opensource/datarmnet-ext/aps \
    qcom/opensource/datarmnet-ext/offload \
    qcom/opensource/datarmnet-ext/perf \
    qcom/opensource/datarmnet-ext/perf_tether \
    qcom/opensource/datarmnet-ext/sch \
    qcom/opensource/datarmnet-ext/shs \
    qcom/opensource/datarmnet-ext/wlan
