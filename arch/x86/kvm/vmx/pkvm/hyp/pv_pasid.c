/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2025 Google
 */
#include <../drivers/iommu/intel/iommu.h>
#include <asm/pkvm_spinlock.h>
#include <linux/pci.h>
#include <pkvm.h>
#include "pkvm_hyp.h"
#include "gfp.h"
#include "memory.h"
#include "mmu.h"
#include "ept.h"
#include "pgtable.h"
#include "mem_protect.h"
#include "iommu_internal.h"
#include "debug.h"
#include "iommu_spgt.h"
#include "bug.h"
#include "iommu.h"

static void __pasid_setup_fl(struct intel_iommu *iommu, struct pasid_entry *pe, u64 flptr,
		u16 did, bool force_snoop)
{
	pasid_clear_entry(pe);
	pasid_set_flptr(pe, flptr);

	if (agaw_to_level(iommu->agaw) == 5 && cap_fl5lp_support(iommu->cap))
		pasid_set_flpm(pe, 1);

	if (force_snoop)
		pasid_set_pgsnp(pe);

	pasid_set_domain_id(pe, did);
	pasid_set_address_width(pe, iommu->agaw);
	pasid_set_page_snoop(pe, !!ecap_smpwc(iommu->ecap));
	pasid_set_translation_type(pe, PASID_ENTRY_PGTT_FL_ONLY);

	pasid_set_present(pe);
}

static void __pasid_setup_sl(struct intel_iommu *iommu, struct pasid_entry *pe, u64 slptr,
		u16 did, u8 agaw, bool dirty_tracking)
{
	pasid_clear_entry(pe);
	pasid_set_domain_id(pe, did);
	pasid_set_slptr(pe, slptr);
	pasid_set_address_width(pe, agaw);
	pasid_set_translation_type(pe, PASID_ENTRY_PGTT_SL_ONLY);
	pasid_set_fault_enable(pe);
	pasid_set_page_snoop(pe, !!ecap_smpwc(iommu->ecap));
	pasid_set_ssade(pe, dirty_tracking);

	pasid_set_present(pe);
}

static int validate_pasid_entry(struct pkvm_iommu *iommu, u16 bdf, struct pasid_entry *pe)
{
	u16 ndoms = cap_ndoms(iommu->iommu.cap);
	u64 flptr = 0, slptr = 0;
	u16 pgtt, did;
	u8 flpm, agaw, pgsnoop;

	agaw = pasid_get_address_width(pe);
	if (agaw < 1 || agaw > 3) {
		pkvm_err("pkvm: %s: invalid agaw(%u)!\n", __func__, agaw);
		return -1;
	}

	did = pasid_get_domain_id(pe);
	if (did > ndoms) {
		pkvm_err("pkvm: %s: did(%u) > max supported domains(%u)!\n",
				__func__, did, ndoms);
		return -1;
	}

	flpm = pasid_get_flpm(pe);
	if (flpm > 1) {
		pkvm_err("pkvm: %s: invalid FSPM(%u)!\n", __func__, flpm);
		return -1;
	}

	pgsnoop = pasid_get_pgsnp(pe);
	pgtt = pasid_get_translation_type(pe);

	slptr = pasid_get_slptr(pe);
	flptr = pasid_get_flptr(pe);

	if (pgtt == PASID_ENTRY_PGTT_PT) {
		int level = pkvm_host_ept_level();
		agaw = (level == 3) ? 1 : (level == 4) ? 2 : 3;

		if (flptr || slptr) {
			pkvm_err("pkvm: %s: SLPTR or FLPTR set with PASID_ENTRY_PGTT_PT!\n",
					__func__);
			return -1;
		}

		slptr = pkvm_host_ept_pgd();
		pgtt = PASID_ENTRY_PGTT_SL_ONLY;
	}

	if (pgtt == PASID_ENTRY_PGTT_FL_ONLY) {
		if (slptr) {
			pkvm_err("pkvm: %s: SLPTR set with PASID_ENTRY_PGTT_FL_ONLY!\n",
					__func__);
			return -1;
		} else if (!flptr) {
			pkvm_err("pkvm: %s: FLPTR unset with PASID_ENTRY_PGTT_FL_ONLY!\n",
					__func__);
			return -1;
		}
		__pasid_setup_fl(&iommu->iommu, pe, flptr, did, pgsnoop);
	} else if (pgtt == PASID_ENTRY_PGTT_SL_ONLY) {
		if (flptr) {
			pkvm_err("pkvm: %s: FLPTR set with PASID_ENTRY_PGTT_SL_ONLY!\n",
					__func__);
			return -1;
		} else if (!slptr) {
			pkvm_err("pkvm: %s: SLPTR unset with PASID_ENTRY_PGTT_SL_ONLY!\n",
					__func__);
			return -1;
		}
		__pasid_setup_sl(&iommu->iommu, pe, slptr, did, agaw, false);
	} else {
		pkvm_err("pkvm: %s: unsupported translation type: %x!\n",
				__func__, pgtt);
		return -1;
	}

	return 0;
}

static int validate_pasid_entries(struct pkvm_iommu *iommu, u32 pasid_start,
		u16 bdf, struct pasid_entry *pe)
{
	int pe_idx;

	pkvm_dbg("pkvm: %s: write protecting pasid table: %llx\n",
			__func__, pkvm_virt_to_phys(pe));
	if (pkvm_switch_host_ept_ro(pkvm_virt_to_phys(pe), VTD_PAGE_SIZE)) {
		pkvm_err("pkvm: %s: failed to write protect pasid table page!\n", __func__);
		return -EFAULT;
	}

	for (pe_idx = 0; pe_idx < PASIDTAB_ENTRIES; pe_idx++, pe++) {
		u32 pasid = pasid_start + pe_idx;

		if (!pasid_pte_is_present(pe))
			continue;

		pkvm_dbg("pkvm: %s: dev[%x], pasid: %x, PE: %llx%llx:%llx%llx:%llx%llx:%llx%llx\n",
				__func__, bdf, pasid,
				pe->val[7], pe->val[6], pe->val[5], pe->val[4],
				pe->val[3], pe->val[2], pe->val[1], pe->val[0]);
		if (validate_pasid_entry(iommu, bdf, pe))
			return -1;
	}

	return 0;
}

static int validate_pasid_dir(struct pkvm_iommu *iommu, u16 bdf, u32 nr_pdes,
			struct pasid_dir_entry *pde)
{
	int pde_index, ret;

	for (pde_index = 0; pde_index < nr_pdes; pde_index++, pde++) {
		struct pasid_entry *pe;

		if (IS_ALIGNED(pkvm_virt_to_phys(pde), VTD_PAGE_SIZE)) {
			pkvm_dbg("pkvm: %s: write protecting pasid dir: %llx\n",
					__func__, pkvm_virt_to_phys(pde));
			if (pkvm_switch_host_ept_ro(pkvm_virt_to_phys(pde), VTD_PAGE_SIZE)) {
				pkvm_err("pkvm: %s: failed to write protect pasid dir page!\n", __func__);
				return -EFAULT;
			}
		}

		pe = get_pasid_table_from_pde(pde);
		if (!pe)
			continue;

		ret = validate_pasid_entries(iommu, pde_index << PASIDDIR_SHIFT, bdf, pe);
		if (ret) {
			pkvm_err("pkvm: %s: failed to validate pasid table entry\n", __func__);
			return ret;
		}
	}

	return 0;
}

int validate_sm_context_entries(struct pkvm_iommu *iommu,
				u8 bus, struct context_entry *context, bool upper)
{
	int ce_idx, ret;

	pkvm_dbg("pkvm: %s: write protecting sm context table: %llx\n",
			__func__, pkvm_virt_to_phys(context));
	if (pkvm_switch_host_ept_ro(pkvm_virt_to_phys(context), VTD_PAGE_SIZE)) {
		pkvm_err("pkvm: %s: failed to write protect sm context table Page!\n", __func__);
		return -EFAULT;
	}

	for (ce_idx = 0; ce_idx < 128; ce_idx++) {
		struct context_entry *ce = &context[ce_idx * 2];
		u8 devfn = upper ? 128 + ce_idx : ce_idx;
		u16 bdf = PCI_DEVID(bus, devfn);
		struct pasid_dir_entry *pde;
		u32 nr_pdes;

		if (!context_present(ce))
			continue;

		if (ce[1].hi || ce[1].lo) {
			pkvm_err("pkvm: %s: device [%x], upper bits of context entry not zero!\n",
					__func__, bdf);
			return -1;
		}

		pkvm_dbg("pkvm: %s: [SM]: dev[%x], CE: %llx:%llx:%llx:%llx\n",
				__func__, bdf,
				ce[1].hi, ce[1].lo, ce[0].hi, ce[0].lo);

		pde = pkvm_phys_to_virt(ce->lo & VTD_PAGE_MASK);
		nr_pdes = get_pasid_dir_size(ce);

		ret = validate_pasid_dir(iommu, bdf, nr_pdes, pde);
		if (ret) {
			return -1;
		}
	}

	return 0;
}

static int pkvm_pasid_get_entry(struct intel_iommu *iommu, u32 pasid, u16 bdf,
		u64 *ptable_gpa, struct pasid_entry **pte)
{

	struct pasid_dir_entry *dir;
	struct pasid_entry *entries;
	struct context_entry *context;
	u8 bus, devfn;
	int dir_index, index;
	u32 pds, max_pasid;

	bus = PCI_BUS_NUM(bdf);
	devfn = PCI_DEV_FN(bdf);
	context = pkvm_iommu_context_addr(iommu, bus, devfn, 0);
	if (!context || !context_present(context)) {
		pkvm_err("pkvm: %s: pasid directory table not found: device=%x\n", __func__, bdf);
		return -EINVAL;
	}

	pds = 1 << (((context->lo >> 9) & 0x7) + 7);
	max_pasid = pds << PASIDDIR_SHIFT;
	if (pasid >= max_pasid) {
		pkvm_err("pkvm: %s: unexpected pasid:  device[%x] pasid=%u, max_pasid=%u\n",
				__func__, bdf, pasid, max_pasid);
		return -EINVAL;
	}

	dir = pkvm_phys_to_virt(context->lo & VTD_PAGE_MASK);
	dir_index = pasid >> PASIDDIR_SHIFT;
	index = pasid & PASID_PTE_MASK;

retry:
	entries = get_pasid_table_from_pde(&dir[dir_index]);
	if (!entries) {
		u64 tmp;
		u64 ptable_hpa;

		if (!*ptable_gpa)
			return -ENOMEM;

		ptable_hpa = host_gpa2hpa(*ptable_gpa);
		entries = host_gpa2hva(*ptable_gpa);

		pkvm_dbg("pkvm: %s: write protecting pasid table: %llx\n",
				__func__, pkvm_virt_to_phys(entries));
		if (pkvm_switch_host_ept_ro(pkvm_virt_to_phys(entries), VTD_PAGE_SIZE)) {
			pkvm_err("pkvm: %s: failed to write protect pasid table Page!\n", __func__);
			return -EFAULT;
		}
		memset(entries, 0, VTD_PAGE_SIZE);

		/*
		 * The pasid directory table entry won't be freed after
		 * allocation. No worry about the race with free and
		 * clear. However, this entry might be populated by others
		 * while we are preparing it. Use theirs with a retry.
		 */
		tmp = 0ULL;
		if (!try_cmpxchg64(&dir[dir_index].val, &tmp,
				   (u64)ptable_hpa | PASID_PTE_PRESENT)) {
			goto retry;
		}

		if (!iommu_coherency(iommu)) {
			pkvm_clflush_cache_range(entries, VTD_PAGE_SIZE);
			pkvm_clflush_cache_range(&dir[dir_index].val, sizeof(*dir));
		}
		*ptable_gpa = 0;
	}

	*pte = &entries[index];
	return 0;
}

/*
 * This function flushes cache for a newly setup pasid table entry.
 * Caller of it should not modify the in-use pasid table entries.
 */
static void pkvm_pasid_flush_caches(struct pkvm_iommu *hyp_iommu,
				struct pasid_entry *pte,
			       u32 pasid, u16 did)
{
	struct intel_iommu *iommu = &hyp_iommu->iommu;

	if (!iommu_coherency(iommu))
		pkvm_clflush_cache_range(pte, sizeof(*pte));

	if (cap_caching_mode(iommu->cap)) {
		flush_pasid_cache(hyp_iommu, did, QI_PC_PASID_SEL, pasid);
		flush_piotlb(hyp_iommu, did, pasid, 0, -1, 0);
	} else {
		flush_write_buffer(hyp_iommu);
	}
}

int pkvm_iommu_clear_pasid_entry(u64 param_va)
{
	struct pkvm_clear_translation_param *param;
	struct pkvm_iommu *hyp_iommu;
	struct intel_iommu *iommu;
	struct pasid_entry *pte;
	int ret = -ENODEV;
	u16 pgtt;

	if (!param_va)
		return -EINVAL;

	param = (struct pkvm_clear_translation_param *)kern_pkvm_va((void *)param_va);
	if (WARN_ON_ONCE(param != this_pv_param(clear_translation_param)))
		return -EINVAL;

	hyp_iommu = find_iommu_by_reg_phys(param->phys);
	if (!hyp_iommu)
		return -EINVAL;

	pkvm_spin_lock(&hyp_iommu->lock);
	iommu = &hyp_iommu->iommu;

	ret = pkvm_pasid_get_entry(iommu, param->pasid, param->bdf, NULL, &pte);
	if (ret) {
		pkvm_err("pkvm: %s: failed to get pasid table entry for device[%x], err=%d\n",
				__func__, param->bdf, ret);
		goto out_unlock;
	}
	if (!pasid_pte_is_present(pte)) {
		pkvm_err("pkvm: %s: pte for teardown not present!\n", __func__);
		ret = -ENODEV;
		goto out_unlock;
	}

	param->did = pasid_get_domain_id(pte);
	pgtt = pasid_get_translation_type(pte);
	pkvm_dbg("pkvm: %s: clear_pe: dev[%x] pasid: %x, did: %x\n",
			__func__, param->bdf, param->pasid, param->did);
	pasid_clear_entry(pte);
	ret = 0;

out_unlock:
	pkvm_spin_unlock(&hyp_iommu->lock);

	if (ret)
		return ret;

	if (!iommu_coherency(iommu))
		pkvm_clflush_cache_range(pte, sizeof(*pte));

	flush_pasid_cache(hyp_iommu, param->did, QI_PC_PASID_SEL, param->pasid);

	if (pgtt == PASID_ENTRY_PGTT_PT || pgtt == PASID_ENTRY_PGTT_FL_ONLY)
		flush_piotlb(hyp_iommu, param->did, param->pasid, 0, -1, 0);
	else
		flush_iotlb(hyp_iommu, param->did, 0, 0, DMA_TLB_DSI_FLUSH);

	return 0;
}

/*
 * Set up the scalable mode pasid table entry for first only
 * translation type.
 */
int pkvm_iommu_set_pasid_fl(u64 param_va)
{
	struct pkvm_pasid_table_param *param;
	struct pkvm_iommu *hyp_iommu;
	struct intel_iommu *iommu;
	struct pasid_entry *pte;
	int ret = -EINVAL;

	if (!param_va)
		return -EINVAL;

	param = (struct pkvm_pasid_table_param *)kern_pkvm_va((void *)param_va);
	if (WARN_ON_ONCE(param != this_pv_param(pasid_table_param)))
		return -EINVAL;

	hyp_iommu = find_iommu_by_reg_phys(param->phys);
	if (!hyp_iommu)
		return -EINVAL;

	pkvm_spin_lock(&hyp_iommu->lock);
	iommu = &hyp_iommu->iommu;

	if (!ecap_flts(iommu->ecap)) {
		pr_err("pkvm: %s: No first level translation support on iommu%d\n",
		       __func__, iommu->seq_id);
		goto out_unlock;
	}

	pkvm_dbg("pkvm: %s: setup_fl: dev[%x] pasid: %x, did: %x, flptr: %llx\n",
			__func__, param->bdf, param->pasid,
			param->did, param->domain_pgd_gpa);

	ret = pkvm_pasid_get_entry(iommu, param->pasid, param->bdf, &param->pasid_table_gpa, &pte);
	if (ret)
		goto out_unlock;

	if (pasid_pte_is_present(pte)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	__pasid_setup_fl(iommu, pte, param->domain_pgd_gpa,
			param->did, param->force_snooping);

out_unlock:
	pkvm_spin_unlock(&hyp_iommu->lock);

	if (!ret)
		pkvm_pasid_flush_caches(hyp_iommu, pte, param->pasid, param->did);

	return ret;
}

static int pasid_setup_sl(struct pkvm_iommu *hyp_iommu, struct pkvm_pasid_table_param *param)
{
	struct intel_iommu *iommu;
	struct pasid_entry *pte;
	int ret = -EINVAL;

	pkvm_spin_lock(&hyp_iommu->lock);
	iommu = &hyp_iommu->iommu;

	/*
	 * If hardware advertises no support for second level
	 * translation, return directly.
	 */
	if (!ecap_slts(iommu->ecap)) {
		pkvm_err("pkvm: %s: No second level translation support on iommu%d\n",
		       __func__, iommu->seq_id);
		goto out_unlock;
	}

	if (param->domain_agaw < 0 || param->domain_agaw > iommu->agaw) {
		pkvm_err("pkvm: %s: Invalid domain agaw(%d)\n",
				__func__, param->domain_agaw);
		goto out_unlock;
	}

	ret = pkvm_pasid_get_entry(iommu, param->pasid, param->bdf, &param->pasid_table_gpa, &pte);
	if (ret)
		goto out_unlock;

	if (pasid_pte_is_present(pte)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	__pasid_setup_sl(iommu, pte, param->domain_pgd_gpa, param->did,
			param->domain_agaw, param->dirty_tracking);

out_unlock:
	pkvm_spin_unlock(&hyp_iommu->lock);

	if (!ret)
		pkvm_pasid_flush_caches(hyp_iommu, pte, param->pasid, param->did);

	return ret;
}

int pkvm_iommu_set_pasid_sl(u64 param_va)
{
	struct pkvm_pasid_table_param *param;
	struct pkvm_iommu *hyp_iommu;

	if (!param_va)
		return -EINVAL;

	param = (struct pkvm_pasid_table_param *)kern_pkvm_va((void *)param_va);
	if (WARN_ON_ONCE(param != this_pv_param(pasid_table_param)))
		return -EINVAL;

	hyp_iommu = find_iommu_by_reg_phys(param->phys);
	if (!hyp_iommu)
		return -EINVAL;

	if (param->did == FLPT_DEFAULT_DID) {
		/*
		 * Passthrough will break pkvm security guarantees as
		 * device would be able to access the whole physical
		 * memory range. Use Second stage translation with host ept
		 * as second stage pagetable so as to limit device access
		 * to host memory..
		 */
		int level = pkvm_host_ept_level();

		param->domain_agaw = (level == 3) ? 1 :
					(level == 4) ? 2 : 3;
		param->domain_pgd_gpa = pkvm_host_ept_pgd();
	} else {
		param->domain_pgd_gpa = host_gpa2hpa(param->domain_pgd_gpa);
	}
	pkvm_dbg("pkvm: %s: setup_sl: dev[%x] pasid: %x, did: %x, slptr: %llx\n",
			__func__, param->bdf, param->pasid,
			param->did, param->domain_pgd_gpa);
	return pasid_setup_sl(hyp_iommu, param);
}
