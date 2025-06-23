// SPDX-License-Identifier: GPL-2.0
#include "mmu.h"
//FIXME: clean up the header files
#include <vmx/pkvm/hyp/pgtable.h>
#include <pkvm.h>

struct pkvm_pgtable_ops guest_mmu_ops;
struct pkvm_pgtable_cap guest_mmu_cap;
