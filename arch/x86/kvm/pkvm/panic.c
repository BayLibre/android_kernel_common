// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/kvm_pkvm.h>
#include <asm/msr.h>
#include <asm-generic/bug.h>
#include "pkvm.h"
#include "memory.h"

/*
 * To not include ACPI based reboot and its complexity, try to reset the system
 * via the keyboard controller (PS/2), PCI reset register (CF9), or triple fault.
 *
 * All based on arch/x86/kernel/reboot.c native_machine_emergency_restart().
 */
static void __noreturn pkvm_emergency_reset(void)
{
	struct desc_ptr idt;

	outb(0xfe, 0x64);
	pkvm_udelay(50000);

	outb(0x0e, 0xcf9);
	pkvm_udelay(50000);

	idt.size = 0;
	idt.address = 0;
	asm volatile("lidt %0" : : "m"(idt));
	asm volatile("int3");

	while (1)
		asm volatile("cli; hlt");
}

atomic_t pkvm_panic_in_progress = ATOMIC_INIT(0);

void __noreturn pkvm_hyp_panic(struct pt_regs *regs, const char *file, unsigned int line)
{
	/*
	 * Ensure only one CPU handles the panic and writes to ramoops.
	 * This also sets the global 'panic_in_progress' flag which signals
	 * the VM exit handler to catch and hold all other CPUs.
	 */
	if (atomic_cmpxchg(&pkvm_panic_in_progress, 0, 1) != 0) {
		while (1)
			asm volatile("cli; hlt");
	}

	/*
	 * Broadcast NMI to all other CPUs (excluding self) via x2APIC ICR
	 * to pull them out of the host VM. This ensures that the host will not
	 * interfere with panic handling and e.g. will not interfere with
	 * ramoops update.
	 */
	wrmsrl(0x830, 0xC0400);

	/* TODO: add rammops loging */

	pkvm_emergency_reset();
}
