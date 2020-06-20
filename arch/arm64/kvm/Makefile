# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module
#

<<<<<<< HEAD   (228804 ANDROID: dm-bow: avoid truncating the logical block size)
ccflags-y += -I $(srctree)/$(src) -I $(srctree)/virt/kvm/arm/vgic
CFLAGS_REMOVE_debug.o += $(CC_FLAGS_CFI)
=======
ccflags-y += -I $(srctree)/$(src)
>>>>>>> BRANCH (039aeb Merge tag 'for-linus' of git://git.kernel.org/pub/scm/virt/k)

KVM=../../../virt/kvm

obj-$(CONFIG_KVM) += kvm.o
obj-$(CONFIG_KVM) += hyp/

kvm-y := $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o $(KVM)/eventfd.o \
	 $(KVM)/vfio.o $(KVM)/irqchip.o \
	 arm.o mmu.o mmio.o psci.o perf.o hypercalls.o pvtime.o \
	 inject_fault.o regmap.o va_layout.o hyp.o hyp-init.o handle_exit.o \
	 guest.o debug.o reset.o sys_regs.o sys_regs_generic_v8.o \
	 vgic-sys-reg-v3.o fpsimd.o pmu.o \
	 aarch32.o arch_timer.o \
	 vgic/vgic.o vgic/vgic-init.o \
	 vgic/vgic-irqfd.o vgic/vgic-v2.o \
	 vgic/vgic-v3.o vgic/vgic-v4.o \
	 vgic/vgic-mmio.o vgic/vgic-mmio-v2.o \
	 vgic/vgic-mmio-v3.o vgic/vgic-kvm-device.o \
	 vgic/vgic-its.o vgic/vgic-debug.o

kvm-$(CONFIG_KVM_ARM_PMU)  += pmu-emul.o
