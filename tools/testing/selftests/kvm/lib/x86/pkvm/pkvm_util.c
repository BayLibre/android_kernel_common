// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "kvm_util.h"
#include "processor.h"
#include "pkvm/pkvm_boot.h"
#include "pkvm/pkvm_util.h"

/* Arbitrarily selected to avoid overlaps with anything else */
#define PKVM_BOOT_CODE_SLOT     22
#define PKVM_BOOT_PARAMETERS_SLOT 23

#define PKVM_GDT_ENTRY(flags) (((uint64_t)(flags) << 40) | 0x0000ffffULL)

/*
 * Descriptor fields encoded as [flags|limit_hi|access]:
 * 0x00cf9b: 32-bit code  (G=1, DB=1, L=0, Access=0x9b)
 * 0x00cf93: 32-bit data  (G=1, DB=1, L=0, Access=0x93)
 * 0x00af9b: 64-bit code  (G=1, DB=0, L=1, Access=0x9b)
 */
#define PKVM_BOOT_GDT_DESC_CODE32 0x00cf9b
#define PKVM_BOOT_GDT_DESC_DATA32 0x00cf93
#define PKVM_BOOT_GDT_DESC_CODE64 0x00af9b

void vm_pkvm_setup_boot_code_region(struct kvm_vm *vm)
{
	vm_paddr_t boot_code_gpa = PKVM_AP_SIPI_BOOT_GPA;
	size_t nr_pages = DIV_ROUND_UP(PKVM_BOOT_CODE_SIZE, PAGE_SIZE);
	vm_paddr_t gpa;
	uint8_t *hva;

	/* APs start from SIPI in real mode, use memory below 1MB for boot flow. */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    boot_code_gpa,
				    PKVM_BOOT_CODE_SLOT, nr_pages, 0);

	gpa = vm_phy_pages_alloc(vm, nr_pages, boot_code_gpa, PKVM_BOOT_CODE_SLOT);
	TEST_ASSERT(gpa == boot_code_gpa, "Failed vm_phy_pages_alloc\n");

	/* Identity map the SIPI target region for AP real-mode boot transition. */
	virt_map(vm, boot_code_gpa, boot_code_gpa, nr_pages);
	hva = addr_gpa2hva(vm, boot_code_gpa);
	TEST_ASSERT(PKVM_BOOT_CODE_SIZE <= nr_pages * PAGE_SIZE,
		    "pKVM boot code size %lu exceeds %llu-byte SIPI region",
		    PKVM_BOOT_CODE_SIZE, nr_pages * PAGE_SIZE);
	memcpy(hva, pkvm_boot, PKVM_BOOT_CODE_SIZE);
}

void vm_pkvm_setup_boot_parameters_region(struct kvm_vm *vm,
					 uint32_t nr_runnable_vcpus)
{
	size_t boot_params_size =
		sizeof(struct pkvm_boot_parameters) +
		nr_runnable_vcpus * sizeof(struct pkvm_per_vcpu_parameters);
	int npages = DIV_ROUND_UP(boot_params_size, PAGE_SIZE);
	vm_paddr_t gpa;

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    PKVM_BOOT_PARAMETERS_GPA,
				    PKVM_BOOT_PARAMETERS_SLOT, npages, 0);
	gpa = vm_phy_pages_alloc(vm, npages, PKVM_BOOT_PARAMETERS_GPA,
				 PKVM_BOOT_PARAMETERS_SLOT);
	TEST_ASSERT(gpa == PKVM_BOOT_PARAMETERS_GPA,
		    "Failed vm_phy_pages_alloc\n");

	virt_map(vm, PKVM_BOOT_PARAMETERS_GPA, PKVM_BOOT_PARAMETERS_GPA, npages);
}

void vm_pkvm_load_common_boot_parameters(struct kvm_vm *vm, uint32_t nr_vcpus)
{
	struct pkvm_boot_parameters *params =
		addr_gpa2hva(vm, PKVM_BOOT_PARAMETERS_GPA);

	TEST_ASSERT_EQ(vm->mode, VM_MODE_PXXV48_4K);

	params->boot_gdtr.base =
		PKVM_BOOT_PARAMETERS_GPA + offsetof(struct pkvm_boot_parameters, boot_gdt);
	params->boot_gdtr.limit = sizeof(params->boot_gdt) - 1;
	params->boot_idtr.base = 0;
	params->boot_idtr.limit = 0;
	params->boot_gdt[PKVM_BOOT_GDT_LONG_CS] =
		PKVM_GDT_ENTRY(PKVM_BOOT_GDT_DESC_CODE64);
	params->boot_gdt[PKVM_BOOT_GDT_DATA32] =
		PKVM_GDT_ENTRY(PKVM_BOOT_GDT_DESC_DATA32);
	params->boot_gdt[PKVM_BOOT_GDT_CODE32] =
		PKVM_GDT_ENTRY(PKVM_BOOT_GDT_DESC_CODE32);
	params->nr_vcpus = nr_vcpus;

	params->runtime_idtr.base = vm->arch.idt;
	params->runtime_idtr.limit = kvm_get_default_idt_limit();
	params->runtime_gdtr.base = vm->arch.gdt;
	params->runtime_gdtr.limit = kvm_get_default_gdt_limit();

	TEST_ASSERT(params->boot_gdtr.base != 0,
		    "boot gdt base address should not be 0");
	TEST_ASSERT(params->runtime_gdtr.base != 0,
		    "runtime gdt base address should not be 0");
	TEST_ASSERT(params->runtime_idtr.base != 0,
		    "runtime idt base address should not be 0");
}

void vm_pkvm_load_vcpu_boot_parameters(struct kvm_vm *vm, struct kvm_vcpu *vcpu)
{
	struct pkvm_boot_parameters *params =
		addr_gpa2hva(vm, PKVM_BOOT_PARAMETERS_GPA);
	struct pkvm_per_vcpu_parameters *vcpu_params = &params->per_vcpu[vcpu->id];

	/* Transition state for protected mode and long mode entry. */
	vcpu_params->cr0 = kvm_get_default_cr0();
	vcpu_params->cr3 = vm->pgd;
	vcpu_params->cr4 = kvm_get_default_cr4();
	vcpu_params->efer = kvm_get_default_efer();
	vcpu_params->guest_stack = kvm_allocate_vcpu_stack(vm);

	TEST_ASSERT(vcpu_params->cr0 != 0, "cr0 should not be 0");
	TEST_ASSERT(vcpu_params->cr3 != 0, "cr3 should not be 0");
	TEST_ASSERT(vcpu_params->cr4 != 0, "cr4 should not be 0");
	TEST_ASSERT(vcpu_params->efer != 0, "efer should not be 0");
}

void vm_pkvm_set_vcpu_entry_point(struct kvm_vcpu *vcpu, void *guest_code)
{
	struct pkvm_boot_parameters *params =
		addr_gpa2hva(vcpu->vm, PKVM_BOOT_PARAMETERS_GPA);
	struct pkvm_per_vcpu_parameters *vcpu_params = &params->per_vcpu[vcpu->id];

	vcpu_params->guest_code = (uint64_t)guest_code;
}