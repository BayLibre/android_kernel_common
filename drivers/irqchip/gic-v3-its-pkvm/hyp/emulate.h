// SPDX-License-Identifier: GPL-2.0-only
#ifndef __GIC_V3_ITS_PKVM_EMULATE__
#define __GIC_V3_ITS_PKVM_EMULATE__

#include <asm/kvm_pkvm_module.h>

#include <linux/list.h>
#include <linux/types.h>

struct emulate;

typedef int(emulate_handler_t)(struct emulate *emulate, u64 offset, bool write,
			       u32 *reg);

struct emulate {
	u64 base;
	u64 size;

	emulate_handler_t *handler;

	void *priv;

	struct list_head list;
};

int hyp_add_emulate(struct emulate *emulate);

int hyp_emulate_perm_fault(struct user_pt_regs *regs, u64 esr, u64 addr);

#endif /* __GIC_V3_ITS_PKVM_EMULATE__ */
