#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Run in a Linux build environment with LLVM, kmod and the Android kernel toolchain.
set -euo pipefail

if (( $# != 3 )); then
	echo "Usage: $0 KERNEL_OUT MODULES_SOURCE NEW_INSTALL_DIR" >&2
	exit 2
fi
kernel=$(cd "$(dirname "$0")/../.." && pwd -P)
mkdir -p "$1"
out=$(cd "$1" && pwd -P)
external=$(cd "$2" && pwd -P)
# Never overlay stale modules from a different kernel.
mkdir "$3"
stage=$(cd "$3" && pwd -P)
jobs=${JOBS:-$(nproc)}
kmake=(make -C "$kernel" O="$out" ARCH=arm64 LLVM=1)
"${kmake[@]}" gki_defconfig m2391.config

dtbs=(meizu/m2391.dtb)
for source in "$kernel"/arch/arm64/boot/dts/meizu/m2391-*-overlay.dtso; do
	name=${source##*/}
	dtbs+=("meizu/${name%.dtso}.dtbo")
done
"${kmake[@]}" -j"$jobs" Image modules "${dtbs[@]}"
install=(INSTALL_MOD_PATH="$stage" INSTALL_MOD_STRIP=1 DEPMOD=true)
"${kmake[@]}" "${install[@]}" modules_install

# Each external Kbuild writes into its own output directory just like the kernel.
symvers=("$out/Module.symvers")
extbuild() {
	local path=$1
	shift
	local args=(M="$external/$path" MO="$out/m2391-external/$path" "$@")
	"${kmake[@]}" "${args[@]}" -j"$jobs" modules
	"${kmake[@]}" "${args[@]}" "${install[@]}" modules_install
	symvers+=("$out/m2391-external/$path/Module.symvers")
}
extbuild meizu/system
extbuild qcom/opensource/touch-drivers/mz_gesture \
	KBUILD_EXTRA_SYMBOLS="$out/m2391-external/meizu/system/Module.symvers"
extbuild meizu/power-notify
extbuild meizu/storage
extbuild meizu/tof-ai11ba KBUILD_EXTRA_SYMBOLS="$out/m2391-external/meizu/system/Module.symvers"
tof_symbols=("$out/m2391-external/meizu/system/Module.symvers"
             "$out/m2391-external/meizu/storage/Module.symvers"
             "$out/m2391-external/meizu/tof-ai11ba/Module.symvers")
extbuild meizu/tof-vl53l1 KBUILD_EXTRA_SYMBOLS="${tof_symbols[*]}"
extbuild meizu/v4l2loopback
extbuild meizu/pwm-ir
extbuild meizu/audio-dummy
extbuild meizu/cs35l45
extbuild meizu/cirrus-calibration KBUILD_EXTRA_SYMBOLS="$out/m2391-external/meizu/storage/Module.symvers"
extbuild meizu/power-logger
extbuild meizu/power-thermal
extbuild meizu/power-interface KBUILD_EXTRA_SYMBOLS="$out/m2391-external/meizu/system/Module.symvers"
extbuild meizu/power-battery-logger KBUILD_EXTRA_SYMBOLS="$out/m2391-external/meizu/power-logger/Module.symvers"
power_symbols=("$out/m2391-external/meizu/storage/Module.symvers"
	       "$out/m2391-external/meizu/power-logger/Module.symvers"
	       "$out/m2391-external/meizu/power-interface/Module.symvers")
extbuild meizu/power-manager KBUILD_EXTRA_SYMBOLS="${power_symbols[*]}"
charger_symbols=("$out/m2391-external/meizu/power-notify/Module.symvers"
		 "$out/m2391-external/meizu/power-battery-logger/Module.symvers"
		 "$out/m2391-external/meizu/power-manager/Module.symvers"
		 "$out/m2391-external/meizu/power-thermal/Module.symvers"
		 "${power_symbols[@]}")
extbuild meizu/power-charger KBUILD_EXTRA_SYMBOLS="${charger_symbols[*]}"
extbuild qcom/opensource/pmic-haptics \
	KBUILD_EXTRA_SYMBOLS="$out/m2391-external/meizu/power-charger/Module.symvers"
touch=qcom/opensource/touch-drivers
touch_symbols=("$out/m2391-external/meizu/system/Module.symvers"
	      "$out/m2391-external/$touch/mz_gesture/Module.symvers"
	      "$out/m2391-external/meizu/power-notify/Module.symvers")
extbuild "$touch" TOUCH_ROOT="$external/$touch" CONFIG_ARCH_KALAMA=y CONFIG_MSM_TOUCH=m \
	CONFIG_QTS_ENABLE=y CONFIG_TOUCHSCREEN_GOODIX_BRL=y \
	CONFIG_TOUCHSCREEN_GOODIX_BRL_MEIZU=y CONFIG_TOUCHSCREEN_NT36XXX_I2C=n \
	CONFIG_TOUCHSCREEN_ATMEL_MXT=n CONFIG_TOUCHSCREEN_DUMMY=n \
	KBUILD_EXTRA_SYMBOLS="${touch_symbols[*]}"
extbuild qcom/opensource/mmrm-driver MMRM_ROOT="$external/qcom/opensource/mmrm-driver"

secure=qcom/opensource/securemsm-kernel
extbuild "$secure/smcinvoke"
extbuild "$secure/qrng"
extbuild "$secure/crypto-qti"
extbuild "$secure/tz_log"
extbuild meizu/rpmb-control KBUILD_EXTRA_SYMBOLS="$out/m2391-external/meizu/storage/Module.symvers"
extbuild "$secure/hdcp" KBUILD_EXTRA_SYMBOLS="$out/m2391-external/$secure/smcinvoke/Module.symvers"

extbuild meizu/boot-mode
input_symbols=("$out/m2391-external/meizu/boot-mode/Module.symvers"
               "$out/m2391-external/meizu/system/Module.symvers"
               "$out/m2391-external/meizu/storage/Module.symvers")
for path in meizu/haptic-hv meizu/pressure-f71 meizu/pressure-m68; do
	extbuild "$path" KBUILD_EXTRA_SYMBOLS="${input_symbols[*]}"
done
extbuild meizu/aks-adc
extbuild meizu/aks-joystick
extbuild nxp/opensource/uwb
extbuild st/opensource/st54se-gpio
for path in nxp/opensource/driver st/opensource/nfc; do
	extbuild "$path" KBUILD_EXTRA_SYMBOLS="$out/m2391-external/$secure/smcinvoke/Module.symvers"
done

mm=qcom/opensource/mm-drivers
extbuild "$mm/msm_ext_display" MSM_EXT_DISPLAY_ROOT="$external/$mm/"
extbuild "$mm/sync_fence" SYNC_FENCE_ROOT="$external/$mm/"
extbuild "$mm/hw_fence" MSM_HW_FENCE_ROOT="$external/$mm/"

display=qcom/opensource/display-drivers
display_symbols=("$out/m2391-external/meizu/system/Module.symvers"
		 "$out/m2391-external/$mm/msm_ext_display/Module.symvers"
		 "$out/m2391-external/$mm/hw_fence/Module.symvers"
		 "$out/m2391-external/$mm/sync_fence/Module.symvers"
		 "$out/m2391-external/qcom/opensource/mmrm-driver/Module.symvers"
		 "$out/m2391-external/$secure/hdcp/Module.symvers"
		 "$out/m2391-external/$secure/smcinvoke/Module.symvers")
extbuild "$display/msm" DISPLAY_ROOT="$external/$display" KERNEL_SRC="$kernel" \
	CONFIG_ARCH_KALAMA=y KBUILD_EXTRA_SYMBOLS="${display_symbols[*]}"
gpu=qcom/opensource/graphics-kernel
extbuild "$gpu" KGSL_PATH="$external/$gpu" KERNEL_SRC="$kernel" CONFIG_ARCH_KALAMA=y \
	KBUILD_EXTRA_SYMBOLS="$out/m2391-external/$mm/hw_fence/Module.symvers"

audio=qcom/opensource/audio-kernel
audio_symbols=("$out/m2391-external/$mm/msm_ext_display/Module.symvers"
	       "$out/m2391-external/meizu/power-charger/Module.symvers")
extbuild "$audio" AUDIO_ROOT="$external/$audio" MODNAME=audio_dlkm BOARD_PLATFORM=kalama \
	CONFIG_ARCH_KALAMA=y KBUILD_EXTRA_SYMBOLS="${audio_symbols[*]}"

ipa=qcom/opensource/dataipa
extbuild "$ipa" DATAIPA_ROOT="$external/$ipa" CONFIG_ARCH_KALAMA=y CONFIG_IPA3=m \
	CONFIG_IPA3_MHI_PRIME_MANAGER=n

camera=qcom/opensource/camera-kernel
camera_symbols=("$out/m2391-external/qcom/opensource/mmrm-driver/Module.symvers"
		"$out/m2391-external/meizu/system/Module.symvers"
		"$out/m2391-external/meizu/storage/Module.symvers")
extbuild "$camera" CAMERA_KERNEL_ROOT="$external/$camera" KERNEL_ROOT="$kernel" \
	KERNEL_SRC="$kernel" CONFIG_SPECTRA_CAMERA=m BOARD_PLATFORM=kalama MODNAME=camera \
	KBUILD_EXTRA_SYMBOLS="${camera_symbols[*]}"

media_symbols="$out/m2391-external/qcom/opensource/mmrm-driver/Module.symvers"
video=qcom/opensource/video-driver
extbuild "$video" VIDEO_ROOT="$external/$video" CONFIG_ARCH_KALAMA=y \
	BOARD_PLATFORM=kalama KBUILD_EXTRA_SYMBOLS="$media_symbols"
eva=qcom/opensource/eva-kernel
extbuild "$eva" EVA_ROOT="$external/$eva" CONFIG_ARCH_KALAMA=y \
	BOARD_PLATFORM=kalama KBUILD_EXTRA_SYMBOLS="$media_symbols"

wlan=qcom/opensource/wlan/platform
extbuild "$wlan" WLAN_PLATFORM_ROOT="$external/$wlan" CONFIG_CNSS_OUT_OF_TREE=y \
	CONFIG_CNSS_GENL=m CONFIG_WCNSS_MEM_PRE_ALLOC=m CONFIG_CNSS_UTILS=m \
	CONFIG_CNSS_QMI_SVC=m CONFIG_CNSS_PLAT_IPC_QMI_SVC=m \
	CONFIG_CNSS2=m CONFIG_CNSS2_QMI=y CONFIG_CNSS_HW_SECURE_DISABLE=y \
	KBUILD_EXTRA_SYMBOLS="$out/m2391-external/$secure/smcinvoke/Module.symvers"
wifi=qcom/opensource/wlan
cld="$wifi/qcacld-3.0"
extbuild "$cld" WLAN_PLATFORM_ROOT="$external/$wlan" WLAN_ROOT="$external/$cld" \
	WLAN_COMMON_ROOT=cmn WLAN_COMMON_INC="$external/$wifi/qca-wifi-host-cmn" \
	WLAN_FW_API="$external/$wifi/fw-api" WLAN_PROFILE=kiwi_v2 DYNAMIC_SINGLE_CHIP=kiwi_v2 \
	DEVNAME=kiwi_v2 WLAN_CTRL_NAME=wlan CONFIG_QCA_CLD_WLAN=m CONFIG_BUS_AUTO_SUSPEND=y \
	CONFIG_ARCH_KALAMA=y CONFIG_CNSS_OUT_OF_TREE=y CONFIG_CNSS2=m CONFIG_CNSS2_QMI=y \
	CONFIG_CNSS_UTILS=m CONFIG_CNSS_GENL=m CONFIG_WCNSS_MEM_PRE_ALLOC=m \
	CONFIG_CNSS_QMI_SVC=m CONFIG_CNSS_PLAT_IPC_QMI_SVC=m CONFIG_CNSS_HW_SECURE_DISABLE=y \
	BOARD_PLATFORM=kalama MODNAME=kiwi_v2 KBUILD_EXTRA_SYMBOLS="$out/m2391-external/$wlan/Module.symvers"
bt=qcom/opensource/bt-kernel
extbuild "$bt" BT_ROOT="$external/$bt" CONFIG_MSM_BT_POWER=m CONFIG_BTFM_SLIM=m \
	CONFIG_SLIM_BTFM_CODEC=n CONFIG_I2C_RTC6226_QCA=m \
	KBUILD_EXTRA_SYMBOLS="$out/m2391-external/$wlan/Module.symvers"

rmnet=qcom/opensource/datarmnet/core
extbuild "$rmnet" RMNET_CORE_ROOT="$external/$rmnet" RMNET_CORE_INC_DIR="$external/$rmnet" \
	CONFIG_ARCH_KALAMA=y

for feature in aps offload perf perf_tether sch shs wlan; do
	extbuild "qcom/opensource/datarmnet-ext/$feature" CONFIG_ARCH_KALAMA=y \
		RMNET_CORE_INC_DIR="$external/$rmnet" \
		KBUILD_EXTRA_SYMBOLS="$out/m2391-external/$rmnet/Module.symvers"
done

release=$(<"$out/include/config/kernel.release")
# Flyme's WLAN loader opens this filename. The module advertises kiwi_v2 internally.
updates="$stage/lib/modules/$release/updates"
mv "$updates/kiwi_v2.ko" "$updates/qca_cld3_kiwi_v2.ko"
for order in "$updates"/modules.order.*; do
	sed -i 's@/kiwi_v2\.ko$@/qca_cld3_kiwi_v2.ko@' "$order"
done
cat "${symvers[@]}" > "$stage/Module.symvers"
depmod -ae -E "$stage/Module.symvers" -b "$stage" "$release" 2>"$stage/depmod.log"
if [[ -s "$stage/depmod.log" ]]; then
	cat "$stage/depmod.log" >&2
	exit 1
fi
# modules.load for first-stage init, including suppliers linked only through DT.
python3 "$kernel/scripts/m2391/first-stage.py" "$stage/lib/modules/$release"
cp "$out/arch/arm64/boot/Image" "$stage/Image"
cp "$out/.config" "$stage/kernel.config"
cp "$out/certs/signing_key.x509" "$stage/modules-signing.x509"
mkdir "$stage/dts"
cp "$kernel/arch/arm64/boot/dts/meizu/dtbo.cfg" "$stage/dts/"
for dt in "${dtbs[@]}"; do
	cp "$out/arch/arm64/boot/dts/$dt" "$stage/dts/"
done
# Kbuild leaves an absolute symlink to the local build tree in modules_install.
if [[ -L "$stage/lib/modules/$release/build" ]]; then
	unlink "$stage/lib/modules/$release/build"
fi
