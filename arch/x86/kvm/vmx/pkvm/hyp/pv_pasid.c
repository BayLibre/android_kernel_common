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
