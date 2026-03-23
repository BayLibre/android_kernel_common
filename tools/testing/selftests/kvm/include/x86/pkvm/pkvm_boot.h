/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_PKVM_BOOT_H
#define SELFTEST_PKVM_BOOT_H

#include <stdint.h>

#include <linux/compiler.h>
#include <linux/sizes.h>

/*
 * Layout for AP SIPI boot section (not to scale, real-mode start)
 *
 *                                 GPA
 * ________________________________ 0x0010_0000 (1MB)
 * |      AP SIPI boot code    |
 * |                           |
 * |___________________________|____ 0x000f_f000: PKVM_AP_SIPI_BOOT_GPA
 * |                           |
 * |   Boot parameters         |
 * |                           |
 * |___________________________|____ 0x000f_0000: PKVM_BOOT_PARAMETERS_GPA
 */
#define ONE_MEGABYTE_GPA         (SZ_1M)
#define PKVM_AP_SIPI_BOOT_GPA    (ONE_MEGABYTE_GPA - SZ_4K)
#define PKVM_BOOT_PARAMETERS_GPA (ONE_MEGABYTE_GPA - SZ_64K)

/*
 * Exact memory layout consumed by LGDT/LIDT.
 */
struct __packed pkvm_boot_parameters_dtr {
	uint16_t limit;
	uint32_t base;
};

struct __packed pkvm_boot_parameters_dtr64 {
	uint16_t limit;
	uint64_t base;
};

#define PKVM_BOOT_GDT_ENTRY_COUNT 4

#define PKVM_BOOT_GDT_LONG_CS 1
#define PKVM_BOOT_GDT_DATA32  2
#define PKVM_BOOT_GDT_CODE32  3

/*
 * Per-vCPU boot parameters consumed after AP SIPI enters the pKVM boot stub.
 */
struct pkvm_per_vcpu_parameters {
	uint32_t cr0;
	uint32_t cr3;
	uint32_t cr4;
	uint64_t efer;
	uint64_t guest_code;
	uint32_t guest_stack;
};

/*
 * Boot parameters for pKVM VM_TYPE VM.
 *
 * pVM APs start execution from x86 SIPI state. The boot code reads this structure,
 * uses a temporary low-memory GDT with dedicated 32-bit and 64-bit code
 * descriptors to enter protected mode, enables paging and long mode, reloads
 * the runtime GDT/IDT, and then transfers control to per-vCPU guest_code.
 *
 * This structure is loaded at PKVM_BOOT_PARAMETERS_GPA.
 */
struct pkvm_boot_parameters {
	struct pkvm_boot_parameters_dtr boot_gdtr;
	struct pkvm_boot_parameters_dtr boot_idtr;
	struct pkvm_boot_parameters_dtr64 runtime_gdtr;
	struct pkvm_boot_parameters_dtr64 runtime_idtr;
	uint64_t boot_gdt[PKVM_BOOT_GDT_ENTRY_COUNT];
	uint32_t nr_vcpus;
	struct pkvm_per_vcpu_parameters per_vcpu[];
};

void pkvm_boot(void);
void pkvm_boot_code_end(void);

#define PKVM_BOOT_CODE_SIZE (pkvm_boot_code_end - pkvm_boot)

#endif /* SELFTEST_PKVM_BOOT_H */
