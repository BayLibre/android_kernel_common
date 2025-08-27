obj-$(CONFIG_EXYNOS_HVM) += exynos_hvm.o

HVM_DIR ?= ../../../drivers/halla

exynos_hvm-y := $(HVM_DIR)/hvm_main.o $(HVM_DIR)/hvm_vm.o \
          $(HVM_DIR)/hvm_vcpu.o $(HVM_DIR)/hvm_irqfd.o \
          $(HVM_DIR)/hvm_ioeventfd.o $(HVM_DIR)/hvm_mmu.o \
          $(HVM_DIR)/hvm_vtimer.o $(HVM_DIR)/hvm_debug.o
