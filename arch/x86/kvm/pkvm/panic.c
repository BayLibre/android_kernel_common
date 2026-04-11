// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/kvm_pkvm.h>
#include <asm/msr.h>
#include <asm-generic/bug.h>
#include "debug.h"
#include "pkvm.h"
#include "memory.h"

/*
 * To not include ACPI based reboot and its complexity, try to reset the system
 * via PCI warm reset (0xCF9), the keyboard controller (PS/2), PCI cold reset
 * register (CF9), or triple fault.
 *
 * All based on arch/x86/kernel/reboot.c native_machine_emergency_restart().
 */
static void __noreturn pkvm_emergency_reset(void)
{
	struct desc_ptr idt;

	/*
	 * PCI warm reset (0xCF9), which is effectively the ACPI reset.
	 * This is the modern recommended default.
	 */
	outb(0x06, 0xcf9);
	pkvm_udelay(50000);

	/* Keyboard controller (PS/2) reset */
	outb(0xfe, 0x64);
	pkvm_udelay(50000);

	/* PCI cold reset */
	outb(0x0e, 0xcf9);
	pkvm_udelay(50000);

	/* Triple fault */
	idt.size = 0;
	idt.address = 0;
	asm volatile("lidt %0" : : "m"(idt));
	asm volatile("int3");

	while (1)
		asm volatile("cli; hlt");
}

atomic_t pkvm_panic_in_progress = ATOMIC_INIT(0);

void __noreturn pkvm_panic(const char *panic_msg)
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
	 * Broadcast INIT to all other CPUs (excluding self) via x2APIC ICR
	 * to pull them out of the host VM. This ensures that the host will not
	 * interfere with panic handling and e.g. will not interfere with
	 * ramoops update.
	 */
	native_x2apic_icr_write(APIC_DEST_ALLBUT | APIC_INT_ASSERT | APIC_DM_INIT, 0);

	/* TODO: add ramoops logging */

	pkvm_emergency_reset();
}

void __noreturn pkvm_bug_panic(const char *file, unsigned int line)
{
	char panic_msg[1024];

	scnprintf(panic_msg, sizeof(panic_msg),
		  "\n===================================\n"
		  "pKVM BUG at %s:%u\n"
		  "===================================\n",
		  file, line);

	pkvm_err("%s", panic_msg);

	pkvm_panic(panic_msg);
}
