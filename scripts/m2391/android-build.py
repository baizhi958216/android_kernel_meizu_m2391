#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build the board kernel once for all Android images sharing KERNEL_OBJ."""
import fcntl
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main(obj, modules, device_config, mkdtboimg, depmod, make, *flags):
    scripts = Path(__file__).resolve().parent
    obj, modules, device_config, mkdtboimg, depmod, make = (
        Path(p).resolve() for p in (obj, modules, device_config, mkdtboimg, depmod, make))
    output = obj / "android"
    output.mkdir(parents=True, exist_ok=True)
    with (output / "build.lock").open("a") as lock, tempfile.TemporaryDirectory(dir=output) as tmp:
        fcntl.flock(lock, fcntl.LOCK_EX)
        scratch = Path(tmp)
        # Retain the platform's host compiler/sysroot flags, including arguments
        # containing spaces, for every kernel and external-module make call.
        launcher = scratch / "make"
        command = [str(make)] + [flag for flag in flags if not flag.startswith("-j")]
        launcher.write_text("#!/bin/sh\nexec " + shlex.join(command) + ' "$@"\n')
        launcher.chmod(0o755)
        (scratch / "depmod").symlink_to(depmod)
        env = dict(os.environ, LOCALVERSION="", PATH=str(scratch) + os.pathsep + os.environ["PATH"])
        stage = scratch / "stage"
        subprocess.run([scripts / "build-system.sh", obj, modules, stage], env=env, check=True)
        manifest = scripts / "android-modules.mk"
        plan = subprocess.check_output([sys.executable, scripts / "android-modules.py",
                                        "plan", stage, device_config])
        if plan != manifest.read_bytes():
            next_manifest = output / "android-modules.next.mk"
            next_manifest.write_bytes(plan)
            sys.exit(f"module graph changed; review {next_manifest} and update {manifest}")
        dtbo = scratch / "dtbo.img"
        subprocess.run([mkdtboimg, "cfg_create", dtbo, stage / "dts/dtbo.cfg",
                        "-d", obj / "arch/arm64/boot/dts"], check=True)
        subprocess.run([sys.executable, scripts / "android-modules.py", "stage", stage,
                        device_config, manifest, output], env=env, check=True)
        dtbo.replace(output / "dtbo.img")
        (output / "kernel.stamp").touch()


if __name__ == "__main__":
    main(*sys.argv[1:])
