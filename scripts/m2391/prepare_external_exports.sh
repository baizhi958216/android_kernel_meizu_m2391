#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Produce core modpost inputs from the actual source providers, before the
# core image is linked. No stock version table supplies these exports.
set -euo pipefail
source "$(dirname "$0")/build_external.sh"

top=$(cd "$1" && pwd)
out=$(cd "$2" && pwd)
jobs=$3
shift 3
version_flags=("$@")
table="$out/m2391-core-external.symvers"
if ! grep -qx 'CONFIG_M2391_CHARGER_RECOVERY=y' "$out/.config"; then
    : > "$table"
    exit 0
fi

original="$top/kernel/meizu/m2391-modules"
mirror="$out/m2391-external-src/m2391-modules"
stage="$out/m2391-source-export-objects"
clang="$top/prebuilts/clang/kernel/linux-x86/clang-r450784e"
READELF="$clang/bin/llvm-readelf"
export PATH="$clang/bin:$top/prebuilts/kernel-build-tools/linux-x86/bin:$top/prebuilts/build-tools/linux-x86/bin:$PATH"
export LD_LIBRARY_PATH="$clang/lib64:${LD_LIBRARY_PATH:-}"
unset ANDROID_BUILD_TOP
mkdir -p "$mirror" "$stage"
git --no-optional-locks -C "$original" ls-files --cached --others --exclude-standard -z > "$stage/files.list"
/usr/bin/tar -C "$original" --null -T "$stage/files.list" --format=pax -cf - |
    /usr/bin/tar -C "$mirror" -xf -
: > "$table.tmp"
trap 'rm -f "$table.tmp"' EXIT

# Only direct providers of the native charger are required here. Their own
# unresolved calls are resolved later by normal external-module modpost.
providers=(
    power-logger:mpower_logger
    power-notify:xcharge_notify_subsys
    power-battery-logger:xcharge_mbattery_logger
    power-thermal:xcharge_thermal
    power-manager:xcharge_chg_manager
    power-interface:xcharge_chg_interface
)
for provider in "${providers[@]}"; do
    group=${provider%%:*}
    name=${provider#*:}
    directory="$mirror/meizu/$group"
    "$top/prebuilts/build-tools/linux-x86/bin/make" -j"$jobs" \
        -C "$top/kernel/meizu/m2391" "O=$out" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
        CC=clang LD=ld.lld CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- \
        "${version_flags[@]}" "M=$directory" "$name.o"
    versions="$directory/$name.o.symversions"
    test -s "$versions"
    # This is the compiler's own genksyms linker script, exactly as consumed
    # by Kbuild. It is never replaced with an expected or stock CRC table.
    "$clang/bin/ld.lld" -r -T "$versions" "$directory/$name.o" \
        -o "$stage/$name.export-object.o"
    module_symvers "$stage/$name.export-object.o" "$name" > "$stage/$name.symvers"
    check_retained_abi "$top/kernel/meizu/m2391/scripts/m2391/abi/$name.ko.symvers" \
        "$stage/$name.symvers"
    cat "$stage/$name.symvers" >> "$table.tmp"
done
mv "$table.tmp" "$table"
