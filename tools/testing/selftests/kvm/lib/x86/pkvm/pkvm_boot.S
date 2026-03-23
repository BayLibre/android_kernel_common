/* SPDX-License-Identifier: GPL-2.0-only */

#include "pkvm/pkvm_boot_asm.h"
#include "pkvm/pkvm_boot_offsets.h"

#define MSR_IA32_EFER     0xC0000080
#define MSR_IA32_APICBASE 0x0000001b
#define X2APIC_APICID_MSR 0x00000802
#define APICBASE_X2APIC   (1 << 10)
#define APICBASE_ENABLE   (1 << 11)

.code16

.globl pkvm_boot
pkvm_boot:
	/* In this procedure, ebx/edi are used as temporary registers. */
	cli

	/* Read boot parameters from fixed GPA below 1MB. */
	movw $(PKVM_BOOT_PARAMETERS_GPA >> 4), %ax
	movw %ax, %ds

	/*
	 * pKVM enters the AP trampoline with x2APIC enabled. Read the x2APIC
	 * APIC ID MSR, validate it against nr_vcpus, and index per-vCPU boot
	 * parameters directly.
	 */
	movl $MSR_IA32_APICBASE, %ecx
	rdmsr
	movl %eax, %edi
	andl $(APICBASE_X2APIC | APICBASE_ENABLE), %edi
	cmpl $(APICBASE_X2APIC | APICBASE_ENABLE), %edi
	jne .Lpkvm_boot_hang

	movl $X2APIC_APICID_MSR, %ecx
	rdmsr
	movl %eax, %esi

	movl PKVM_BOOT_PARAMETERS_NR_VCPUS, %ecx
	cmpl %ecx, %esi
	jae .Lpkvm_boot_hang

	imull $SIZEOF_PKVM_PER_VCPU_PARAMETERS, %esi, %ebx
	addl $PKVM_BOOT_PARAMETERS_PER_VCPU, %ebx
	jmp .Lpkvm_vcpu_found

.Lpkvm_boot_hang:
	hlt
	jmp .Lpkvm_boot_hang

.Lpkvm_vcpu_found:

	/* Load temporary descriptor tables before leaving real mode. */
	lgdtl PKVM_BOOT_PARAMETERS_BOOT_GDT
	lidtl PKVM_BOOT_PARAMETERS_BOOT_IDT

	/* Enter protected mode with PE set and PG cleared. */
	movl PKVM_PER_VCPU_PARAMETERS_CR0(%ebx), %edi
	andl $0x7fffffff, %edi
	orl $0x1, %edi
	movl %edi, %cr0
	ljmpl $(PKVM_BOOT32_CS), $(PKVM_AP_SIPI_BOOT_GPA + (pkvm_boot_pm - pkvm_boot))

.code32
pkvm_boot_pm:
	/* Switch to flat 32-bit data segments before dereferencing boot params. */
	addl $PKVM_BOOT_PARAMETERS_GPA, %ebx
	movw $(PKVM_BOOT32_DS), %di
	movw %di, %ds
	movw %di, %es
	movw %di, %ss

	/*
	 * Configure control registers and EFER using values passed in
	 * struct pkvm_per_vcpu_parameters.
	 */
	movl PKVM_PER_VCPU_PARAMETERS_CR4(%ebx), %edi
	movl %edi, %cr4
	movl PKVM_PER_VCPU_PARAMETERS_CR3(%ebx), %edi
	movl %edi, %cr3

	movl $MSR_IA32_EFER, %ecx
	movl PKVM_PER_VCPU_PARAMETERS_EFER(%ebx), %eax
	movl PKVM_PER_VCPU_PARAMETERS_EFER + 4(%ebx), %edx
	wrmsr

	/*
	 * Runtime descriptor tables are identity-mapped; preload them in
	 * protected mode before entering long mode.
	 */
	movl $PKVM_BOOT_PARAMETERS_GPA, %eax
	lgdtl PKVM_BOOT_PARAMETERS_RUNTIME_GDT(%eax)
	lidtl PKVM_BOOT_PARAMETERS_RUNTIME_IDT(%eax)

	/*
	 * Guest stack is a 32-bit boot parameter, set ESP before switching
	 * to long mode.
	 */
	movl PKVM_PER_VCPU_PARAMETERS_GUEST_STACK(%ebx), %esp

	movl PKVM_PER_VCPU_PARAMETERS_CR0(%ebx), %edi
	movl %edi, %cr0

	/*
	 * Switch to 64-bit mode using the boot GDT's long-mode code segment,
	 * then reload the runtime descriptor tables once in long mode.
	 */
	ljmpl $(PKVM_BOOT64_CS), $(PKVM_AP_SIPI_BOOT_GPA + (pkvm_boot_lm - pkvm_boot))

.code64
pkvm_boot_lm:
	jmp *PKVM_PER_VCPU_PARAMETERS_GUEST_CODE(%rbx)

/* Leave marker so size of pkvm_boot code can be computed. */
.globl pkvm_boot_code_end
pkvm_boot_code_end:

/* Disable executable stack. */
.section .note.GNU-stack,"",%progbits