#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Check compiler-produced exports of an in-tree replacement before packaging.
set -euo pipefail
source "$(dirname "$0")/build_external.sh"
expected=$1
module=$2
READELF=$3
actual=$(mktemp)
trap 'rm -f "$actual"' EXIT
module_symvers "$module" > "$actual"
check_retained_abi "$expected" "$actual"
