/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_PKVM_BOOT_ASM_H
#define SELFTEST_PKVM_BOOT_ASM_H

/*
 * GPA where pKVM boot parameters will be loaded.
 *
 * This location is chosen so that the boot parameter block lives below 1MB,
 * consistent with real-mode startup and reset vector placement.
 */
#define PKVM_BOOT_PARAMETERS_GPA 0x000f0000
#define PKVM_AP_SIPI_BOOT_GPA    0x000ff000

#define PKVM_BOOT64_CS 0x8
#define PKVM_BOOT32_DS 0x10
#define PKVM_BOOT32_CS 0x18

#endif /* SELFTEST_PKVM_BOOT_ASM_H */