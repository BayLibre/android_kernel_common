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

static int pkvm_pasid_get_entry(struct intel_iommu *iommu, struct ptdev_info *ptdev_info,
		u64 *ptable_gpa, struct pasid_entry **pte)
{
	struct pkvm_ptdev *ptdev;
	struct pasid_dir_entry *dir;
	struct pasid_entry *entries;
	int dir_index, index;
	u8 bus, devfn;

	ptdev = ptdev_info->ptdev;
	bus = PCI_BUS_NUM(ptdev->bdf);
	devfn = PCI_DEV_FN(ptdev->bdf);
	if (!ptdev->pasid_table || ptdev_info->pasid >= ptdev->max_pasid) {
		pkvm_err("pkvm: %s: unexpected state in pas_table for device[%x:%x:%x]: ptable=%p, max_pasid=%u\n",
				__func__, bus, PCI_SLOT(devfn), PCI_FUNC(devfn), ptdev->pasid_table, ptdev->max_pasid);
		return -EINVAL;
	}


	dir = ptdev->pasid_table;
	dir_index = ptdev_info->pasid >> PASIDDIR_SHIFT;
	index = ptdev_info->pasid & PASID_PTE_MASK;

retry:
	entries = get_pasid_table_from_pde(&dir[dir_index]);
	if (!entries) {
		u64 tmp;
		u64 ptable_hpa;

		if (!ptable_gpa)
			return -ENOMEM;

		ptable_hpa = host_gpa2hpa(*ptable_gpa);
		entries = host_gpa2hva(*ptable_gpa);

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
			iommu_flush_cache(entries, VTD_PAGE_SIZE);
			iommu_flush_cache(&dir[dir_index].val, sizeof(*dir));
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
		iommu_flush_cache(pte, sizeof(*pte));

	if (cap_caching_mode(iommu->cap)) {
		flush_pasid_cache(hyp_iommu, did, QI_PC_PASID_SEL, pasid);
		flush_piotlb(hyp_iommu, did, pasid, 0, -1, 0);
	} else {
		flush_write_buffer(hyp_iommu);
	}
}

int pkvm_iommu_clear_pasid_entry(u64 phys, u64 param_gpa)
{
	struct pkvm_iommu *hyp_iommu = find_iommu_by_reg_phys(phys);
	struct pkvm_block_translation_param *param;
	struct pasid_entry *pte;
	struct intel_iommu *iommu;
	struct ptdev_info *ptdev_info;
	u16 did, pgtt;
	int ret = -ENODEV;

	if (!hyp_iommu)
		return -EINVAL;

	param = host_gpa2hva(param_gpa);
	if (!param)
		return -EINVAL;

	pkvm_spin_lock(&hyp_iommu->lock);
	iommu = &hyp_iommu->iommu;

	ptdev_info = iommu_find_ptdev(hyp_iommu, param->bdf, param->pasid);
	if (!ptdev_info) {
		pkvm_err("pkvm: %s: failed to locate ptdev for device[%x:%x:%x]\n",
				__func__, PCI_BUS_NUM(param->bdf), PCI_SLOT(PCI_DEV_FN(param->bdf)), PCI_FUNC(PCI_DEV_FN(param->bdf)));
		goto out_unlock;
	}
	pkvm_info("pkvm: %s: ptdev_info for device[%x:%x:%x]\n",
			__func__, PCI_BUS_NUM(param->bdf),
			PCI_SLOT(PCI_DEV_FN(param->bdf)), PCI_FUNC(PCI_DEV_FN(param->bdf)));

	ret = pkvm_pasid_get_entry(iommu, ptdev_info, NULL, &pte);
	if (ret) {
		pkvm_dbg("pkvm: %s: failed to get pasid table entry for device[%x:%x:%x], err=%d\n",
				__func__, PCI_BUS_NUM(param->bdf),
				PCI_SLOT(PCI_DEV_FN(param->bdf)), PCI_FUNC(PCI_DEV_FN(param->bdf)), ret);
		goto out_unlock;
	}
	if (!pasid_pte_is_present(pte)) {
		pkvm_err("pkvm: %s: pte for teardown not present!\n", __func__);
		ret = -ENODEV;
		goto out_unlock;
	}

	did = pasid_get_domain_id(pte);
	pgtt = pasid_get_translation_type(pte);
	pasid_clear_entry(pte);
	iommu_del_ptdev(hyp_iommu, ptdev_info);
	ret = 0;

out_unlock:
	pkvm_spin_unlock(&hyp_iommu->lock);

	if (ret)
		return ret;

	if (!iommu_coherency(iommu))
		iommu_flush_cache(pte, sizeof(*pte));

	flush_pasid_cache(hyp_iommu, did, QI_PC_PASID_SEL, param->pasid);

	if (pgtt == PASID_ENTRY_PGTT_PT || pgtt == PASID_ENTRY_PGTT_FL_ONLY)
		flush_piotlb(hyp_iommu, did, param->pasid, 0, -1, 0);
	else
		flush_iotlb(hyp_iommu, did, 0, 0, DMA_TLB_DSI_FLUSH);

	return 0;

}

/*
 * Set up the scalable mode pasid table entry for first only
 * translation type.
 */
int pkvm_iommu_pasid_setup_fl(u64 phys, u64 param_gpa)
{
	struct pkvm_iommu *hyp_iommu = find_iommu_by_reg_phys(phys);
	struct pkvm_pasid_table_param *param;
	struct intel_iommu *iommu;
	struct ptdev_info *ptdev_info = NULL;
	struct pkvm_ptdev *ptdev;
	struct pasid_entry *pte;
	int ret = -EINVAL;

	if (!hyp_iommu)
		return -EINVAL;

	param = host_gpa2hva(param_gpa);
	if (!param)
		return -EINVAL;

	pkvm_spin_lock(&hyp_iommu->lock);
	iommu = &hyp_iommu->iommu;

	if (!ecap_flts(iommu->ecap)) {
		pr_err("pkvm: %s: No first level translation support on iommu%d\n",
		       __func__, iommu->seq_id);
		goto out_unlock;
	}

	ptdev_info = iommu_add_ptdev(hyp_iommu, param->bdf, param->pasid);
	if (!ptdev_info) {
		pkvm_err("pkvm: %s: ptdev_info not found for device[%x:%x:%x]\n",
				__func__, PCI_BUS_NUM(param->bdf),
				PCI_SLOT(PCI_DEV_FN(param->bdf)), PCI_FUNC(PCI_DEV_FN(param->bdf)));
		ret = -EFAULT;
		goto out_unlock;
	}
	ptdev = ptdev_info->ptdev;
	if (!ptdev->pasid_table) {
		ptdev->pasid_table = host_gpa2hva(param->pasid_dir_gpa);
		ptdev->max_pasid = param->max_pasid;
	} else if (ptdev->pasid_table != host_gpa2hva(param->pasid_dir_gpa)) {
		pkvm_err("pkvm: %s: Invalid pasid_table for device!\n", __func__);
		ret = -EINVAL;
		goto out_unlock;
	}

	pkvm_info("pkvm: %s: ptdev_info for device[%x:%x:%x]\n",
			__func__, PCI_BUS_NUM(param->bdf),
			PCI_SLOT(PCI_DEV_FN(param->bdf)), PCI_FUNC(PCI_DEV_FN(param->bdf)));
	ret = pkvm_pasid_get_entry(iommu, ptdev_info, &param->pasid_table_gpa, &pte);
	if (ret) {
		pkvm_dbg("pkvm: %s: failed to get pasid table entry for device[%x:%x:%x], err=%d\n",
				__func__, PCI_BUS_NUM(param->bdf),
				PCI_SLOT(PCI_DEV_FN(param->bdf)), PCI_FUNC(PCI_DEV_FN(param->bdf)), ret);
		goto out_unlock;
	}

	if (pasid_pte_is_present(pte)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	pasid_clear_entry(pte);

	/* Setup the first level page table pointer: */
	pasid_set_flptr(pte, param->domain_pgd_gpa);

	if (agaw_to_level(iommu->agaw) == 5 && cap_fl5lp_support(iommu->cap))
		pasid_set_flpm(pte, 1);

	if (param->force_snooping)
		pasid_set_pgsnp(pte);

	pasid_set_domain_id(pte, param->did);
	pasid_set_address_width(pte, iommu->agaw);
	pasid_set_page_snoop(pte, !!ecap_smpwc(iommu->ecap));

	/* Setup Present and PASID Granular Transfer Type: */
	pasid_set_translation_type(pte, PASID_ENTRY_PGTT_FL_ONLY);
	pasid_set_present(pte);

	if (validate_pasid_entry(hyp_iommu, param->bdf, pte))
		ret = -EINVAL;

out_unlock:
	if (ret && ptdev_info)
		iommu_del_ptdev(hyp_iommu, ptdev_info);

	pkvm_spin_unlock(&hyp_iommu->lock);

	if (!ret)
		pkvm_pasid_flush_caches(hyp_iommu, pte, param->pasid, param->did);

	return ret;
}

static int pasid_setup_sl(struct pkvm_iommu *hyp_iommu, struct pkvm_pasid_table_param *param)
{
	struct intel_iommu *iommu;
	struct ptdev_info *ptdev_info = NULL;
	struct pkvm_ptdev *ptdev;
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
		pkvm_err("pkvm: %s: Invalid domain page table\n", __func__);
		goto out_unlock;
	}

	ptdev_info = iommu_add_ptdev(hyp_iommu, param->bdf, param->pasid);
	if (!ptdev_info) {
		pkvm_err("pkvm: %s: failed to locate ptdev for device[%x:%x:%x]\n",
				__func__, PCI_BUS_NUM(param->bdf), PCI_SLOT(PCI_DEV_FN(param->bdf)), PCI_FUNC(PCI_DEV_FN(param->bdf)));
		ret = -ENODEV;
		goto out_unlock;
	}
	ptdev = ptdev_info->ptdev;
	if (!ptdev->pasid_table) {
		ptdev->pasid_table = host_gpa2hva(param->pasid_dir_gpa);
		ptdev->max_pasid = param->max_pasid;
	} else if (ptdev->pasid_table != host_gpa2hva(param->pasid_dir_gpa)) {
		pkvm_err("pkvm: %s: Invalid pasid_table for device!\n", __func__);
		ret = -EINVAL;
		goto out_unlock;
	}
	pkvm_info("pkvm: %s: ptdev_info for device[%x:%x:%x]\n",
			__func__, PCI_BUS_NUM(param->bdf),
			PCI_SLOT(PCI_DEV_FN(param->bdf)), PCI_FUNC(PCI_DEV_FN(param->bdf)));

	ret = pkvm_pasid_get_entry(iommu, ptdev_info, &param->pasid_table_gpa, &pte);
	if (ret) {
		pkvm_dbg("pkvm: %s: failed to get pasid table entry for device[%x:%x:%x], err=%d\n",
				__func__, PCI_BUS_NUM(param->bdf),
				PCI_SLOT(PCI_DEV_FN(param->bdf)), PCI_FUNC(PCI_DEV_FN(param->bdf)), ret);
		goto out_unlock;
	}

	if (pasid_pte_is_present(pte)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	__pasid_setup_sl(iommu, pte, param->domain_pgd_gpa, param->did,
			param->domain_agaw, param->dirty_tracking);

	if (validate_pasid_entry(hyp_iommu, param->bdf, pte))
		ret = -EINVAL;

out_unlock:
	if (ret && ptdev_info)
		iommu_del_ptdev(hyp_iommu, ptdev_info);

	pkvm_spin_unlock(&hyp_iommu->lock);

	if (!ret)
		pkvm_pasid_flush_caches(hyp_iommu, pte, param->pasid, param->did);

	return ret;
}

int pkvm_iommu_pasid_setup_sl(u64 phys, u64 param_gpa)
{
	struct pkvm_iommu *hyp_iommu = find_iommu_by_reg_phys(phys);
	struct pkvm_pasid_table_param *param;

	if (!hyp_iommu)
		return -EINVAL;

	param = host_gpa2hva(param_gpa);
	if (!param)
		return -EINVAL;

	param->domain_pgd_gpa = host_gpa2hpa(param->domain_pgd_gpa);
	return pasid_setup_sl(hyp_iommu, param);
}

int pkvm_iommu_pasid_setup_pt(u64 phys, u64 param_gpa)
{
	struct pkvm_iommu *hyp_iommu = find_iommu_by_reg_phys(phys);
	struct pkvm_pasid_table_param *param;
	int level = pkvm_host_ept_level();

	if (!hyp_iommu)
		return -EINVAL;

	param = host_gpa2hva(param_gpa);
	if (!param)
		return -EINVAL;

	/*
	 * pkvm should convert the passthrough request to a mapped one
	 * to protect devices from protected memory. So we use the host
	 * ept as the second level page table.
	 */
	param->domain_agaw = (level == 3) ? 1 :
				(level == 4) ? 2 : 3;
	param->domain_pgd_gpa = pkvm_host_ept_pgd();
	return pasid_setup_sl(hyp_iommu, param);

}
