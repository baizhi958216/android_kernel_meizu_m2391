# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_GUNYAH_KALAMA_RM) += gh_dbl.o gh_msgq.o gh_rm_drv.o gh_irq_lend.o gh_mem_notifier.o
gh_rm_drv-y := gh_rm_core.o gh_rm_iface.o irq.o reset.o
obj-$(CONFIG_GUNYAH_KALAMA_VM) += gh_kalama_vm.o gh_ctrl.o
gh_kalama_vm-y := gh_main.o gh_secure_vm_loader.o gh_secure_vm_virtio_backend.o gh_proxy_sched.o
obj-$(CONFIG_GUNYAH_KALAMA_WATCHDOG) += qcom_wdt_core.o gh_virt_wdt.o
