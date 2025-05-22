// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Google
 */

#include <asm/kvm_pkvm.h>

#include <pkvm.h>
#include "pkvm_hyp.h"
#include "gfp.h"
#include "debug.h"
#include "init_finalise.h"
#include <vmx/vmx.h>
#include <pkvm/vmx/vmx.h>
#include <pkvm/pkvm.h>

static inline void __repriv_restore_selectors(void)
{
	static u16 guest_ds, guest_es, guest_fs, guest_gs, guest_ss;
	static u64 guest_fsbase, guest_gsbase;

	guest_fsbase = vmcs_readl(GUEST_FS_BASE);
	guest_gsbase = vmcs_readl(GUEST_GS_BASE);

	guest_ds = vmcs_read16(GUEST_DS_SELECTOR);
	guest_es = vmcs_read16(GUEST_ES_SELECTOR);
	guest_fs = vmcs_read16(GUEST_FS_SELECTOR);
	guest_gs = vmcs_read16(GUEST_GS_SELECTOR);
	guest_ss = vmcs_read16(GUEST_SS_SELECTOR);

	asm volatile (
		"mov %0, %%ds\n"
		"mov %1, %%es\n"
		"mov %2, %%fs\n"
		"mov %3, %%gs\n"
		"mov %4, %%ss\n"

		:
		: "m"(guest_ds), "m"(guest_es),
		  "m"(guest_fs), "m"(guest_gs), "m"(guest_ss)
		: "memory"
	);

	wrmsrl(MSR_FS_BASE, guest_fsbase);
	wrmsrl(MSR_GS_BASE, guest_gsbase);
}

static void __repriv_restore_special_regs(u64 guest_cr0, u64 guest_cr3, u64 guest_cr4,
		struct desc_ptr *gdt, struct desc_ptr *idt)
{
	struct desc_struct *guest_gdt;
	tss_desc *tss;

	asm volatile (
		"lgdt %0\n"
		"lidt %1\n"

		:
		: "m"(*gdt), "m"(*idt)
		: "memory"
	);

	/*
	 * Reset the busy bit to reload TR
	 */
	guest_gdt = (struct desc_struct *)(gdt->address);
	tss = (tss_desc *)&guest_gdt[GDT_ENTRY_TSS];
	tss->type = DESC_TSS;
	asm volatile("ltr %w0"::"q" (GDT_ENTRY_TSS*8));

	__pkvm_write_cr0(guest_cr0);
	__pkvm_write_cr4(guest_cr4);
	__pkvm_write_cr3(guest_cr3);
}

/*
 * Restores host cpu state and returns to host with vmx off
 */
void pkvm_repriv_restore_cpu(unsigned long *vcpu_regs)
{
	/*
	 * We manipulate SP in assembly. So don't use
	 * stack for the variables.
	 */
	static u64 guest_rip, guest_rflags;
	static u64 guest_cr0, guest_cr3, guest_cr4;
	static struct desc_ptr gdt, idt;
	static u16 guest_cs, guest_ss;

	native_irq_disable();

	gdt.address = vmcs_readl(GUEST_GDTR_BASE);
	gdt.size = vmcs_read32(GUEST_GDTR_LIMIT);

	idt.address = vmcs_readl(GUEST_IDTR_BASE);
	idt.size = vmcs_read32(GUEST_IDTR_LIMIT);

	guest_ss = vmcs_read16(GUEST_SS_SELECTOR);
	guest_cs = vmcs_read16(GUEST_CS_SELECTOR);
	guest_cr0 = vmcs_readl(GUEST_CR0);
	guest_cr3 = vmcs_readl(GUEST_CR3);
	guest_cr4 = vmcs_readl(GUEST_CR4);
	guest_rip = vmcs_readl(GUEST_RIP) + vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
	guest_rflags = vmcs_readl(GUEST_RFLAGS);

	__repriv_restore_selectors();

	asm volatile ("vmxoff" ::: "memory");

	__repriv_restore_special_regs(guest_cr0, guest_cr3, guest_cr4, &gdt, &idt);

	asm volatile (
		/* Restore general purpose registers */
		"mov 0x00(%%rdi), %%rax\n"
		"mov 0x08(%%rdi), %%rcx\n"
		"mov 0x10(%%rdi), %%rdx\n"
		"mov 0x18(%%rdi), %%rbx\n"
		"mov 0x20(%%rdi), %%rsp\n"
		"mov 0x28(%%rdi), %%rbp\n"
		"mov 0x30(%%rdi), %%rsi\n"
		"mov 0x40(%%rdi), %%r8\n"
		"mov 0x48(%%rdi), %%r9\n"
		"mov 0x50(%%rdi), %%r10\n"
		"mov 0x58(%%rdi), %%r11\n"
		"mov 0x60(%%rdi), %%r12\n"
		"mov 0x68(%%rdi), %%r13\n"
		"mov 0x70(%%rdi), %%r14\n"
		"mov 0x78(%%rdi), %%r15\n"

		/* Restore RDI (last!) */
		"mov 0x38(%%rdi), %%rdi\n"

		/*
		 * update stack as expected by iretq
		 */
		"pushq %0\n"
		"pushq %%rsp\n"
		"addq $8, (%%rsp)\n"
		"pushq %1\n"
		"pushq %2\n"
		"pushq %3\n"

		"iretq\n"

		:
		: "m"(guest_ss), "m"(guest_rflags), "m"(guest_cs), "m"(guest_rip),
		  "D"(vcpu_regs)
		: "memory", "cc", "rax", "rbx", "rcx", "rdx", "rsi",
		  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
	);
}
STACK_FRAME_NON_STANDARD(pkvm_repriv_restore_cpu);
