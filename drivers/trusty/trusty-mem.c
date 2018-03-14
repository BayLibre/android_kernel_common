/*
 * Copyright (C) 2015-2019 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/bits.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/trusty/trusty.h>
#include <linux/trusty/smcall.h>

#define TRUSTY_PTE_USER   BIT(6)
#define TRUSTY_PTE_RDONLY BIT(7)

#define TRUSTY_PTE_INNER_SHAREABLE (3UL << 8)

static u64 mk_ns_attr(u64 pte, u64 mair)
{
	return (pte & 0x0000FFFFFFFFFFFFull) | (mair << 48);
}

#if defined(CONFIG_ARM64)
static int encode_page_inf(struct ns_mem_page_info *inf, pte_t pte)
{
	u64 mair;
	uint attr_index = (pte_val(pte) & PTE_ATTRINDX_MASK) >> 2;

	asm ("mrs %0, mair_el1\n" : "=&r" (mair));
	mair = (mair >> (attr_index * 8)) & 0xff;

	inf->attr = mk_ns_attr(pte_val(pte), mair);
	return 0;
}
#elif defined(CONFIG_ARM_LPAE)
static int encode_page_inf(struct ns_mem_page_info *inf, pte_t pte)
{
	u32 mair;
	u64 val;
	uint attr_index = ((pte_val(pte) & L_PTE_MT_MASK) >> 2);

	if (attr_index >= 4) {
		attr_index -= 4;
		asm volatile("mrc p15, 0, %0, c10, c2, 1\n" : "=&r" (mair));
	} else {
		asm volatile("mrc p15, 0, %0, c10, c2, 0\n" : "=&r" (mair));
	}
	mair = (mair >> (attr_index * 8)) & 0xff;

	/*
	 * Patch RDONLY attribute as linux pte format for LPAE does not match
	 * format that Trusty expects. The rest of bits are compatible.
	 */
	val = pte_val(pte);
	if (val & L_PTE_RDONLY)
		val |= TRUSTY_PTE_RDONLY;
	else
		val &= ~TRUSTY_PTE_RDONLY;

	inf->attr = mk_ns_attr(val, mair);
	return 0;
}
#elif defined(CONFIG_ARM)
static int encode_page_inf(struct ns_mem_page_info *inf, pte_t pte)
{
	u32 mair;
	u64 val = page_to_phys(pte_page(pte));

	/* check memory type */
	switch (pte_val(pte) & L_PTE_MT_MASK) {
	case L_PTE_MT_WRITEALLOC:
		/* Normal: write back write allocate */
		mair = 0xFF;
		break;

	case L_PTE_MT_BUFFERABLE:
		/* Normal: non-cacheble */
		mair = 0x44;
		break;

	case L_PTE_MT_WRITEBACK:
		/* Normal: writeback, read allocate */
		mair = 0xEE;
		break;

	case L_PTE_MT_WRITETHROUGH:
		/* Normal: write through */
		mair = 0xAA;
		break;

	case L_PTE_MT_UNCACHED:
		/* strongly ordered */
		mair = 0x00;
		break;

	case L_PTE_MT_DEV_SHARED:
	case L_PTE_MT_DEV_NONSHARED:
		/* device */
		mair = 0x04;
		break;

	default:
		return -EINVAL;
	}

	/* add other attributes */
	if (pte_val(pte) & L_PTE_USER)
		val |= TRUSTY_PTE_USER;

	if (pte_val(pte) & L_PTE_RDONLY)
		val |= TRUSTY_PTE_RDONLY;

	if (pte_val(pte) & L_PTE_SHARED)
		val |= TRUSTY_PTE_INNER_SHAREABLE;

	inf->attr = mk_ns_attr(val, mair);
	return 0;
}
#else
static int encode_page_inf(struct ns_mem_page_info *inf, u64 pte,
			   pgprot_t pgprot, vm_flags_t vm_flags)
{
	return -EINVAL;
}
#endif

int trusty_encode_page_info(struct ns_mem_page_info *inf, struct page *page,
			    pgprot_t pgprot, vm_flags_t vm_flags)
{
	pte_t pte;

	if (!inf || !page)
		return -EINVAL;

	pte = mk_pte(page, pgprot);
	if (vm_flags & VM_WRITE)
		pte = pte_mkwrite(pte);

	return encode_page_inf(inf, pte);
}

int trusty_call32_mem_buf(struct device *dev, u32 smcnr,
			  struct page *page,  u32 size,
			  pgprot_t pgprot, vm_flags_t vm_flags)
{
	int ret;
	struct ns_mem_page_info pg_inf;

	if (!dev || !page)
		return -EINVAL;

	ret = trusty_encode_page_info(&pg_inf, page, pgprot, vm_flags);
	if (ret)
		return ret;

	if (SMC_IS_FASTCALL(smcnr)) {
		return trusty_fast_call32(dev, smcnr,
					  (u32)pg_inf.attr,
					  (u32)(pg_inf.attr >> 32), size);
	} else {
		return trusty_std_call32(dev, smcnr,
					 (u32)pg_inf.attr,
					 (u32)(pg_inf.attr >> 32), size);
	}
}
