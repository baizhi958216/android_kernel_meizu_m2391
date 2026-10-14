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
extbuild qcom/opensource/mmrm-driver MMRM_ROOT="$external/qcom/opensource/mmrm-driver"

release=$(<"$out/include/config/kernel.release")
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
