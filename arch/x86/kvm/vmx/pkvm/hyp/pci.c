// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Intel Corporation. */
#include <asm/pci_x86.h>
#include <asm/pkvm_spinlock.h>
#include <pkvm.h>

#include "io.h"
#include "mmu.h"
#include "pci.h"
#include "memory.h"
#include "debug.h"

static pkvm_spinlock_t pci_cfg_lock = __PKVM_SPINLOCK_UNLOCKED;

static int pci_cfg_space_read(union pci_cfg_addr_reg *cfg_addr,
	u32 offset, int size, unsigned long *value)
{
	pkvm_spin_lock(&pci_cfg_lock);

	pkvm_pio_write(PCI_CFG_ADDR, 4, cfg_addr->value);
	pkvm_pio_read(PCI_CFG_DATA + offset, size, value);

	pkvm_spin_unlock(&pci_cfg_lock);

	return 0;
}

static int pci_cfg_space_write(union pci_cfg_addr_reg *cfg_addr,
	u32 offset, int size, unsigned long value)
{
	pkvm_spin_lock(&pci_cfg_lock);

	pkvm_pio_write(PCI_CFG_ADDR, 4, cfg_addr->value);
	pkvm_pio_write(PCI_CFG_DATA + offset, size, value);

	pkvm_spin_unlock(&pci_cfg_lock);

	return 0;
}

unsigned long pkvm_pci_cfg_space_read(u32 bdf, u32 offset, int size)
{
	union pci_cfg_addr_reg reg;
	unsigned long value = 0;

	reg.enable = 1;
	reg.bdf = bdf;
	reg.reg = offset & (~0x3);

	pci_cfg_space_read(&reg, offset & 0x3, size, &value);

	return value;
}

void pkvm_pci_cfg_space_write(u32 bdf, u32 offset, int size, unsigned long value)
{
	union pci_cfg_addr_reg reg;

	reg.enable = 1;
	reg.bdf = bdf;
	reg.reg = offset & (~0x3);

	pci_cfg_space_write(&reg, offset & 0x3, size, value);
}
