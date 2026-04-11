// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/atomic.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/kvm_pkvm.h>
#include <asm/msr.h>
#include <asm-generic/bug.h>
#include "debug.h"
#include "pkvm.h"
#include "memory.h"
#include "pkvm_constants.h"

static void pkvm_write_ramoops_console(const char *msg)
{
	void *buffer = NULL;
	char *data;
	size_t msg_len = strlen(msg);
	size_t capacity;
	u32 start, size;
	void *console_vaddr = __pkvm_va(pkvm_ramoops_console_pa);

	if (!pkvm_ramoops_console_pa || !pkvm_ramoops_console_size)
		return;

	/*
	 * Sanity check if writing to correct area by making sure it is valid
	 * persistent_ram_buffer structure which starts with RAMOOPS_SIG_VAL
	 */
	if (READ_ONCE(*(u32 *)(console_vaddr + PKVM_RAMOOPS_BUFFER_SIG_OFFSET)) !=
	    PKVM_RAMOOPS_SIG_VAL)
		return;

	buffer = console_vaddr;

	/*
	 * The ramoops console uses the 'persistent_ram_buffer' ABI defined in
	 * fs/pstore/ram_core.c. The buffer starts with a header:
	 * [u32 sig] [u32 start] [u32 size] [raw data...]
	 */
	capacity = pkvm_ramoops_console_size - PKVM_RAMOOPS_BUFFER_DATA_OFFSET;
	data = (char *)(buffer + PKVM_RAMOOPS_BUFFER_DATA_OFFSET);

	/*
	 * 'start' indicates where the host stopped logging. In the
	 * persistent_ram_buffer ABI, this field is the "write pointer".
	 *
	 * By reading this value, the hypervisor knows where the host VM (which
	 * is now frozen) left off, allowing the panic message to be appended
	 * to the console log.
	 */
	start = atomic_read((atomic_t *)(buffer + PKVM_RAMOOPS_BUFFER_START_OFFSET));
	size = atomic_read((atomic_t *)(buffer + PKVM_RAMOOPS_BUFFER_SIZE_OFFSET));

	if (start >= capacity)
		start = 0;

	/* Circular append logic */
	if (start + msg_len <= capacity) {
		memcpy(data + start, msg, msg_len);
		start += msg_len;
	} else {
		size_t first_part = capacity - start;
		size_t second_part = msg_len - first_part;

		memcpy(data + start, msg, first_part);
		if (second_part > capacity)
			second_part = capacity;
		memcpy(data, msg + first_part, second_part);
		start = second_part;
	}

	if (start >= capacity)
		start = 0;

	/* Update total valid data size, capping at the buffer capacity */
	if (size < capacity) {
		size += msg_len;
		if (size > capacity)
			size = capacity;
	}

	/* Sync metadata so host pstore can find the new data after reboot */
	atomic_set((atomic_t *)(buffer + PKVM_RAMOOPS_BUFFER_START_OFFSET), start);
	atomic_set((atomic_t *)(buffer + PKVM_RAMOOPS_BUFFER_SIZE_OFFSET), size);

	/* Ensure data is visible in physical RAM before the hardware reset */
	clflush_cache_range(buffer, pkvm_ramoops_console_size);
}

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

	pkvm_write_ramoops_console(panic_msg);

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
