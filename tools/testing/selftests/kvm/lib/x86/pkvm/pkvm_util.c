// SPDX-License-Identifier: GPL-2.0-only

#include <stdint.h>

#include "kvm_util.h"
#include "processor.h"
#include "pkvm/pkvm_boot.h"
#include "pkvm/pkvm_util.h"

/* Arbitrarily selected to avoid overlaps with anything else */
#define PKVM_BOOT_CODE_SLOT     22

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