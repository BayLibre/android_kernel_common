/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTESTS_PKVM_UTIL_H
#define SELFTESTS_PKVM_UTIL_H

#include "kvm_util.h"

static inline bool is_pkvm_protected_vm(struct kvm_vm *vm)
{
	return vm->type == KVM_X86_PKVM_PROTECTED_VM;
}

void vm_pkvm_setup_boot_code_region(struct kvm_vm *vm);
void vm_pkvm_setup_boot_parameters_region(struct kvm_vm *vm,
					  uint32_t nr_runnable_vcpus);
void vm_pkvm_load_common_boot_parameters(struct kvm_vm *vm, uint32_t nr_vcpus);
void vm_pkvm_load_vcpu_boot_parameters(struct kvm_vm *vm,
				      struct kvm_vcpu *vcpu);
void vm_pkvm_set_vcpu_entry_point(struct kvm_vcpu *vcpu, void *guest_code);

#endif /* SELFTESTS_PKVM_UTIL_H */