// SPDX-License-Identifier: GPL-2.0-only
#include "module.h"
#include "emulate.h"
#include "gic-v3-its.h"

#include <asm/kvm_hyp.h>
#include <asm/kvm_pkvm_module.h>

#ifdef MODULE
const struct pkvm_module_ops *mod_ops;

int hyp_gic_v3_its_init(const struct pkvm_module_ops *ops)
{
	if (!ops)
		return -EINVAL;

	mod_ops = ops;

	return register_host_perm_fault_handler(hyp_emulate_perm_fault);
}
#endif /* MODULE */
