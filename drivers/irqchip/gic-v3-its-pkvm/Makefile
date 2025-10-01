# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_ARM_GIC_V3_ITS_PKVM) += gic_v3_its_pkvm.o

gic_v3_its_pkvm-y := gic-v3-its-pkvm.o

ifeq ($(CONFIG_ARM_GIC_V3_ITS_PKVM),m)
$(obj)/hyp/kvm_nvhe.o: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/hyp $(obj)/hyp/kvm_nvhe.o

gic_v3_its_pkvm-y += hyp/kvm_nvhe.o
endif
