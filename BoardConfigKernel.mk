# SPDX-License-Identifier: Apache-2.0
M2391_KERNEL_PATH := kernel/meizu/m2391
M2391_KERNEL_BLOBS := vendor/meizu/m2391/proprietary

M2391_KERNEL_SRC := $(M2391_KERNEL_PATH)
M2391_KERNEL_DEFCONFIG := $(M2391_KERNEL_PATH)/arch/arm64/configs/m2391_defconfig

TARGET_KERNEL_SOURCE := $(M2391_KERNEL_SRC)
TARGET_KERNEL_CONFIG := m2391_defconfig
TARGET_KERNEL_VERSION := 5.15
# Preserve the ACK branch and KMI generation from build.config.constants/common
# when building directly with Make rather than the upstream build.sh wrapper.
M2391_KERNEL_VERSION_FLAGS := BRANCH=android13-5.15 KMI_GENERATION=8
TARGET_KERNEL_ADDITIONAL_FLAGS += $(M2391_KERNEL_VERSION_FLAGS)
TARGET_BOARD_KERNEL_HEADERS += $(M2391_KERNEL_PATH)/kernel-headers
TARGET_NO_KERNEL_OVERRIDE := true
M2391_KERNEL_BUILD_OUT = $(OUT_DIR)/target/product/$(TARGET_DEVICE)/obj/KERNEL_OBJ
TARGET_PREBUILT_KERNEL = $(M2391_KERNEL_BUILD_OUT)/arch/arm64/boot/Image

# Pin the toolchain the kernel itself designates.  This 5.15 ACK source
# declares CLANG_VERSION=r450784e in build.config.constants, and the defconfig
# enables LTO_CLANG_FULL + CFI_CLANG, so the kernel must be built with that
# exact clang rather than the platform's clang-stable (r596125), which
# additionally rejects this source with -Wdefault-const-init-var-unsafe.
#
# The prebuilt is not part of the LineageOS 24 / AOSP 17 prebuilts; fetch it
# with (see patches/README.md):
#   curl -L -o clang.tar.gz \
#     "https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/heads/android14-release/clang-r450784e.tar.gz"
#   mkdir -p prebuilts/clang/kernel/linux-x86/clang-r450784e
#   tar -xzf clang.tar.gz -C prebuilts/clang/kernel/linux-x86/clang-r450784e
TARGET_KERNEL_CLANG_VERSION := r450784e
# `=` (deferred) on purpose: $(BUILD_TOP) is not defined yet while BoardConfig
# files are parsed, so `:=` would bake in a broken "/prebuilts/..." path.
TARGET_KERNEL_CLANG_PATH = $(BUILD_TOP)/prebuilts/clang/kernel/linux-x86/clang-r450784e
# Keep automatic BTF feature detection identical to the stock GKI config.
TARGET_KERNEL_ADDITIONAL_FLAGS += PAHOLE=$(abspath prebuilts/kernel-build-tools/linux-x86/bin/pahole)
BOARD_KERNEL_IMAGE_NAME := Image

# ---------------------------------------------------------------------------
# Boot image geometry and board DTB
# ---------------------------------------------------------------------------
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_CMDLINE := video=vfb:640x400,bpp=32,memsize=3072000 \
    qcom_geni_serial.con_enabled=1 nosoftlockup page_poison=1 \
    sysrq_always_enabled firmware_class.path=/vendor/firmware bootconfig
BOARD_BOOTCONFIG := androidboot.hardware=qcom androidboot.memcg=1 \
    androidboot.usbcontroller=a600000.dwc3
BOARD_MKBOOTIMG_ARGS += --header_version 4 --kernel_offset 0x00008000 \
    --ramdisk_offset 0x01000000 --tags_offset 0x00000100 --dtb_offset 0x01f00000
BOARD_INCLUDE_DTB_IN_BOOTIMG := true

# Build all stock board overlays from the decompiled DTS sources. The config
# fixes their order and preserves the original v0 table metadata.
BOARD_KERNEL_SEPARATED_DTBO := true
BOARD_DTBO_CFG := $(M2391_KERNEL_PATH)/arch/arm64/boot/dts/meizu/dtbo.cfg
BOARD_PREBUILT_DTBOIMAGE = $(M2391_KERNEL_BUILD_OUT)/m2391-dtbo.img

BOARD_DO_NOT_STRIP_VENDOR_MODULES := true
BOARD_DO_NOT_STRIP_VENDOR_RAMDISK_MODULES := true
include $(M2391_KERNEL_PATH)/source-modules.mk
BOARD_VENDOR_KERNEL_MODULES = $(wildcard $(M2391_KERNEL_BLOBS)/vendor_dlkm/lib/modules/*.ko) \
    $(addprefix $(M2391_SOURCE_MODULES_OUT)/,$(M2391_SOURCE_VENDOR_MODULE_NAMES))
BOARD_VENDOR_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/configs/modules/modules.load.vendor_dlkm))
BOARD_VENDOR_KERNEL_MODULES_BLOCKLIST_FILE := $(DEVICE_PATH)/configs/modules/modules.blocklist.vendor_dlkm
BOARD_VENDOR_RAMDISK_KERNEL_MODULES = $(wildcard $(M2391_KERNEL_BLOBS)/vendor_boot/lib/modules/*.ko) \
    $(addprefix $(M2391_SOURCE_MODULES_OUT)/,$(M2391_SOURCE_RAMDISK_MODULE_NAMES))
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/configs/modules/modules.load.vendor_ramdisk))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/configs/modules/modules.load.recovery))
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_BLOCKLIST_FILE := $(DEVICE_PATH)/configs/modules/modules.blocklist.vendor_ramdisk
