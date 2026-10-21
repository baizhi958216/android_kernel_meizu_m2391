# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),m2391)

M2391_KERNEL_BUILD_OUT := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
M2391_KERNEL_BUILD_IMAGE := $(M2391_KERNEL_BUILD_OUT)/arch/arm64/boot/Image
M2391_KERNEL_CONFIG := $(M2391_KERNEL_BUILD_OUT)/.config
M2391_KERNEL_STAMP := $(M2391_KERNEL_BUILD_OUT)/m2391-kernel.stamp
M2391_KERNEL_DTB := $(M2391_KERNEL_BUILD_OUT)/arch/arm64/boot/dts/meizu/m2391.dtb
M2391_KERNEL_OVERLAYS := $(patsubst $(LOCAL_PATH)/%.dts,$(M2391_KERNEL_BUILD_OUT)/%.dtbo,$(wildcard $(LOCAL_PATH)/arch/arm64/boot/dts/meizu/*-overlay.dts))
M2391_DTC := $(HOST_OUT_EXECUTABLES)/dtc
M2391_MKDTBOIMG := $(HOST_OUT_EXECUTABLES)/mkdtboimg

M2391_MAKE_KERNEL = $(TOOLS_PATH_OVERRIDE) \
    PATH=$(TARGET_KERNEL_CLANG_PATH)/bin:$(abspath $(HOST_OUT_EXECUTABLES)):$$PATH \
    LD_LIBRARY_PATH=$(TARGET_KERNEL_CLANG_PATH)/lib64:$$LD_LIBRARY_PATH \
    $(KERNEL_MAKE_CMD) $(KERNEL_MAKE_FLAGS) $(TARGET_KERNEL_ADDITIONAL_FLAGS) \
    -C $(M2391_KERNEL_PATH) O=$(abspath $(M2391_KERNEL_BUILD_OUT)) \
    ARCH=arm64 CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- \
    CC="$(KERNEL_CC_WRAPPER) clang" LD=ld.lld DTC_EXT=$(abspath $(M2391_DTC))

M2391_KERNEL_BUILD_INPUTS := $(shell find $(LOCAL_PATH) -name .git -prune -o -name __pycache__ -prune -o -type f -print)
M2391_KERNEL_RAW_MODULES := $(addprefix $(M2391_KERNEL_BUILD_OUT)/,$(M2391_SOURCE_MODULE_PATHS))
M2391_KERNEL_MODULE_OUTPUTS := $(addprefix $(M2391_KERNEL_BUILD_OUT)/,Module.symvers scripts/sign-file certs/signing_key.pem certs/signing_key.x509)
M2391_KERNEL_OUTPUTS := $(M2391_KERNEL_BUILD_IMAGE) $(M2391_KERNEL_RAW_MODULES) $(M2391_KERNEL_DTB) $(M2391_KERNEL_OVERLAYS) $(M2391_KERNEL_MODULE_OUTPUTS)

$(M2391_KERNEL_CONFIG): $(M2391_KERNEL_DEFCONFIG) $(LOCAL_PATH)/BoardConfigKernel.mk $(LOCAL_PATH)/Android.mk
	@mkdir -p $(dir $@)
	cp $(M2391_KERNEL_DEFCONFIG) $@
	$(M2391_MAKE_KERNEL) olddefconfig

$(M2391_KERNEL_STAMP): .KATI_IMPLICIT_OUTPUTS := $(M2391_KERNEL_OUTPUTS)
.KATI_RESTAT: $(M2391_KERNEL_STAMP)
$(M2391_KERNEL_STAMP): $(M2391_KERNEL_CONFIG) $(M2391_KERNEL_BUILD_INPUTS) $(M2391_DTC) $(KERNEL_MAKE_CMD) $(TARGET_KERNEL_CLANG_PATH)/bin/clang
	@echo "Building m2391 kernel, DTBs and source modules"
	$(M2391_MAKE_KERNEL) $(BOARD_KERNEL_IMAGE_NAME)
	$(M2391_MAKE_KERNEL) dtbs
	$(M2391_MAKE_KERNEL) modules
	@for output in $(M2391_KERNEL_OUTPUTS); do \
	    test -s "$$output" || { echo "Missing kernel output: $$output" >&2; exit 1; }; \
	done

	@touch $(M2391_KERNEL_OUTPUTS) $@

$(PRODUCT_OUT)/kernel: $(M2391_KERNEL_BUILD_IMAGE)
	@mkdir -p $(dir $@)
	cp $< $@

$(PRODUCT_OUT)/dtb.img: $(M2391_KERNEL_DTB)
	@mkdir -p $(dir $@)
	cp $< $@

$(BOARD_PREBUILT_DTBOIMAGE): $(M2391_KERNEL_OVERLAYS) $(BOARD_DTBO_CFG) $(M2391_MKDTBOIMG)
	$(M2391_MKDTBOIMG) cfg_create $@ $(BOARD_DTBO_CFG) -d $(M2391_KERNEL_BUILD_OUT)/arch/arm64/boot/dts

.PHONY: kernel dtbimage dtboimage
kernel: $(PRODUCT_OUT)/kernel
dtbimage: $(PRODUCT_OUT)/dtb.img
dtboimage: $(PRODUCT_OUT)/dtbo.img

define m2391-source-module
$(M2391_SOURCE_MODULES_OUT)/$(notdir $(1)): $(M2391_KERNEL_STAMP) $(M2391_KERNEL_BUILD_OUT)/$(1)
	@echo "m2391 source kernel module: $(notdir $(1))"
	@test -s $(M2391_KERNEL_BUILD_OUT)/$(1)
	@mkdir -p $$(dir $$@)
	$(TARGET_KERNEL_CLANG_PATH)/bin/llvm-strip --strip-debug -o $$@ $(M2391_KERNEL_BUILD_OUT)/$(1)
	$(M2391_KERNEL_BUILD_OUT)/scripts/sign-file sha1 $(M2391_KERNEL_BUILD_OUT)/certs/signing_key.pem $(M2391_KERNEL_BUILD_OUT)/certs/signing_key.x509 $$@
endef
$(foreach module,$(M2391_SOURCE_MODULE_PATHS),$(eval $(call m2391-source-module,$(module))))

M2391_EXTERNAL_PLAN := $(M2391_KERNEL_BUILD_OUT)/m2391-external-plan.tsv
M2391_EXTERNAL_STAMP := $(M2391_KERNEL_BUILD_OUT)/m2391-external.stamp
M2391_EXTERNAL_OUTPUTS := $(addprefix $(M2391_SOURCE_MODULES_OUT)/,$(M2391_SOURCE_EXTERNAL_MODULE_NAMES))
M2391_EXTERNAL_INPUTS := $(shell find kernel/meizu/m2391-modules -type f \( -name '*.c' -o -name '*.h' -o -name Kbuild -o -name Makefile -o -name '*.conf' \) ! -name '*.mod.c')
$(M2391_EXTERNAL_STAMP): .KATI_IMPLICIT_OUTPUTS := $(M2391_EXTERNAL_OUTPUTS)
.KATI_RESTAT: $(M2391_EXTERNAL_STAMP)
$(M2391_EXTERNAL_STAMP): $(M2391_KERNEL_STAMP) $(M2391_KERNEL_MODULE_OUTPUTS) $(M2391_KERNEL_RAW_MODULES) $(M2391_EXTERNAL_INPUTS) $(LOCAL_PATH)/source-modules.mk $(LOCAL_PATH)/scripts/m2391/build_external.sh $(wildcard vendor/meizu/m2391/proprietary/vendor_dlkm/lib/modules/*.ko) $(wildcard vendor/meizu/m2391/proprietary/vendor_boot/lib/modules/*.ko)
	@mkdir -p $(dir $(M2391_EXTERNAL_PLAN))
	@{ $(foreach group,$(M2391_EXTERNAL_GROUPS),printf '%s\t%s\t%s\n' '$(group)' '$(M2391_EXTERNAL_BUILT_MODULE_NAMES.$(group))' '$(M2391_EXTERNAL_MODULES.$(group))';) } > $(M2391_EXTERNAL_PLAN).tmp
	mv $(M2391_EXTERNAL_PLAN).tmp $(M2391_EXTERNAL_PLAN)
	bash $(M2391_KERNEL_PATH)/scripts/m2391/build_external.sh $(abspath .) $(abspath $(M2391_KERNEL_BUILD_OUT)) $(shell getconf _NPROCESSORS_ONLN) $(M2391_KERNEL_VERSION_FLAGS)
	@for output in $(M2391_EXTERNAL_OUTPUTS); do \
	    test -s "$$output" || { echo "Missing external module: $$output" >&2; exit 1; }; \
	done
	@touch $(M2391_EXTERNAL_OUTPUTS) $@
endif
