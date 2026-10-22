#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

# Emit KBUILD_EXTRA_SYMBOLS rows using the LLVM tools already in the toolchain.
module_symvers() {
    local module=$1 fallback
    fallback=${2:-$(basename "$module" .ko)}
    "$READELF" --elf-output-style=GNU --wide --sections --symbols \
        --string-dump=.modinfo --string-dump=__ksymtab_strings "$module" 2>/dev/null |
        awk -v fallback="$fallback" '
        function hex(s,    n,i,c) {
            n=0; s=tolower(s)
            for (i=1;i<=length(s);i++) {
                c=index("0123456789abcdef",substr(s,i,1))-1
                n=n*16+c
            }
            return n
        }
        /^String dump of section/ {
            mode=($0 ~ /\.modinfo/ ? "info" : "strings"); next
        }
        /^[[:space:]]*\[[[:space:]]*[0-9a-fA-F]+\]/ {
            line=$0; sub(/^[[:space:]]*\[[[:space:]]*/,"",line)
            bracket=index(line,"]"); offset=substr(line,1,bracket-1)
            text=substr(line,bracket+1); sub(/^[[:space:]]+/,"",text)
            if (mode=="info") {
                if (text ~ /^name=/) provider=substr(text,6)
            } else if (mode=="strings") {
                start=hex(offset)
                for (i=1;i<=length(text);i++) chars[start+i-1]=substr(text,i,1)
                chars[start+length(text)]=""
            } else {
                split(text,fields,/[[:space:]]+/); sections[offset+0]=fields[1]
            }
            next
        }
        $1 ~ /^[0-9]+:$/ && NF>=8 && $4!="SECTION" {
            name=$8
            if (name ~ /^__ksymtab_/) {
                name=substr(name,11); exports[name]=1
                kind[name]=(sections[$7] ~ /gpl/ ? "EXPORT_SYMBOL_GPL" : "EXPORT_SYMBOL")
            } else if (name ~ /^__crc_/) {
                value=tolower($2); sub(/^0+/,"",value)
                crc[substr(name,7)]=substr("00000000",1,8-length(value)) value
            } else if (name ~ /^__kstrtabns_/) namespace[substr(name,13)]=hex($2)
        }
        END {
            if (provider=="") provider=fallback
            for (name in exports) {
                ns=""; pos=namespace[name]
                if (name in namespace) while ((pos in chars) && chars[pos]!="") ns=ns chars[pos++]
                printf "0x%s\t%s\tm2391-dependencies/%s\t%s\t%s\n",crc[name],name,provider,kind[name],ns
            }
        }'
}

# Retained OEM camera/display/video modules import these interfaces. Compare
# compiler-generated CRCs; never rewrite a module's version metadata to match.
check_retained_abi() {
    local expected=$1 actual=$2
    if [[ ! -s "$expected" ]]; then
        echo "Empty retained ABI snapshot: $expected" >&2
        return 1
    fi
    awk '
        FNR==NR { expected[$2]=$1 SUBSEP $4 SUBSEP $5; next }
        $2 in expected {
            seen[$2]=1
            if (expected[$2] != $1 SUBSEP $4 SUBSEP $5) {
                print "Retained OEM ABI mismatch: " $2 > "/dev/stderr"
                failed=1
            }
        }
        END {
            for (symbol in expected) if (!(symbol in seen)) {
                print "Missing retained OEM export: " symbol > "/dev/stderr"
                failed=1
            }
            exit failed
        }' "$expected" "$actual"
}

# Some recovered source interfaces preserve behavior and machine layout while
# their unavailable original type names prevent retaining a truthful old CRC.
# Never let a retained binary acquire one of those source-group interfaces.
check_private_abi_consumers() {
    local private=$1 top=$2 plan=$3 selected module name
    selected=" $(awk -F '\t' '{ n=split($3,a," "); for(i=1;i<=n;i++) { split(a[i],b,":"); printf "%s ",b[1] } }' "$plan")"
    while IFS= read -r -d '' module; do
        name=$(basename "$module")
        case "$selected" in *" $name "*) continue ;; esac
        "$READELF" --elf-output-style=GNU --wide --symbols "$module" |
            awk -v consumer="$module" '
            FNR==NR { if (NF && $1 !~ /^#/) private[$1]=1; next }
            $7=="UND" && $8 in private {
                print "Retained stock consumer of source-private ABI: " consumer ": " $8 > "/dev/stderr"
                failed=1
            }
            END { exit failed }' "$private" - || return 1
    done < <(find "$top/vendor/meizu/m2391/proprietary" -name '*.ko' -type f -print0)
}

main() {
    local top=$1 out=$2 jobs=$3 original mirror source clang symbols plan
    # Use the same ACK version arguments as the core kernel build.
    local -a version_flags=("${@:4}")
    top=$(cd "$top" && pwd)
    mkdir -p "$out"
    out=$(cd "$out" && pwd)
    cd "$top"
    original="$top/kernel/meizu/m2391-modules"
    mirror="$out/m2391-external-src/m2391-modules"
    source="$mirror/qcom/opensource"
    clang="${CLANG_PATH:-$(dirname "$(command -v clang)")}"
    clang=$(cd "$clang" && pwd)
    READELF="$clang/bin/llvm-readelf"
    plan="$out/m2391-external-plan.tsv"
    symbols="$out/m2391-external-symbols"
    export PATH="$clang/bin:$top/prebuilts/kernel-build-tools/linux-x86/bin:$top/prebuilts/build-tools/linux-x86/bin:$PATH"
    export LD_LIBRARY_PATH="$clang/lib64:${LD_LIBRARY_PATH:-}"
    unset ANDROID_BUILD_TOP

    # Keep Kbuild writes under out while including tracked worktree edits.
    mkdir -p "$mirror" "$symbols/stock" "$symbols/built"
    git --no-optional-locks -C "$original" ls-files --cached --others --exclude-standard -z > "$out/m2391-external-files.list"
    # Android's sanitized PATH may resolve tar to Toybox, which lacks --format.
    # Use the host tar for the NUL-delimited list and pax archive on both sides.
    /usr/bin/tar -C "$original" --null -T "$out/m2391-external-files.list" --format=pax -cf - |
        /usr/bin/tar -C "$mirror" -xf -
    # Symbol metadata must describe this build, including removed OEM modules.
    rm -f "$symbols/stock/"*.symvers "$symbols/built/"*.symvers
    local module name seen=" "
    while IFS= read -r -d '' module; do
        name=$(basename "$module")
        case "$seen" in *" $name "*) continue ;; esac
        seen="$seen$name "
        module_symvers "$module" > "$symbols/stock/$name.symvers"
    done < <(find "$top/vendor/meizu/m2391/proprietary" -name '*.ko' -type f -print0)

    local -a flags=(
        CONFIG_CNSS_OUT_OF_TREE=y CONFIG_CNSS2=m CONFIG_CNSS2_QMI=y
        CONFIG_CNSS_QMI_SVC=m CONFIG_CNSS_PLAT_IPC_QMI_SVC=m CONFIG_CNSS_GENL=m
        CONFIG_WCNSS_MEM_PRE_ALLOC=m CONFIG_CNSS_UTILS=m CONFIG_CNSS_HW_SECURE_DISABLE=y
        "EVA_ROOT=$source/eva-kernel" "VIDEO_ROOT=$source/video-driver"
        "DATAIPA_ROOT=$source/dataipa" "WLAN_PLATFORM_ROOT=$source/wlan/platform"
        "SSG_MODULE_ROOT=$source/securemsm-kernel" "MSM_EXT_DISPLAY_ROOT=$source/mm-drivers"
        "MSM_HW_FENCE_ROOT=$source/mm-drivers/" "MMRM_ROOT=$source/mmrm-driver"
        "SYNC_FENCE_ROOT=$source/mm-drivers/"
        "RMNET_CORE_ROOT=$source/datarmnet/core" "RMNET_CORE_INC_DIR=$source/datarmnet/core"
        "BT_ROOT=$source/bt-kernel" "AUDIO_ROOT=$source/audio-kernel"
        MODNAME=audio_dlkm BOARD_PLATFORM=kalama CONFIG_MSM_BT_POWER=m
        CONFIG_BTFM_SLIM=m CONFIG_SLIM_BTFM_CODEC=n CONFIG_I2C_RTC6226_QCA=m
    )
    local group excluded selected table symvers relative
    local -a tables groupflags
    shopt -s nullglob
    while IFS=$'\t' read -r group excluded selected; do
        tables=()
        for table in "$symbols/stock/"*.symvers; do
            [[ -f "$symbols/built/$(basename "$table")" ]] || tables+=("$table")
        done
        tables+=("$symbols/built/"*.symvers)
        symvers="$out/m2391-retained-${group//\//-}.symvers"
        # Exclude the current group and symbols already provided by the core.
        awk -v excluded=" $excluded " '
            FILENAME==ARGV[1] { core[$2]=1; next }
            {
                file=FILENAME; sub(/^.*\//,"",file); sub(/\.symvers$/,"",file)
                if (!index(excluded," " file " ") && !($2 in core)) print
            }' "$out/Module.symvers" "${tables[@]}" > "$symvers"
        groupflags=("${flags[@]}")
        if [[ "$group" == audio-kernel ]]; then
            groupflags+=(CONFIG_M2391_AUDIO_MACHINE_RECOVERY=y)
        fi
        if [[ "$group" == ../../nxp/opensource/driver ]]; then
            groupflags+=("NFC_ROOT=$source/$group")
        fi
        if [[ "$group" == wlan/qcacld-3.0 ]]; then
            groupflags+=("WLAN_ROOT=$source/$group" WLAN_COMMON_ROOT=cmn
                "WLAN_COMMON_INC=$source/wlan/qca-wifi-host-cmn" "WLAN_FW_API=$source/wlan/fw-api"
                WLAN_PROFILE=kiwi_v2 DYNAMIC_SINGLE_CHIP=kiwi_v2 DEVNAME=kiwi_v2
                WLAN_CTRL_NAME=wlan CONFIG_QCA_CLD_WLAN=m CONFIG_BUS_AUTO_SUSPEND=y MODNAME=kiwi_v2)
        fi
        if [[ "$group" == touch-drivers ]]; then
            groupflags+=("TOUCH_ROOT=$source/touch-drivers" CONFIG_ARCH_KALAMA=y
                CONFIG_MSM_TOUCH=m CONFIG_TOUCHSCREEN_GOODIX_BRL=y CONFIG_QTS_ENABLE=y
                CONFIG_TOUCHSCREEN_GOODIX_BRL_MEIZU=y CONFIG_TOUCHSCREEN_NT36XXX_I2C=y
                CONFIG_TOUCHSCREEN_ATMEL_MXT=n CONFIG_TOUCHSCREEN_DUMMY=n)
        fi
        if [[ "$group" == graphics-kernel ]]; then
            groupflags+=("KGSL_PATH=$source/graphics-kernel" "KERNEL_SRC=$top/kernel/meizu/m2391"
                CONFIG_ARCH_KALAMA=y CONFIG_QCOM_KGSL=m MODNAME=msm_kgsl
                CONFIG_QCOM_KGSL_MEIZU_DIAGNOSTICS=y CONFIG_QCOM_KGSL_MEIZU_THERMAL_LOG=y)
        fi
        if [[ "$group" == camera-kernel ]]; then
            groupflags+=("CAMERA_KERNEL_ROOT=$source/camera-kernel"
                "KERNEL_ROOT=$top/kernel/meizu/m2391" "KERNEL_SRC=$top/kernel/meizu/m2391"
                CONFIG_ARCH_KALAMA=y CONFIG_SPECTRA_CAMERA=m CONFIG_INTERCONNECT_QCOM=y
                MODNAME=camera)
        fi
        if [[ "$group" == display-drivers ]]; then
            groupflags+=("DISPLAY_ROOT=$source/display-drivers"
                "KERNEL_ROOT=$top/kernel/meizu/m2391" "KERNEL_SRC=$top/kernel/meizu/m2391"
                CONFIG_ARCH_KALAMA=y CONFIG_DRM_MSM=m MODNAME=msm_drm)
        fi
        for relative in $selected; do
            name=${relative%%:*}
            if [[ -f "$top/kernel/meizu/m2391/scripts/m2391/abi/$name.private-symbols" ]]; then
                check_private_abi_consumers \
                    "$top/kernel/meizu/m2391/scripts/m2391/abi/$name.private-symbols" "$top" "$plan"
            fi
        done
        echo "Building m2391 external group: $group"
        "$top/prebuilts/build-tools/linux-x86/bin/make" -j"$jobs" \
            -C "$top/kernel/meizu/m2391" "O=$out" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
            "${version_flags[@]}" \
            CC=clang LD=ld.lld CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- \
            "M=$source/$group" "KBUILD_EXTRA_SYMBOLS=$symvers" "${groupflags[@]}" modules
        for relative in $selected; do
            name=${relative%%:*}
            module="$source/${relative#*:}"
            if [[ ! -f "$module" ]]; then
                echo "Expected source module missing: $module" >&2
                return 1
            fi
            module_symvers "$module" > "$symbols/built/$name.symvers"
            if [[ -f "$top/kernel/meizu/m2391/scripts/m2391/abi/$name.symvers" ]]; then
                check_retained_abi "$top/kernel/meizu/m2391/scripts/m2391/abi/$name.symvers" \
                    "$symbols/built/$name.symvers"
            fi
        done
    done < "$plan"

    mkdir -p "$out/m2391-modules"
    while IFS=$'\t' read -r group excluded selected; do
        for relative in $selected; do
            module="$out/m2391-modules/${relative%%:*}"
            "$clang/bin/llvm-strip" --strip-debug -o "$module" "$source/${relative#*:}"
            "$out/scripts/sign-file" sha1 "$out/certs/signing_key.pem" "$out/certs/signing_key.x509" "$module"
        done
    done < "$plan"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
