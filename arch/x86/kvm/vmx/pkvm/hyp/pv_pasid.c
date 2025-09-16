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
#include "ptdev.h"
#include "iommu_spgt.h"
#include "bug.h"
#include "iommu.h"

static void __pasid_setup_sl(struct intel_iommu *iommu, struct pasid_entry *pe, u64 pgd_gpa,
		u16 did, u8 agaw, bool dirty_tracking)
{
	pasid_clear_entry(pe);
	pasid_set_domain_id(pe, did);
	pasid_set_slptr(pe, pgd_gpa);
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
	u8 flpm, agaw;

	pgtt = pasid_get_translation_type(pe);
	if (pgtt < 1 || pgtt > 4) {
		pkvm_err("pkvm: %s: invalid PGTT in pasid entry!\n", __func__);
		return -1;
	}

	agaw = pasid_get_address_width(pe);
	if (agaw < 1 || agaw > 3) {
		pkvm_err("pkvm: %s: invalid agaw(%d) in the pasid entry!\n", __func__, agaw);
		return -1;
	}

	did = pasid_get_domain_id(pe);
	if (did > ndoms) {
		pkvm_err("pkvm: %s: did(%u) greater than iommu max supported domains(%u)!\n",
				__func__, did, ndoms);
		return -1;
	}

	flpm = pasid_get_flpm(pe);
	if (flpm > 1) {
		pkvm_err("pkvm: %s: invalid FSPM(%d) in the pasid entry!\n", __func__, flpm);
		return -1;
	}

	slptr = pasid_get_slptr(pe);
	flptr = pasid_get_flptr(pe);
	if (pgtt == PASID_ENTRY_PGTT_FL_ONLY) {
		if (slptr) {
			pkvm_err("pkvm: %s: SLPTR set with PASID_ENTRY_PGTT_FL_ONLY!\n", __func__);
			return -1;
		} else if (!flptr) {
			pkvm_err("pkvm: %s: FLPTR unset with PASID_ENTRY_PGTT_FL_ONLY!\n", __func__);
			return -1;
		}
	} else if (pgtt == PASID_ENTRY_PGTT_SL_ONLY) {
		if (flptr) {
			pkvm_err("pkvm: %s: FLPTR set with PASID_ENTRY_PGTT_SL_ONLY!\n", __func__);
			return -1;
		} else if (!slptr) {
			pkvm_err("pkvm: %s: SLPTR unset with PASID_ENTRY_PGTT_SL_ONLY!\n", __func__);
			return -1;
		}
	} else if (pgtt == PASID_ENTRY_PGTT_NESTED) {
		if (!flptr || !slptr) {
			pkvm_err("pkvm: %s: SLPTR or FLPTR unset with PASID_ENTRY_PGTT_NESTED!\n", __func__);
			return -1;
		}
	} else {
		int level = pkvm_host_ept_level();
		u8 ept_agaw = (level == 3) ? 1 : (level == 4) ? 2 : 3;

		if (flptr || slptr) {
			pkvm_err("pkvm: %s: SLPTR or FLPTR set with PASID_ENTRY_PGTT_PT!\n", __func__);
			return -1;
		}

		pkvm_dbg("pkvm: %s: converting PASID_ENTRY_PGTT_PT to PASID_ENTRY_PGTT_SL_ONLY!\n", __func__);
		__pasid_setup_sl(&iommu->iommu, pe, pkvm_host_ept_pgd(), did, ept_agaw, false);
	}

	pkvm_dbg("pkvm: %s: PGTT: %x, agaw: %x, did: %x, flpm: %x, flptr: %llx, slptr: %llx\n",
		__func__, pgtt, agaw, did, flpm, flptr, slptr);

	return 0;
}

static int validate_pasid_entries(struct pkvm_iommu *iommu, u32 pasid_start,
		u16 bdf, struct pasid_entry *pe, struct pkvm_ptdev **ptdev)
{
	int pe_idx;

	for (pe_idx = 0; pe_idx < PASIDTAB_ENTRIES; pe_idx++, pe++) {
		struct ptdev_info *ptdev_info;

		if (!pasid_pte_is_present(pe))
			continue;

		ptdev_info = iommu_add_ptdev(iommu, bdf, pasid_start + pe_idx);
		if (!ptdev_info) {
			pkvm_dbg("pkvm: %s: Unable to instantiate ptdev for device!\n", __func__);
			return -1;
		}

		if (!*ptdev) {
			*ptdev = ptdev_info->ptdev;
		} else if (*ptdev != ptdev_info->ptdev) {
			iommu_del_ptdev(iommu, ptdev_info);
			pkvm_err("pkvm: %s: ptdev mismatch between different pasids!\n", __func__);
			return -1;
		}

		if (validate_pasid_entry(iommu, bdf, pe)) {
			iommu_del_ptdev(iommu, ptdev_info);
			return -1;
		}
	}

	return 0;
}

static int validate_pasid_dir(struct pkvm_iommu *iommu, u16 bdf, u32 nr_pdes,
			struct pasid_dir_entry *pde, struct pkvm_ptdev **ptdev)
{
	int pde_index, ret;

	for (pde_index = 0; pde_index < nr_pdes; pde_index++, pde++) {
		struct pasid_entry *pe;

		pe = get_pasid_table_from_pde(pde);
		if (!pe)
			continue;

		pkvm_dbg("pkvm: %s: pdeval[%d]: %llx, pte: %p\n", __func__, pde_index, pde->val, pe);
		ret = validate_pasid_entries(iommu, pde_index << PASIDDIR_SHIFT, bdf, pe, ptdev);
		if (ret) {
			pkvm_err("pkvm: %s: Failed to validate pasid table entry\n", __func__);
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
		struct pkvm_ptdev *ptdev = NULL;
		struct pasid_dir_entry *pde;
		u8 devfn = upper ? 128 + ce_idx : ce_idx;
		u16 bdf = PCI_DEVID(bus, devfn);
		u32 nr_pdes;

		if (!context_present(ce))
			continue;

		if (ce[1].hi || ce[1].lo) {
			pkvm_dbg("pkvm: %s: device [%x:%x:%x], upper bits of context entry not zero!\n",
					__func__, bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
			return -1;
		}

		pde = pkvm_phys_to_virt(ce->lo & VTD_PAGE_MASK);
		nr_pdes = get_pasid_dir_size(ce);
		pkvm_dbg("pkvm: %s: device [%x:%x:%x]: pasid_table[size=%u]=%p CE: [%llx:%llx:%llx:%llx]\n",
				__func__, bus, PCI_SLOT(devfn), PCI_FUNC(devfn),
				nr_pdes << PASIDDIR_SHIFT, pde,
				ce[1].hi, ce[1].lo, ce[0].hi, ce[0].lo);

		ret = validate_pasid_dir(iommu, bdf, nr_pdes, pde, &ptdev);
		if (ret || !ptdev) {
			pkvm_dbg("pkvm: %s: Failed to instantiate ptdev for device[%x:%x:%x]\n",
					__func__, bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
			iommu_del_ptdevs(iommu, bdf);
			return -1;
		}
		ptdev->pasid_table = pde;
		ptdev->max_pasid = nr_pdes << PASIDDIR_SHIFT;
	}

	return 0;
}

