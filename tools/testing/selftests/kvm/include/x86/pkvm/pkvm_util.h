/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTESTS_PKVM_UTIL_H
#define SELFTESTS_PKVM_UTIL_H

#include "kvm_util.h"

void vm_pkvm_setup_boot_code_region(struct kvm_vm *vm);

#endif /* SELFTESTS_PKVM_UTIL_H */