// SPDX-License-Identifier: GPL-2.0
#define COMPILE_OFFSETS

#include <linux/kbuild.h>

#include "pkvm/pkvm_boot.h"

static void __attribute__((used)) common(void)
{
	OFFSET(PKVM_BOOT_PARAMETERS_BOOT_GDT, pkvm_boot_parameters, boot_gdtr);
	OFFSET(PKVM_BOOT_PARAMETERS_BOOT_IDT, pkvm_boot_parameters, boot_idtr);
	OFFSET(PKVM_BOOT_PARAMETERS_RUNTIME_GDT, pkvm_boot_parameters,
	       runtime_gdtr);
	OFFSET(PKVM_BOOT_PARAMETERS_RUNTIME_IDT, pkvm_boot_parameters,
	       runtime_idtr);
	OFFSET(PKVM_BOOT_PARAMETERS_NR_VCPUS, pkvm_boot_parameters, nr_vcpus);
	OFFSET(PKVM_BOOT_PARAMETERS_PER_VCPU, pkvm_boot_parameters, per_vcpu);

	OFFSET(PKVM_PER_VCPU_PARAMETERS_CR0, pkvm_per_vcpu_parameters, cr0);
	OFFSET(PKVM_PER_VCPU_PARAMETERS_CR3, pkvm_per_vcpu_parameters, cr3);
	OFFSET(PKVM_PER_VCPU_PARAMETERS_CR4, pkvm_per_vcpu_parameters, cr4);
	OFFSET(PKVM_PER_VCPU_PARAMETERS_EFER, pkvm_per_vcpu_parameters, efer);
	OFFSET(PKVM_PER_VCPU_PARAMETERS_GUEST_CODE, pkvm_per_vcpu_parameters,
	       guest_code);
	OFFSET(PKVM_PER_VCPU_PARAMETERS_GUEST_STACK, pkvm_per_vcpu_parameters,
	       guest_stack);

	DEFINE(SIZEOF_PKVM_PER_VCPU_PARAMETERS,
	       sizeof(struct pkvm_per_vcpu_parameters));
}