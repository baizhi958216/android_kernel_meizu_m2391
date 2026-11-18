#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Write the early module load order from this build's depmod graph."""
import sys
from pathlib import Path

# Load storage and USB PHY providers before first-stage init can mount dlkm.
# Firmware links between DT devices do not appear in an ELF dependency graph.
PROVIDERS = """
qcom-scm qcom_hwspinlock smem cmd-db qcom_rpmh rpmh-regulator proxy-consumer
qcom_aoss qcom-ipcc msm_qmp tmecom-intf qcom_ice clk-rpmh tcsrcc-kalama
gcc-kalama qcom-pdc pinctrl-kalama spmi-pmic-arb qcom-spmi-pmic
pinctrl-spmi-gpio gdsc-regulator icc-bcm-voter qnoc-sm8550 gpucc-kalama arm_smmu
phy-qcom-eusb2-repeater phy-snps-eusb2 phy-qcom-qmp-combo
phy-qcom-ufs-qmp-v4-kalama ufs-qcom
""".split()


def name(path):
    return Path(path).name.removesuffix(".ko").replace("-", "_")


def main(directory):
    graph = {}
    by_name = {}
    for line in (directory / "modules.dep").read_text().splitlines():
        module, dependencies = line.split(":", 1)
        graph[module] = dependencies.split()
        key = name(module)
        if key in by_name:
            raise ValueError(f"duplicate installed module name {key}")
        by_name[key] = module

    builtin = {name(p) for p in (directory / "modules.builtin").read_text().splitlines()}
    visited = set()
    visiting = set()
    output = []

    def load(module):
        if module in visiting:
            raise ValueError(f"module dependency cycle at {module}")
        if module in visited:
            return
        visiting.add(module)
        for dependency in graph[module]:
            load(dependency)
        visiting.remove(module)
        visited.add(module)
        output.append(Path(module).name)

    for provider in PROVIDERS:
        key = provider.replace("-", "_")
        if key not in builtin:
            load(by_name[key])
    (directory / "modules.load").write_text("".join(f"{module}\n" for module in output))


if __name__ == "__main__":
    main(Path(sys.argv[1]))
