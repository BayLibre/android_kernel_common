// SPDX-License-Identifier: GPL-2.0-only

#include <nvhe/pkvm.h>

#include "module.h"
#include "emulate.h"
#include "gic-v3-its.h"

#include <nvhe/spinlock.h>

struct hyp_gic_v3_its {
	void __iomem *base;
	struct emulate emulate;
};

static struct hyp_gic_v3_its its_devs[8];
static size_t its_dev_count;
static DEFINE_HYP_SPINLOCK(its_devs_lock);

static int its_emulate_handler(struct emulate *emulate, u64 offset, bool write,
			       u32 *reg)
{
	struct hyp_gic_v3_its *its = emulate->priv;

	if (write)
		writel_relaxed(*reg, its->base + offset);
	else
		*reg = readl_relaxed(its->base + offset);

	return 0;
}

static int hyp_gic_v3_its_protect(u64 paddr, u64 size)
{
	struct hyp_gic_v3_its *its;
	int ret;

	its = &its_devs[its_dev_count];
	its_dev_count++;

	ret = create_private_mapping(paddr, size, KVM_PGTABLE_PROT_RW | KVM_PGTABLE_PROT_DEVICE,
				     (unsigned long *)&its->base);
	if (ret)
		return ret;

	its->emulate.base = paddr;
	its->emulate.size = size;
	its->emulate.handler = its_emulate_handler;
	its->emulate.priv = its;

	ret = hyp_add_emulate(&its->emulate);
	if (ret)
		return ret;

	return 0;
}

void hyp_gic_v3_its_protect_hvc(struct user_pt_regs *regs)
{
	int ret;
	u64 paddr, size;

	/* regs->regs[0] is the HVC function ID */
	paddr = regs->regs[1];
	size = regs->regs[2];

	hyp_spin_lock(&its_devs_lock);
	ret = hyp_gic_v3_its_protect(paddr, size);
	hyp_spin_unlock(&its_devs_lock);

	regs->regs[0] = SMCCC_RET_SUCCESS;
	regs->regs[1] = ret;
}
