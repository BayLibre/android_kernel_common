// SPDX-License-Identifier: GPL-2.0-only
#include "module.h"
#include "emulate.h"
#include "gic-v3-its.h"

#include <nvhe/spinlock.h>
#include <linux/irqchip/arm-gic-v3.h>

struct hyp_gic_v3_redist {
	void __iomem *base;

	u64 typer;
	u64 propbaser;
	u64 pendbaser;

	struct emulate emulate;
};

static DEFINE_HYP_SPINLOCK(redist_lock);
static struct hyp_gic_v3_redist redist_devs[CONFIG_NR_CPUS];
static size_t redist_dev_count;

#define GICR_VLPI_OFFSET SZ_128K

#define PROPBASER_IDBITS(propbaser) ((propbaser) & GICR_PROPBASER_IDBITS_MASK)

#define PROPBASE_SZ(id_bits) ALIGN(BIT_ULL((id_bits) + 1), SZ_64K)
#define PENDBASE_SZ(id_bits) ALIGN(BIT_ULL((id_bits) + 1) / 8, SZ_64K)

#define for_each_gicr(__gicr)                                                \
	for (__gicr = redist_devs; __gicr != &redist_devs[redist_dev_count]; \
	     __gicr++)

static int host_unshare_pages_hyp(u64 addr, u64 num_pages)
{
	u64 base_pfn = addr >> PAGE_SHIFT;
	u64 i = 0;
	int ret = 0;

	for (i = 0; i < num_pages; i++)
		ret |= host_unshare_hyp(base_pfn + i);

	return ret;
}

static int host_share_pages_hyp(u64 addr, u64 num_pages)
{
	u64 base_pfn = addr >> PAGE_SHIFT;
	u64 i = 0;
	u64 j = 0;
	int ret;

	for (i = 0; i < num_pages; i++) {
		ret = host_share_hyp(base_pfn + i);
		if (ret)
			goto failed;
	}

	return 0;

failed:
	host_unshare_pages_hyp(addr, j);
	return ret;
}

static int host_owns_pages(u64 phys, u64 num_pages)
{
	int ret;
	kvm_pte_t pte;
	u64 end = phys + (num_pages << PAGE_SHIFT);

	while (phys < end) {
		ret = host_stage2_get_leaf(phys, &pte, NULL);
		if (ret)
			return -EFAULT;

		if (!kvm_pte_valid(pte))
			return -EFAULT;

		/* TODO: Check RW permissions? */
		phys += PAGE_SHIFT;
	}

	return 0;
}

static inline u64 gicr_stride(u32 typer, u64 dt_stride)
{
	if (dt_stride)
		return dt_stride;

	if (typer & GICR_TYPER_VLPIS)
		return SZ_256K;

	return SZ_128K;
}

static inline bool gicr_lpi_enabled(struct hyp_gic_v3_redist *gicr)
{
	u32 ctrl = readl_relaxed(gicr->base + GICR_CTLR);

	return FIELD_GET(GICR_CTLR_ENABLE_LPIS, ctrl);
}

static inline u64 gicr_lpi_aff(struct hyp_gic_v3_redist *gicr)
{
	u64 affinity = FIELD_GET(GICR_TYPER_AFFINITY, gicr->typer);
	u64 comm_lpi_aff = FIELD_GET(GICR_TYPER_COMMON_LPI_AFF, gicr->typer);

	return affinity & ~(GENMASK_ULL(32, 8) >> (comm_lpi_aff << 3));
}

static int propbaser_write(struct hyp_gic_v3_redist *gicr, u64 propbaser)
{
	struct hyp_gic_v3_redist *other_gicr;
	u64 id_bits;
	int ret;

	mod_ops->puts("GICR_PROPBASER (write)");

	/* Noop write */
	if (gicr->propbaser == propbaser)
		return 0;

	/* GICR_PROPBASER cannot be changed when LPI are enabled */
	if (gicr_lpi_enabled(gicr))
		return -EFAULT;

	id_bits = PROPBASER_IDBITS(propbaser);

	for_each_gicr(other_gicr) {
		/* Ignore self and GICR with disabled LPI */
		if (gicr == other_gicr || !gicr_lpi_enabled(other_gicr))
			continue;

		/*
		 * Require all GICR sharing a property table to have
		 * the same IDbits.
		 */
		if (GICR_PROPBASER_ADDRESS(other_gicr->propbaser) ==
			    GICR_PROPBASER_ADDRESS(propbaser) &&
		    PROPBASER_IDBITS(other_gicr->propbaser) != id_bits)
			return -EFAULT;

		/*
		 * If there's another GICR in the LPI affinity group with LPI
		 * enabled, stop now.
		 */
		if (gicr_lpi_aff(gicr) == gicr_lpi_aff(other_gicr))
			return -EFAULT;
	}

	/* Smoke test if the host own the pages */
	ret = host_owns_pages(propbaser, PROPBASE_SZ(id_bits));
	if (ret) {
		mod_ops->puts("Denied (smoke)");
		return -EFAULT;
	}

	gicr->propbaser = propbaser;
	return 0;
}

static int propbaser_read(struct hyp_gic_v3_redist *gicr, u64 *propbaser)
{
	mod_ops->puts("GICR_PROPBASER (read)");

	/* Return cached value */
	*propbaser = gicr->propbaser;

	return 0;
}

static int pendbaser_write(struct hyp_gic_v3_redist *gicr, u64 pendbaser)
{
	int ret;
	u64 id_bits;

	mod_ops->puts("GICR_PENDBASER (write)");
	if (gicr_lpi_enabled(gicr))
		return -EFAULT;

	/* Smoke test if the host own the pages */
	id_bits = PROPBASER_IDBITS(gicr->propbaser);
	ret = host_owns_pages(pendbaser, PENDBASE_SZ(id_bits));
	if (ret) {
		mod_ops->puts("Denied (smoke)");
		return -EFAULT;
	}

	gicr->pendbaser = pendbaser;
	return 0;
}

static int pendbaser_read(struct hyp_gic_v3_redist *gicr, u64 *pendbaser)
{
	mod_ops->puts("GICR_PENDBASER (read)");

	/* Return cached value */
	*pendbaser = gicr->pendbaser;

	return 0;
}

static int handle_lpi_enable(struct hyp_gic_v3_redist *gicr)
{
	struct hyp_gic_v3_redist *other_gicr;
	bool leader = true;
	u64 id_bits;
	int ret;

	mod_ops->puts("Enabling LPI");

	if (gicr->propbaser == 0)
		gicr->propbaser = readq_relaxed(gicr->base + GICR_PROPBASER);
	if (gicr->pendbaser == 0)
		gicr->pendbaser = readq_relaxed(gicr->base + GICR_PENDBASER);

	id_bits = PROPBASER_IDBITS(gicr->propbaser);

	/*
	 * Check if GICR is the first in LPI affinity group and verify that
	 * share the same pendbaser.
	 */
	for_each_gicr(other_gicr) {
		/* Ignore self and GICR with disabled LPI */
		if (gicr == other_gicr || !gicr_lpi_enabled(other_gicr))
			continue;

		/*
		 * Require all GICR sharing a property table to have
		 * the same IDbits.
		 */
		if (GICR_PROPBASER_ADDRESS(other_gicr->propbaser) ==
			    GICR_PROPBASER_ADDRESS(gicr->propbaser) &&
		    PROPBASER_IDBITS(other_gicr->propbaser) != id_bits)
			return -EFAULT;

		if (gicr_lpi_aff(gicr) != gicr_lpi_aff(other_gicr))
			continue;

		/* Require all GICRs in the affinity to share the same propbaser */
		if (gicr->propbaser != other_gicr->propbaser)
			return -EFAULT;
		/*
		 * There is already a GICR with LPI enabled in the LPI
		 * affinity group. Property table was already shared.
		 */
		leader = false;
	}

	/*
	 * If this is the first GICR in LPI affinity group, share the property
	 * table. We only need to do this once for the affinity group as
	 * all in the group share the same property table.
	 */
	if (leader) {
		/* Try to share new LPI property table */
		ret = host_share_pages_hyp(GICR_PROPBASER_ADDRESS(gicr->propbaser),
					   PROPBASE_SZ(id_bits) >> PAGE_SHIFT);
		if (ret) {
			mod_ops->puts("Denied (share LPI property table)");
			return ret;
		}
	}

	/* Try to share new LPI pending table */
	ret = host_share_pages_hyp(GICR_PENDBASER_ADDRESS(gicr->pendbaser),
				   PENDBASE_SZ(id_bits) >> PAGE_SHIFT);
	if (ret) {
		mod_ops->puts("Denied (share LPI pending table)");
		goto pend_share_fail;
	}

	/* Write the verified values to registers */
	writeq_relaxed(gicr->propbaser, gicr->base + GICR_PROPBASER);
	writeq_relaxed(gicr->pendbaser, gicr->base + GICR_PENDBASER);

	return 0;

pend_share_fail:
	/*
	 * It failed to share the LPI pending table, but if we shared the
	 * property table and LPI won't be enabled, the property table
	 * has to be unshared to avoid error state.
	 */
	if (leader) {
		host_unshare_pages_hyp(GICR_PROPBASER_ADDRESS(gicr->propbaser),
				       PROPBASE_SZ(id_bits) >> PAGE_SHIFT);
	}
	return -EFAULT;
}

static int handle_lpi_disable(struct hyp_gic_v3_redist *gicr)
{
	struct hyp_gic_v3_redist *other_gicr;
	bool last = true;
	u64 id_bits;
	int ret = 0;

	mod_ops->puts("Disabling LPI");

	for_each_gicr(other_gicr) {
		/* Ignore self and GICR with disabled LPI */
		if (gicr == other_gicr || !gicr_lpi_enabled(other_gicr))
			continue;

		/* Check if GICR is the last in LPI affinity group with LPI enabled. */
		if (gicr_lpi_aff(gicr) == gicr_lpi_aff(other_gicr))
			last = false;
	}

	id_bits = PROPBASER_IDBITS(gicr->propbaser);

	/*
	 * If this is the last GICR in affinity group to have LPI disabled
	 * we need to unshare the property table.
	 */
	if (last) {
		ret = host_unshare_pages_hyp(GICR_PROPBASER_ADDRESS(gicr->propbaser),
					     PROPBASE_SZ(id_bits) >> PAGE_SHIFT);
		if (ret)
			return -EFAULT;
	}

	/* Unshare old LPI pending table */
	if (gicr->pendbaser != 0)
		ret = host_unshare_pages_hyp(GICR_PENDBASER_ADDRESS(gicr->pendbaser),
					     PENDBASE_SZ(id_bits) >> PAGE_SHIFT);
	if (ret)
		goto pend_unshare_fail;

	return 0;
pend_unshare_fail:
	/*
	 * It failed to unshare the LPI pending table, but if we unshared the
	 * property table and LPI won't be disabled, the property table
	 * has to be shared back to avoid error state.
	 */
	if (last)
		host_share_pages_hyp(GICR_PROPBASER_ADDRESS(gicr->propbaser),
				     PROPBASE_SZ(id_bits) >> PAGE_SHIFT);

	return -EFAULT;
}

static int ctrl_write(struct hyp_gic_v3_redist *gicr, u64 ctrl)
{
	bool lpi_on;
	bool set_lpi;
	u64 curr_ctrl;

	mod_ops->puts("GICR_CTLR (write)");

	curr_ctrl = readl_relaxed(gicr->base + GICR_CTLR);
	lpi_on = FIELD_GET(GICR_CTLR_ENABLE_LPIS, curr_ctrl);

	set_lpi = FIELD_GET(GICR_CTLR_ENABLE_LPIS, ctrl);

	if (!lpi_on && set_lpi) {
		/* The GICR_CTLR.RWP needs to be 0, before enabling LPI */
		if (FIELD_GET(GICR_CTLR_RWP, curr_ctrl))
			return -EFAULT;

		if (handle_lpi_enable(gicr))
			return -EFAULT;
	} else if (lpi_on && !set_lpi) {
		if (handle_lpi_disable(gicr))
			return -EFAULT;
	}

	writel_relaxed(ctrl, gicr->base + GICR_CTLR);
	return 0;
}

static int forbidden_write(struct hyp_gic_v3_redist *gicr, u64 value)
{
	return -EINVAL;
}

static const struct hyp_gicr_handler {
	u64 offset;
	int size;
	int (*write)(struct hyp_gic_v3_redist *gicr, u64 value);
	int (*read)(struct hyp_gic_v3_redist *gicr, u64 *read);
} gicr_reg_handlers[] = {
	{
		.offset = GICR_CTLR,
		.size = sizeof(u32),
		.write = ctrl_write,
	},
	{
		.offset = GICR_PROPBASER,
		.size = sizeof(u64),
		.write = propbaser_write,
		.read = propbaser_read,
	},
	{
		.offset = GICR_PENDBASER,
		.size = sizeof(u64),
		.write = pendbaser_write,
		.read = pendbaser_read,
	},
	{
		.offset = GICR_VLPI_OFFSET + GICR_VPROPBASER,
		.size = sizeof(u64),
		.write = forbidden_write,
	},
	{
		.offset = GICR_VLPI_OFFSET + GICR_VPENDBASER,
		.size = sizeof(u64),
		.write = forbidden_write,
	},
	{},
};

static int emulate_handler(struct emulate *emulate, u64 offset, bool write,
			   u64 *reg, int reg_size)
{
	struct hyp_gic_v3_redist *gicr = emulate->priv;
	const struct hyp_gicr_handler *handler;
	int ret;

	for (handler = gicr_reg_handlers; handler->size != 0; handler++) {
		if (offset < handler->offset ||
		    offset >= handler->offset + handler->size)
			continue;

		/* Check for unaligned register access */
		if (offset != handler->offset || reg_size != handler->size)
			return -EFAULT;

		if (write && handler->write) {
			hyp_spin_lock(&redist_lock);
			ret = handler->write(gicr, *reg);
			hyp_spin_unlock(&redist_lock);
			return ret;
		} else if (!write && handler->read) {
			hyp_spin_lock(&redist_lock);
			ret = handler->read(gicr, reg);
			hyp_spin_unlock(&redist_lock);
			return ret;
		}

		break;
	}

	hyp_emulate_passthrough(gicr->base, offset, write, reg, reg_size);
	return 0;
}

static int redist_protect_locked(struct hyp_gic_v3_redist *gicr, u64 paddr,
				 void __iomem *base, u64 dt_stride)
{
	int ret;

	gicr->base = base;

	gicr->typer = readq_relaxed(base + GICR_TYPER);
	gicr->propbaser = 0;
	gicr->pendbaser = 0;

	gicr->emulate.base = paddr;
	gicr->emulate.handler = emulate_handler;
	gicr->emulate.priv = gicr;
	gicr->emulate.size = gicr_stride(gicr->typer, dt_stride);

	/* If GIC support Virtual LPI, make sure that it is not used */
	if (gicr->typer & GICR_TYPER_VLPIS) {
		/* Check if VLPI registers are accessiable */
		if (gicr->emulate.size < GICR_VLPI_OFFSET)
			return -EINVAL;

		/* Check if VLPI Property table is not valid */
		if (readq_relaxed(base + GICR_VLPI_OFFSET + GICR_VPROPBASER) &
		    GICR_VPROPBASER_4_1_VALID)
			return -EINVAL;

		/* Check if VLPI Pending table is not valid */
		if (readq_relaxed(base + GICR_VLPI_OFFSET + GICR_VPENDBASER) &
		    GICR_VPENDBASER_Valid)
			return -EINVAL;
	}

	if (gicr_lpi_enabled(gicr)) {
		ret = handle_lpi_enable(gicr);
		if (ret)
			return ret;
	}

	ret = hyp_add_emulate(&gicr->emulate);
	if (ret)
		return ret;

	return 0;
}

static int hyp_gic_v3_redist_protect(u64 paddr, u64 size, u64 dt_stride)
{
	struct hyp_gic_v3_redist *gicr;
	void *gicr_base;
	u64 end;
	int ret;

	if (!PAGE_ALIGNED(paddr) || !PAGE_ALIGNED(size) ||
	    !PAGE_ALIGNED(dt_stride))
		return -EINVAL;

	hyp_spin_lock(&redist_lock);

	ret = create_private_mapping(paddr, size, KVM_PGTABLE_PROT_RW | KVM_PGTABLE_PROT_DEVICE,
				     (unsigned long *)&gicr_base);
	if (ret)
		goto err;

	end = paddr + size;

	while (paddr < end) {
		gicr = &redist_devs[redist_dev_count];
		redist_dev_count++;

		ret = redist_protect_locked(gicr, paddr, gicr_base, dt_stride);
		if (ret) {
			redist_dev_count--;
			goto err;
		}

		paddr += gicr->emulate.size;
		gicr_base += gicr->emulate.size;

		if (gicr->typer & GICR_TYPER_LAST)
			break;
	}

err:
	hyp_spin_unlock(&redist_lock);
	return ret;
}

void hyp_gic_v3_redist_protect_hvc(struct user_pt_regs *regs)
{
	int ret;
	u64 paddr, size, stride;

	/* regs->regs[0] is the HVC function ID */
	paddr = regs->regs[1];
	size = regs->regs[2];
	stride = regs->regs[3];

	ret = hyp_gic_v3_redist_protect(paddr, size, stride);

	regs->regs[0] = SMCCC_RET_SUCCESS;
	regs->regs[1] = ret;
}
