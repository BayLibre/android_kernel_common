// SPDX-License-Identifier: GPL-2.0-only
/*
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

int initialize_iommu_pgt(struct pkvm_iommu *iommu)
{
	struct pkvm_pgtable *pgt = &iommu->pgt;
	struct viommu_reg *vreg = &iommu->viommu.vreg;

	if (!vreg->rta) {
		pkvm_err("pkvm: %s: iommu%d: host RTADDR_REG not set",
				__func__, iommu->iommu.seq_id);
		return -EINVAL;
	}

	pgt->root_pa = vreg->rta & VTD_PAGE_MASK;

	return 0;
}

static void __set_lm_context(struct context_entry *context, u16 did, u8 aw, u8 tt, u64 slptr)
{
	context_clear_entry(context);

	context_set_domain_id(context, did);

	context_set_address_root(context, slptr);
	context_set_address_width(context, aw);
	context_set_translation_type(context, tt);
	context_set_fault_enable(context);
	context_set_present(context);
}

static int validate_lm_context_entry(struct pkvm_iommu *hyp_iommu, u16 bdf,
					struct context_entry *ce)
{
	struct intel_iommu *iommu = &hyp_iommu->iommu;
	u16 ndoms = cap_ndoms(iommu->cap);
	u64 slptr;
	u16 did;
	u8 tt, aw;

	aw = context_lm_get_aw(ce);
	if (aw < 1 || aw > 3) {
		pkvm_err("pkvm: %s: unsupported aw(%d)!\n", __func__, aw);
		return -1;
	} else if (aw != iommu->agaw) {
		pkvm_err("pkvm: %s: aw mismatch CE(%u) != iommu aw(%u)!\n",
				__func__, aw, iommu->agaw);
		return -1;
	}

	did = context_lm_get_did(ce);
	if (did > ndoms) {
		pkvm_err("pkvm: %s: did(%u) > iommu max supported domains(%u)!\n",
				__func__, did, ndoms);
		return -1;
	}

	slptr = context_lm_get_slptr(ce);
	tt = context_lm_get_tt(ce);
	if (tt > 2) {
		pkvm_err("pkvm: %s: unsupported tt(%u)!\n", __func__, tt);
		return -1;
	} else if ((tt == CONTEXT_TT_MULTI_LEVEL || tt == CONTEXT_TT_DEV_IOTLB) && !slptr) {
		pkvm_err("pkvm: %s: SLPTR not set in DMA mode\n", __func__);
		return -1;
	}

	if (tt == CONTEXT_TT_PASS_THROUGH) {
		/*
		 * Passthrough will break pkvm security guarantees as
		 * device would be able to access the whole physical
		 * memory range. Use Second stage translation with host ept
		 * as second stage pagetable so as to limit device access
		 * to host memory.
		 */
		int level = pkvm_host_ept_level();

		aw = (level == 3) ? 1 : (level == 4) ? 2 : 3;

		slptr = pkvm_host_ept_pgd();
		if (sm_supported(iommu) && is_dev_in_satc(bdf))
			tt = CONTEXT_TT_DEV_IOTLB;
		else
			tt = CONTEXT_TT_MULTI_LEVEL;
	}

	/*
	 * Explicitly update the context entry to guarantee that
	 * only needed and validated fields are set.
	 */
	__set_lm_context(ce, did, aw, tt, slptr);

	return 0;
}

static int validate_lm_context_entries(struct pkvm_iommu *hyp_iommu,
		u8 bus, struct context_entry *context)
{

	for (int devfn = 0; devfn < 256; devfn++) {
		struct context_entry *ce = &context[devfn];
		int ret;

		if (!context_present(ce))
			continue;

		pkvm_dbg("pkvm: %s: [LEGACY]: dev[%x], CE: %llx:%llx\n",
				__func__, PCI_DEVID(bus, devfn), ce->hi, ce->lo);
		ret = validate_lm_context_entry(hyp_iommu, PCI_DEVID(bus, devfn), ce);
		if (ret)
			return -1;
	}
	return 0;
}

static int validate_translation_tables(struct pkvm_iommu *hyp_iommu, struct root_entry *root)
{
	struct intel_iommu *iommu = &hyp_iommu->iommu;
	bool sm_supported = sm_supported(iommu);

	for (int bus = 0; bus < 256; bus++) {
		struct root_entry *rte = &root[bus];
		struct context_entry *context;
		int ret = 0;

		if (!sm_supported) {
			if (rte->lo & 1) {
				pkvm_dbg("pkvm: %s: bus: %d, [LEGACY]: parsing context table at hpa: %llx\n",
					__func__, bus, rte->lo & VTD_PAGE_MASK);
				context = host_gpa2hva(rte->lo & VTD_PAGE_MASK);
				ret = validate_lm_context_entries(hyp_iommu, bus, context);
			}
		} else {
			if (rte->lo & 1) {
				pkvm_dbg("pkvm: %s: bus: %d, [SM]: parsing lower context table at hpa: %llx\n",
					__func__, bus, rte->lo & VTD_PAGE_MASK);
				context = host_gpa2hva(rte->lo & VTD_PAGE_MASK);
				ret = validate_sm_context_entries(hyp_iommu, bus, context, false);
			}
			if (rte->hi & 1) {
				pkvm_dbg("pkvm: %s: bus: %d, [SM]: parsing upper context table at hpa: %llx\n",
					__func__, bus, rte->hi & VTD_PAGE_MASK);
				context = host_gpa2hva(rte->hi & VTD_PAGE_MASK);
				ret = validate_sm_context_entries(hyp_iommu, bus, context, true);
			}
		}

		if (ret)
			return ret;
	}

	return 0;
}

unsigned long pkvm_iommu_enable(u64 phys, u64 rta_gpa)
{
	struct pkvm_iommu *iommu = find_iommu_by_reg_phys(phys);
	struct root_entry *root = host_gpa2hva(rta_gpa);
	int ret;

	if (!iommu)
		return -EINVAL;

	pkvm_spin_lock(&iommu->lock);
	if (iommu->activated) {
		pkvm_err("pkvm: %s: iommu%d already activated!\n",
				__func__, iommu->iommu.seq_id);
		ret = -EPERM;
		goto out;
	}

	ret = validate_translation_tables(iommu, root);
	if (ret) {
		pkvm_err("pkvm: %s: failed to validate host translation tables!\n",
				__func__);
		goto out;
	}

	iommu->viommu.vreg.rta = virt_to_phys(root);
	ret = initialize_iommu_pgt(iommu);
	if (ret)
		goto out;

	pkvm_dbg("pkvm: %s: enabling iommu, root_pa = %llx\n", __func__, rta_gpa);
	ret = enable_translation(iommu);
	if (ret)
		goto out;

	flush_context_cache(iommu, 0, 0, 0, DMA_CCMD_GLOBAL_INVL);
	if (sm_supported(&iommu->iommu))
		flush_pasid_cache(iommu, 0, QI_PC_GLOBAL, 0);
	flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH);

out:
	pkvm_spin_unlock(&iommu->lock);
	return ret;
}

unsigned long pkvm_iommu_disable(u64 phys)
{
	struct pkvm_iommu *iommu = find_iommu_by_reg_phys(phys);

	if (!iommu)
		return -EINVAL;

	pkvm_spin_lock(&iommu->lock);
	disable_translation(iommu);
	flush_context_cache(iommu, 0, 0, 0, DMA_CCMD_GLOBAL_INVL);
	if (sm_supported(&iommu->iommu))
		flush_pasid_cache(iommu, 0, QI_PC_GLOBAL, 0);
	flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH);

	root_tbl_walk(iommu);
	pkvm_spin_unlock(&iommu->lock);

	return 0;
}
