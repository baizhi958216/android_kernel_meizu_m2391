#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Map this board's signed build to the Android module installation and load lists."""

import argparse
import filecmp
import fnmatch
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile


# The factory modules.load files use downstream driver names. Retain their
# boot phase when the same board hardware or Android ABI moved to another
# upstream driver. The two DSP controls share one driver in this tree.
STOCK_REPLACEMENTS = {
    "adsp_loader_dlkm": "kalama_dsp_loader",
    "altmode_glink": "pmic_glink_altmode",
    "cdsp_loader": "kalama_dsp_loader",
    "dwc3_msm": "dwc3_qcom_legacy",
    "fsa4480_i2c": "fsa4480",
    "i2c_msm_geni": "i2c_qcom_geni",
    "msm_geni_serial": "qcom_geni_serial",
    "phy_msm_snps_eusb2": "phy_snps_eusb2",
    "qnoc_kalama": "qnoc_sm8550",
    "repeater_qti_pmic_eusb2": "phy_qcom_eusb2_repeater",
    "rproc_qcom_common": "qcom_common",
    "smart_zsmalloc": "zsmalloc",
    "smartswap_zram": "zram",
    "spi_msm_geni": "spi_geni_qcom",
}


def key(name):
    return Path(name).name.removesuffix(".ko").replace("-", "_")


def lines(path):
    return [line for raw in path.read_text().splitlines()
            if (line := raw.partition("#")[0].strip())]


def compatibles(path):
    """Read compatible strings from a flattened devicetree or overlay."""
    data = path.read_bytes()
    magic, size, start, strings, _, _, _, _, strsize, size_struct = struct.unpack_from(">10I", data)
    if magic != 0xd00dfeed or size > len(data):
        raise ValueError(f"invalid FDT in {path}")
    strings = data[strings:strings + strsize]
    offset = start
    result = set()
    while offset < start + size_struct:
        token, = struct.unpack_from(">I", data, offset)
        offset += 4
        if token == 1:
            offset = (data.index(0, offset) + 4) & ~3
        elif token == 3:
            length, name = struct.unpack_from(">II", data, offset)
            offset += 8
            if strings[name:strings.index(0, name)] == b"compatible":
                result.update(s.decode() for s in data[offset:offset + length].split(b"\0") if s)
            offset += (length + 3) & ~3
        elif token == 9:
            return result
        elif token not in (2, 4):
            raise ValueError(f"invalid FDT token {token} in {path}")
    raise ValueError(f"missing FDT terminator in {path}")


class Modules:
    def __init__(self, stage, config):
        releases = list((stage / "lib/modules").iterdir())
        if len(releases) != 1 or not re.fullmatch(r"6\.18(?:\.\d+)?(?:[-+][\w.+-]+)?", releases[0].name):
            raise ValueError(f"expected one 6.18 module release in {stage}")
        self.root = releases[0]
        self.files = {}
        self.deps = {}
        self.builtins = {key(path) for path in lines(self.root / "modules.builtin")}
        for line in lines(self.root / "modules.dep"):
            path, deps = line.split(":", 1)
            module = key(path)
            if module in self.files or not re.fullmatch(r"[A-Za-z0-9_+.-]+\.ko", Path(path).name):
                raise ValueError(f"ambiguous module name {path}")
            self.files[module] = self.root / path
            self.deps[module] = sorted(key(dep) for dep in deps.split())
        if set(self.root.rglob("*.ko")) != set(self.files.values()):
            raise ValueError("modules.dep does not describe the complete staged tree")
        self.soft = {m: [] for m in self.files}
        self.absent_softdeps = []
        for line in lines(self.root / "modules.softdep"):
            kind, owner, *deps = line.split()
            if kind == "softdep" and owner in self.files:
                for dep in (key(d) for d in deps if d not in ("pre:", "post:")):
                    if dep in self.files:
                        self.soft[owner].append(dep)
                    elif dep not in self.builtins:
                        self.absent_softdeps.append(f"{owner} -> {dep}")
        board = set()
        for dt in sorted((stage / "dts").glob("*.dtb*")):
            board.update(compatibles(dt))
        board_matches = set()
        for line in lines(self.root / "modules.alias"):
            _, alias, mod = line.split()
            if mod in self.files and alias.startswith("of:") and any(
                    fnmatch.fnmatchcase("of:NnodeTC" + c, alias) for c in board):
                board_matches.add(mod)
        self.missing = {}
        def stock(partition):
            ids = [key(p) for p in lines(config / ("modules.load." + partition))]
            missing = []
            resolved = []
            for name in ids:
                target = name
                if name not in self.files and name not in self.builtins:
                    target = STOCK_REPLACEMENTS.get(name, name)
                if target in self.files:
                    resolved.append(target)
                elif target not in self.builtins:
                    if target != name:
                        raise ValueError(f"{partition} requires {target} for {name}")
                    missing.append(name)
            self.missing[partition] = missing
            return resolved

        first = [key(p) for p in lines(self.root / "modules.load")]
        external = sorted(m for m, p in self.files.items()
                          if p.relative_to(self.root).parts[0] == "updates")
        early = self.ordered(first + stock("vendor_ramdisk"))
        recovery = self.ordered(early + stock("recovery"))
        vendor_load = self.ordered(stock("vendor_dlkm") + external + sorted(board_matches),
                                   hard_deps=False)
        vendor = set(vendor_load)
        system = set(self.files) - vendor
        # The platform adds system modules to vendor depmod, in this direction only.
        while moved := {m for m in system if vendor.intersection(self.deps[m] + self.soft[m])}:
            vendor.update(moved)
            system -= moved
        self.groups = {
            "RAMDISK_MODULES": sorted(self.closure(recovery)),
            "VENDOR_MODULES": sorted(vendor),
            "SYSTEM_MODULES": sorted(system),
            "RAMDISK_LOAD": early,
            "RECOVERY_LOAD": recovery,
            "VENDOR_LOAD": vendor_load,
        }

    def closure(self, modules):
        seen = set(modules)
        todo = list(seen)
        while todo:
            module = todo.pop()
            for dep in self.deps[module] + self.soft[module]:
                if dep not in seen:
                    seen.add(dep)
                    todo.append(dep)
        return seen

    def ordered(self, modules, hard_deps=True):
        # Preserve the product order between independent drivers. libmodprobe
        # handles softdeps including post: entries while processing modules.load.
        visited = set()
        active = set()
        result = []
        def add(m):
            if m in visited:
                return
            if m in active:
                raise ValueError(f"module dependency cycle at {m}")
            active.add(m)
            if hard_deps:
                for d in self.deps[m]:
                    add(d)
            active.remove(m)
            visited.add(m)
            result.append(m)
        for m in modules:
            add(m)
        return result

    def manifest(self):
        rows = ["# SPDX-License-Identifier: GPL-2.0-only",
                "# Generated by scripts/m2391/android-modules.py plan.", ""]
        for group, ids in self.groups.items():
            words = [self.files[m].name for m in ids]
            rows.append(f"M2391_ANDROID_{group} := " + " \\\n    ".join(
                        " ".join(words[i:i + 4]) for i in range(0, len(words), 4)))
        return "\n".join(rows) + "\n"


def replace_file(src, dst):
    if dst.is_file() and filecmp.cmp(src, dst, shallow=False):
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=dst.parent, delete=False) as f:
        tmp = Path(f.name)
    try:
        shutil.copyfile(src, tmp)
        tmp.chmod(0o644)
        os.replace(tmp, dst)
    finally:
        tmp.unlink(missing_ok=True)


def verify(path, release, cert, scratch, openssl):
    data = path.read_bytes()
    marker = b"~Module signature appended~\n"
    if not data.endswith(marker) or data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 183:
        raise ValueError(f"{path.name} is not a signed AArch64 ELF")
    if not re.search(rb"\x00vermagic=" + re.escape(release.encode()) + rb"(?: |\x00)", data):
        raise ValueError(f"wrong vermagic in {path.name} for {release}")
    trailer = len(data) - len(marker) - 12
    length, = struct.unpack_from(">I", data, trailer + 8)
    if not 0 < length < trailer or data[trailer:trailer+8] != bytes((0, 0, 2, 0, 0, 0, 0, 0)):
        raise ValueError(f"invalid PKCS#7 trailer in {path.name}")
    sig, elf = scratch / "signature.der", scratch / "module.elf"
    sig.write_bytes(data[trailer-length:trailer])
    elf.write_bytes(data[:trailer-length])
    p = subprocess.run([openssl, "cms", "-verify", "-binary", "-inform", "DER", "-in", sig,
                        "-content", elf, "-nointern", "-certfile", cert, "-noverify",
                        "-out", os.devnull], capture_output=True)
    if p.returncode:
        raise ValueError(f"module signature mismatch for {path.name}: {p.stderr.decode().strip()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for cmd in ("plan", "stage"):
        p = sub.add_parser(cmd)
        p.add_argument("stage", type=Path)
        p.add_argument("device_module_config", type=Path)
    p.add_argument("manifest", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--openssl", default="openssl")
    args = parser.parse_args()
    modules = Modules(args.stage, args.device_module_config)
    manifest = modules.manifest()
    if args.command == "plan":
        sys.stdout.write(manifest)
        return
    if args.manifest.read_text() != manifest:
        raise ValueError("module graph changed; regenerate android-modules.mk with "
                         f"{sys.argv[0]} plan {args.stage} {args.device_module_config}")
    if modules.absent_softdeps:
        raise ValueError("missing module soft dependencies: " + ", ".join(modules.absent_softdeps))
    with tempfile.TemporaryDirectory() as tmp:
        scratch = Path(tmp)
        cert = scratch / "kernel.pem"
        subprocess.run([args.openssl, "x509", "-inform", "DER", "-in",
                        args.stage / "modules-signing.x509", "-out", cert], check=True)
        for path in modules.files.values():
            verify(path, modules.root.name, cert, scratch, args.openssl)
        report = scratch / "unmatched.txt"
        report.write_text("".join(f"{group}\t{m}\n" for group, missing in modules.missing.items()
                                  for m in missing))
        replace_file(report, args.output / "unmatched-stock-modules.txt")
        for path in modules.files.values():
            replace_file(path, args.output / "modules" / path.name)
        names = {path.name for path in modules.files.values()}
        for stale in (args.output / "modules").glob("*.ko"):
            if stale.name not in names:
                stale.unlink()
        replace_file(args.stage / "Image", args.output / "Image")
        replace_file(args.stage / "dts/m2391.dtb", args.output / "m2391.dtb")


if __name__ == "__main__":
    main()
