# SPDX-License-Identifier: Apache-2.0
M2391_KERNEL_PATH := kernel/meizu/m2391
M2391_KERNEL_BLOBS := vendor/meizu/m2391/proprietary

M2391_KERNEL_SRC := $(M2391_KERNEL_PATH)
M2391_KERNEL_DEFCONFIG := $(M2391_KERNEL_PATH)/arch/arm64/configs/m2391_defconfig

TARGET_KERNEL_SOURCE := $(M2391_KERNEL_SRC)
TARGET_KERNEL_CONFIG := m2391_defconfig
TARGET_KERNEL_VERSION := 5.15

# clang-stable (r596125 / clang 20) rejects the 5.15 ACK source with
# -Wdefault-const-init-var-unsafe.  Pin the clang this tree still ships and
# that the kernel is known to build with.
TARGET_KERNEL_CLANG_VERSION := r547379
BOARD_KERNEL_IMAGE_NAME := Image

# ---------------------------------------------------------------------------
# Boot image geometry and board DTB
# ---------------------------------------------------------------------------
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_CMDLINE := video=vfb:640x400,bpp=32,memsize=3072000 \
    qcom_geni_serial.con_enabled=1 nosoftlockup page_poison=1 \
    sysrq_always_enabled bootconfig
BOARD_BOOTCONFIG := androidboot.hardware=qcom androidboot.memcg=1 \
    androidboot.usbcontroller=a600000.dwc3
BOARD_MKBOOTIMG_ARGS += --header_version 4 --kernel_offset 0x00008000 \
    --ramdisk_offset 0x01000000 --tags_offset 0x00000100 --dtb_offset 0x01f00000
BOARD_INCLUDE_DTB_IN_BOOTIMG := true
TARGET_KERNEL_DTB := dtbs
TARGET_DTB_LIST_WILDCARD := meizu/m2391

BOARD_PREBUILT_DTBOIMAGE := vendor/meizu/m2391/radio/dtbo.img

BOARD_DO_NOT_STRIP_VENDOR_MODULES := true
BOARD_DO_NOT_STRIP_VENDOR_RAMDISK_MODULES := true
BOARD_VENDOR_KERNEL_MODULES := $(wildcard $(M2391_KERNEL_BLOBS)/vendor_dlkm/lib/modules/*.ko)
BOARD_VENDOR_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/configs/modules/modules.load.vendor_dlkm))
BOARD_VENDOR_KERNEL_MODULES_BLOCKLIST_FILE := $(DEVICE_PATH)/configs/modules/modules.blocklist.vendor_dlkm
BOARD_VENDOR_RAMDISK_KERNEL_MODULES := $(wildcard $(M2391_KERNEL_BLOBS)/vendor_boot/lib/modules/*.ko)
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/configs/modules/modules.load.vendor_ramdisk))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/configs/modules/modules.load.recovery))
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_BLOCKLIST_FILE := $(DEVICE_PATH)/configs/modules/modules.blocklist.vendor_ramdisk
