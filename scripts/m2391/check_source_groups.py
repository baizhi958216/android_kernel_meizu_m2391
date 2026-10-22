#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep ABI changes inside complete source groups; inspect retained imports."""
import json
from pathlib import Path
import subprocess
import sys


def main():
    manifest, names_file, blobs, llvm_nm = sys.argv[1:]
    selected = set(Path(names_file).read_text().split())
    protected = {}
    for group in json.loads(Path(manifest).read_text()):
        members = set(group["modules"])
        if not members.intersection(selected):
            continue
        missing = members - selected
        if missing:
            sys.exit("Incomplete source ABI group %s: %s" %
                     (group["name"], ", ".join(sorted(missing))))
        for symbol in group["internal_exports"]:
            protected[symbol] = group["name"]
    if not protected:
        return
    violations = []
    for module in sorted(Path(blobs).rglob("*.ko")):
        # BoardConfigKernel.mk excludes every selected same-name stock blob.
        if module.name in selected:
            continue
        result = subprocess.run([llvm_nm, "--undefined-only", str(module)],
                                text=True, capture_output=True, check=True)
        for line in result.stdout.splitlines():
            fields = line.split()
            if fields and fields[-1] in protected:
                symbol = fields[-1]
                violations.append("%s imports source-group ABI %s (%s)" %
                                  (module, symbol, protected[symbol]))
    if violations:
        sys.exit("\n".join(violations))


if __name__ == "__main__":
    main()
