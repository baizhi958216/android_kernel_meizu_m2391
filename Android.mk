# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),m2391)
M2391_EXTERNAL_SOURCE := kernel/meizu/m2391-modules
M2391_ANDROID_SCRIPTS := $(TARGET_KERNEL_SOURCE)/scripts/m2391
M2391_ANDROID_STAMP := $(M2391_ANDROID_OUT)/kernel.stamp
M2391_ANDROID_MODULE_FILES := $(sort $(BOARD_VENDOR_RAMDISK_KERNEL_MODULES) $(BOARD_VENDOR_KERNEL_MODULES) $(BOARD_SYSTEM_KERNEL_MODULES))
M2391_ANDROID_BASE_DTB := $(M2391_ANDROID_OUT)/m2391.dtb
M2391_MKDTBOIMG := $(HOST_OUT_EXECUTABLES)/mkdtboimg
M2391_DEPMOD := $(HOST_OUT_EXECUTABLES)/depmod
M2391_DEVICE_MODULE_CONFIG := device/meizu/m2391/configs/modules
# Include directories for removals and renames. Kati tracks the find traversal.
M2391_KERNEL_INPUTS := $(shell find -H $(TARGET_KERNEL_SOURCE) $(M2391_EXTERNAL_SOURCE) \
    \( -name .git -o -name __pycache__ -o -path '*/tools/testing/selftests' \) -prune -o \
    ! -name '*.o' ! -name '*.ko' ! -name '*.cmd' ! -name '*.mod' ! -name '*.mod.c' -print)

$(M2391_ANDROID_STAMP): .KATI_IMPLICIT_OUTPUTS := $(TARGET_PREBUILT_KERNEL) $(M2391_ANDROID_BASE_DTB) $(BOARD_PREBUILT_DTBOIMAGE) $(M2391_ANDROID_MODULE_FILES)
.KATI_RESTAT: $(M2391_ANDROID_STAMP)
$(M2391_ANDROID_STAMP): $(M2391_KERNEL_INPUTS) $(addprefix $(M2391_DEVICE_MODULE_CONFIG)/modules.load.,vendor_dlkm vendor_ramdisk recovery) $(M2391_MKDTBOIMG) $(M2391_DEPMOD) $(KERNEL_MAKE_CMD) $(TARGET_KERNEL_CLANG_PATH)/bin/clang
	$(TOOLS_PATH_OVERRIDE) python3 $(M2391_ANDROID_SCRIPTS)/android-build.py \
	    $(M2391_KERNEL_BUILD_OUT) $(M2391_EXTERNAL_SOURCE) $(M2391_DEVICE_MODULE_CONFIG) \
	    $(M2391_MKDTBOIMG) $(M2391_DEPMOD) $(KERNEL_MAKE_CMD) $(KERNEL_MAKE_FLAGS)

$(PRODUCT_OUT)/kernel: $(TARGET_PREBUILT_KERNEL)
	@mkdir -p $(dir $@)
	cp $< $@

$(PRODUCT_OUT)/dtb.img: $(M2391_ANDROID_BASE_DTB)
	@mkdir -p $(dir $@)
	cp $< $@

# Lineage passes the system_dlkm inputs to vendor depmod as PRIVATE_EXTRA_MODULES.
$(call intermediates-dir-for,PACKAGING,depmod_VENDOR)/lib/modules/0.0/modules.dep: $(BOARD_SYSTEM_KERNEL_MODULES)

.PHONY: kernel dtbimage dtboimage
kernel: $(PRODUCT_OUT)/kernel
dtbimage: $(PRODUCT_OUT)/dtb.img
dtboimage: $(PRODUCT_OUT)/dtbo.img
endif
