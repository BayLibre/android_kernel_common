// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic page table allocator for IOMMUs.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/bug.h>
#include <linux/io-pgtable.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

static struct io_pgtable_init_fns *io_pgtable_init_table[IO_PGTABLE_NUM_FMTS];

struct io_pgtable_ops *alloc_io_pgtable_ops(enum io_pgtable_fmt fmt,
					    struct io_pgtable_cfg *cfg,
					    void *cookie)
{
	struct io_pgtable *iop;
	struct io_pgtable_init_fns *fns;

	if (fmt >= IO_PGTABLE_NUM_FMTS)
		return NULL;

	fns = io_pgtable_init_table[fmt];
	if (!fns)
		return NULL;

	if (!try_module_get(fns->owner))
		return NULL;

	iop = fns->alloc(cfg, cookie);
	if (!iop) {
		module_put(fns->owner);
		return NULL;
	}

	iop->fmt	= fmt;
	iop->cookie	= cookie;
	iop->cfg	= *cfg;

	return &iop->ops;
}
EXPORT_SYMBOL_GPL(alloc_io_pgtable_ops);

/*
 * It is the IOMMU driver's responsibility to ensure that the page table
 * is no longer accessible to the walker by this point.
 */
void free_io_pgtable_ops(struct io_pgtable_ops *ops)
{
	struct io_pgtable *iop;
	struct io_pgtable_init_fns *fns;

	if (!ops)
		return;

	iop = io_pgtable_ops_to_pgtable(ops);
	io_pgtable_tlb_flush_all(iop);
	fns = io_pgtable_init_table[iop->fmt];
	if (fns) {
		fns->free(iop);
		module_put(fns->owner);
	}
}
EXPORT_SYMBOL_GPL(free_io_pgtable_ops);

int io_pgtable_ops_register(enum io_pgtable_fmt fmt,
			    struct io_pgtable_init_fns *init_fns)
{
	if (fmt >= IO_PGTABLE_NUM_FMTS || !init_fns || !init_fns->alloc ||
	    !init_fns->free)
		return -EINVAL;
	else if (io_pgtable_init_table[fmt])
		return -EEXIST;

	io_pgtable_init_table[fmt] = init_fns;
	return 0;
}
EXPORT_SYMBOL_GPL(io_pgtable_ops_register);

void io_pgtable_ops_unregister(enum io_pgtable_fmt fmt)
{
	if (fmt >= IO_PGTABLE_NUM_FMTS)
		return;

	io_pgtable_init_table[fmt] = NULL;
}
EXPORT_SYMBOL_GPL(io_pgtable_ops_unregister);
