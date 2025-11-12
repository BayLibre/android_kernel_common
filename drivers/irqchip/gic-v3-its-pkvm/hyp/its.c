// SPDX-License-Identifier: GPL-2.0-only

#include <nvhe/pkvm.h>

#include "module.h"
#include "emulate.h"
#include "gic-v3-its.h"

#include <nvhe/spinlock.h>

#include <nvhe/spinlock.h>
#include <linux/irqchip/arm-gic-v3.h>

#define GITS_TRANSLATER_PAGE ALIGN_DOWN(GITS_TRANSLATER, PAGE_SIZE)
#define GITS_TRANSLATER_PFN (GITS_TRANSLATER_PAGE >> PAGE_SHIFT)

struct hyp_gic_v3_its {
	void __iomem *base;
	struct emulate emulate;
	u64 saved_cbaser;
	void *host_cmd_base_va;
	void *host_cmd_cwriter_va;
	void *shadow_cmd;
};

struct hyp_gic_v3_its_handler {
	u64 offset;
	u8 access_size;
	int (*write)(struct hyp_gic_v3_its *its, u64 offset, u64 value);
	int (*read)(struct hyp_gic_v3_its *its, u64 offset, u64 *read);
};

static int forbidden_write(struct hyp_gic_v3_its *its, u64 offset, u64 value)
{
	return -EINVAL;
}

static int cbaser_write(struct hyp_gic_v3_its *its, u64 offset, u64 value)
{
	size_t cmdq_len = value & GENMASK(7, 0);

	if ((its->saved_cbaser & GENMASK(7, 0)) != cmdq_len ||
	    GITS_CBASER_ADDRESS(value) != GITS_CBASER_ADDRESS(its->saved_cbaser))
	    return -EPERM;

	its->saved_cbaser = value;
	writeq_relaxed(value, its->base + GITS_CBASER);
	return 0;
}

static int cbaser_read(struct hyp_gic_v3_its *its, u64 offset, u64 *read)
{
	*read = its->saved_cbaser;
	return 0;
}

static int creadr_read(struct hyp_gic_v3_its *its, u64 offset, u64 *read)
{
	*read = readq_relaxed(its->base + GITS_CREADR);
	return 0;
}

static int parse_its_cmdq(struct hyp_gic_v3_its *its, int cmd_offset, size_t len)
{
	return 0;
}

static int cwriter_write(struct hyp_gic_v3_its *its, u64 offset, u64 value)
{
	u64 cwriter_offset = value & GENMASK(19, 5);
	int cmd_len, cmd_offset;
	int ret;

	if (cwriter_offset >= ITS_CMD_QUEUE_SZ)
		return -EINVAL;

	cmd_offset = its->host_cmd_cwriter_va - its->host_cmd_base_va;
	cmd_len = cwriter_offset - cmd_offset;
	if (cmd_len < 0) {
		/* Detected command queue wrap around */
		its->host_cmd_cwriter_va = its->host_cmd_base_va;
		cmd_len = cwriter_offset;
		cmd_offset = 0;
	}

	if (cmd_offset + cmd_len > ITS_CMD_QUEUE_SZ) {
		mod_ops->puts("ITS shadow dropping command");
		return -EPERM;
	}

	memcpy(its->shadow_cmd + cmd_offset, its->host_cmd_cwriter_va, cmd_len);

	ret = parse_its_cmdq(its, cmd_offset, cmd_len);
	if (ret)
		return ret;

	its->host_cmd_cwriter_va += cmd_len;
	writeq_relaxed(value, its->base + GITS_CWRITER);
	return 0;
}

static int cwriter_read(struct hyp_gic_v3_its *its, u64 offset, u64 *read)
{
	*read = readq_relaxed(its->base + GITS_CWRITER);
	return 0;
}

#define GIC_V3_ITS_HANDLER(off, sz, write_cb, read_cb)	\
{							\
	.offset = (off),				\
	.access_size = (sz),				\
	.write = (write_cb),				\
	.read = (read_cb),					\
}

#define GIC_V3_ITS_HANDLER_64(off, write_cb, read_cb) \
	GIC_V3_ITS_HANDLER(off, sizeof(u64), write_cb, read_cb)

#define GIC_V3_ITS_HANDLER_RONLY(off, sz, read) \
	GIC_V3_ITS_HANDLER(off, sz, forbidden_write, read)

#define GIC_V3_ITS_HANDLER_RONLY_64(off, read) \
	GIC_V3_ITS_HANDLER_RONLY(off, sizeof(u64), read)

static const struct hyp_gic_v3_its_handler gic_v3_its_handlers[] =
{
	GIC_V3_ITS_HANDLER_64(GITS_CBASER, cbaser_write, cbaser_read),
	GIC_V3_ITS_HANDLER_RONLY_64(GITS_CREADR, creadr_read),
	GIC_V3_ITS_HANDLER_64(GITS_CWRITER, cwriter_write, cwriter_read),
	{},
};

static struct hyp_gic_v3_its its_devs[8];
static size_t its_dev_count = 0;
static DEFINE_HYP_SPINLOCK(its_devs_lock);
static DEFINE_HYP_SPINLOCK(its_lock);

#define for_each_its(__its) \
	for (__its = its_devs; __its != &its_devs[its_dev_count]; __its++)

static int its_emulate_handler(struct emulate *emulate, u64 offset, bool write,
			       u64 *reg, int reg_size)
{
	struct hyp_gic_v3_its *its = emulate->priv;
	const struct hyp_gic_v3_its_handler *handler;
	int ret;

	for (handler = gic_v3_its_handlers; handler->access_size != 0; handler++) {
		if (offset < handler->offset ||
		    offset >= handler->offset + handler->access_size)
			continue;

		/* Check for unaligned register access */
		if (offset != handler->offset || (handler->access_size & (reg_size - 1)))
			continue;

		if (write && handler->write) {
			hyp_spin_lock(&its_lock);
			ret = handler->write(its, offset, *reg);
			hyp_spin_unlock(&its_lock);
			return ret;
		}

		if (!write && handler->read) {
			hyp_spin_lock(&its_lock);
			ret = handler->read(its, offset, reg);
			hyp_spin_unlock(&its_lock);
			return ret;
		}

		break;
	}

	hyp_emulate_passthrough(its->base, offset, write, reg, reg_size);
	return 0;
}

static int hyp_gic_v3_its_shadow_cmdq(struct hyp_gic_v3_its *its, u64 host_cmd_base_pa)
{
	int ret, num_pages, i;
	phys_addr_t shadow_cmd_pa;
	u64 pfn, cbaser = readq_relaxed(its->base + GITS_CBASER);
	its->saved_cbaser = cbaser;

	shadow_cmd_pa = GITS_CBASER_ADDRESS(cbaser);
	num_pages = ITS_CMD_QUEUE_SZ >> PAGE_SHIFT;
	ret = host_donate_hyp(shadow_cmd_pa >> PAGE_SHIFT, num_pages);
	if (ret)
		return ret;

	its->shadow_cmd = hyp_phys_to_virt(shadow_cmd_pa);
	pfn = host_cmd_base_pa >> PAGE_SHIFT;
	for (i = 0; i < num_pages; i++) {
		ret = host_share_hyp(pfn);
		if (ret)
			goto remove_donation;

		pfn++;
	}

	its->host_cmd_base_va = hyp_phys_to_virt(host_cmd_base_pa);
	its->host_cmd_cwriter_va = its->host_cmd_base_va;

	ret = hyp_pin_shared_mem(its->host_cmd_base_va,
				 its->host_cmd_base_va + ITS_CMD_QUEUE_SZ);
	if (ret)
		goto remove_donation;

	return 0;
remove_donation:
	hyp_donate_host(shadow_cmd_pa >> PAGE_SHIFT, num_pages);
	for (i = i - 1; i >= 0; i--)
		host_unshare_hyp(pfn--);
	return ret;
}

static int hyp_gic_v3_its_protect(u64 paddr, u64 size, u64 host_cmd_base_pa)
{
	struct hyp_gic_v3_its *its;
	int ret;

	its = &its_devs[its_dev_count];
	its_dev_count++;

	ret = create_private_mapping(
		paddr, size, KVM_PGTABLE_PROT_RW | KVM_PGTABLE_PROT_DEVICE,
		(unsigned long *)&its->base);
	if (ret)
		return ret;

	its->emulate.base = paddr;
	its->emulate.size = size;
	its->emulate.handler = its_emulate_handler;
	its->emulate.priv = its;

	ret = hyp_gic_v3_its_shadow_cmdq(its, host_cmd_base_pa);
	if (ret)
		return ret;

	ret = hyp_add_emulate(&its->emulate);
	if (ret)
		return ret;

	/* Allow DMA/IO access back to the GITS_TRANSLATER */
	ret = host_stage2_mod_prot(
		(paddr >> PAGE_SHIFT) + GITS_TRANSLATER_PFN,
		KVM_PGTABLE_PROT_RW | KVM_PGTABLE_PROT_DEVICE, 1, true);

	return ret;
}

void hyp_gic_v3_its_protect_hvc(struct user_pt_regs *regs)
{
	int ret;
	u64 paddr, size;
	u64 host_cmd_base_pa;

	/* regs->regs[0] is the HVC function ID */
	paddr = regs->regs[1];
	size = regs->regs[2];
	host_cmd_base_pa = regs->regs[3];

	hyp_spin_lock(&its_devs_lock);
	ret = hyp_gic_v3_its_protect(paddr, size, host_cmd_base_pa);
	hyp_spin_unlock(&its_devs_lock);

	regs->regs[0] = SMCCC_RET_SUCCESS;
	regs->regs[1] = ret;
}
