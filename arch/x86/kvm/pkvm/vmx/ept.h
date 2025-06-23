/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PKVM_X86_EPT_H
#define __PKVM_X86_EPT_H

//FIXME: clean up the header files
#include <vmx/pkvm/hyp/pgtable.h>
#include <pkvm.h>

void ept_get_caps(struct pkvm_pgtable_cap *cap, struct pkvm_pgtable_ops **ops);

#endif /* __PKVM_X86_EPT_H */
