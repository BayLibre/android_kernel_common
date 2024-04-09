obj-m += pkvm_mod2.o
pkvm_mod2-y := main.o hyp/kvm_nvhe.o

$(obj)/hyp/kvm_nvhe.o: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/hyp $(obj)/hyp/kvm_nvhe.o
