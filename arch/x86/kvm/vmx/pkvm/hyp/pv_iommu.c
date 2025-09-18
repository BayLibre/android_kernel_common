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
#include "ptdev.h"
#include "iommu_spgt.h"
#include "bug.h"
#include "iommu.h"

int initialize_iommu_pgt(struct pkvm_iommu *iommu)
{
	struct pkvm_pgtable *pgt = &iommu->pgt;
	struct viommu_reg *vreg = &iommu->viommu.vreg;

	if (!vreg->rta) {
		pkvm_err("pkvm: %s: iommu%d: host RTADDR_REG not set", __func__, iommu->iommu.seq_id);
		return -EINVAL;
	}

	pgt->root_pa = vreg->rta & VTD_PAGE_MASK;
	iommu->iommu.root_entry = pkvm_phys_to_virt(pgt->root_pa);

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

static int validate_lm_context_entry(struct pkvm_iommu *hyp_iommu, u16 bdf, struct context_entry *ce)
{
	struct intel_iommu *iommu = &hyp_iommu->iommu;
	struct ptdev_info *ptdev_info = NULL;
	u16 ndoms = cap_ndoms(iommu->cap);
	u64 slptr;
	u16 did;
	u8 tt, aw;

	pkvm_dbg("pkvm: %s: LEGACY_CE: [%llx:%llx]\n", __func__, ce->lo, ce->hi);
	aw = context_lm_get_aw(ce);
	if (aw < 1 || aw > 3) {
		pkvm_err("pkvm: %s: unsupported aw(%d) in context entry!\n", __func__, aw);
		return -1;
	} else if (aw != iommu->agaw) {
		pkvm_err("pkvm: %s: aw(%d) in context entry doesn't match iommu agaw(%u)!\n",
				__func__, aw, iommu->agaw);
		return -1;
	}

	did = context_lm_get_did(ce);
	if (did > ndoms) {
		pkvm_err("pkvm: %s: did(%u) greater than iommu max supported domains(%u)!\n",
				__func__, did, ndoms);
		return -1;
	}

	slptr = context_lm_get_slptr(ce);
	tt = context_lm_get_tt(ce);
	if (tt > 2) {
		pkvm_err("pkvm: %s: unsupported tt(%u) in context entry!\n", __func__, tt);
		return -1;
	} else if ((tt == CONTEXT_TT_MULTI_LEVEL || tt == CONTEXT_TT_DEV_IOTLB) && !slptr) {
		pkvm_err("pkvm: %s: SLPTR not set with DMA mode\n", __func__);
		return -1;
	}

	ptdev_info = iommu_add_ptdev(hyp_iommu, bdf, 0);
	if (!ptdev_info) {
		pkvm_dbg("pkvm: %s: Unable to instantiate ptdev for device!\n", __func__);
		return -1;
	}

	if (tt == CONTEXT_TT_PASS_THROUGH) {
		int level = pkvm_host_ept_level();
		aw = (level == 3) ? 1 : (level == 4) ? 2 : 3;

		/*
		 * Switch to DMA mode if Passthrough specified.
		 */
		if (pkvm_sm_supported(iommu) && is_dev_in_satc(bdf))
			tt = CONTEXT_TT_DEV_IOTLB;
		else
			tt = CONTEXT_TT_MULTI_LEVEL;
		__set_lm_context(ce, did, aw, tt, pkvm_host_ept_pgd());
	}

	return 0;
}

static int validate_lm_context_entries(struct pkvm_iommu *hyp_iommu,
		u8 bus, struct context_entry *context)
{

	pkvm_dbg("pkvm: %s: write protecting lm context table: %llx\n", __func__, pkvm_virt_to_phys(context));
	if (pkvm_switch_host_ept_ro(pkvm_virt_to_phys(context), VTD_PAGE_SIZE)) {
		pkvm_err("pkvm: %s: failed to write protect lm context table Page!\n", __func__);
		return -EFAULT;
	}

	for (int devfn = 0; devfn < 256; devfn++) {
		struct context_entry *ce = &context[devfn];
		int ret;

		if (!context_present(ce))
			continue;

		ret = validate_lm_context_entry(hyp_iommu, PCI_DEVID(bus, devfn), ce);
		if (ret)
			return -1;
	}
	return 0;
}

static int validate_translation_tables(struct pkvm_iommu *hyp_iommu, struct root_entry *root)
{
	struct intel_iommu *iommu = &hyp_iommu->iommu;
	bool sm_supported = pkvm_sm_supported(iommu);

	pkvm_dbg("pkvm: %s: write protecting rta: %llx\n", __func__, pkvm_virt_to_phys(root));
	if (pkvm_switch_host_ept_ro(pkvm_virt_to_phys(root), VTD_PAGE_SIZE)) {
		pkvm_err("pkvm: %s: failed to write protect Root Table Page!\n", __func__);
		return -EFAULT;
	}

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
		pkvm_err("pkvm: %s: iommu%d already activated!\n", __func__, iommu->iommu.seq_id);
		ret = -EPERM;
		goto out;
	}

	ret = validate_translation_tables(iommu, root);
	if (ret) {
		pkvm_err("pkvm: %s: Failed to validate host translation tables!\n", __func__);
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
	if (ecap_smts(iommu->iommu.ecap))
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
	if (ecap_smts(iommu->iommu.ecap))
		flush_pasid_cache(iommu, 0, QI_PC_GLOBAL, 0);
	flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH);

	root_tbl_walk(iommu);
	pkvm_spin_unlock(&iommu->lock);

	return 0;
}

struct context_entry *pkvm_iommu_context_addr(struct intel_iommu *iommu, u8 bus,
					 u8 devfn, u64 context_phys)
{
	struct root_entry *root = &iommu->root_entry[bus];
	struct context_entry *context;
	u64 *entry;

	entry = &root->lo;
	if (pkvm_sm_supported(iommu)) {
		if (devfn >= 0x80) {
			devfn -= 0x80;
			entry = &root->hi;
		}
		devfn *= 2;
	}
	if (*entry & 1)
		context = pkvm_phys_to_virt(*entry & VTD_PAGE_MASK);
	else {
		unsigned long phy_addr;
		if (!context_phys)
			return NULL;

		context = (struct context_entry *)host_gpa2hva(context_phys);
		if (!context)
			return NULL;

		pkvm_dbg("pkvm: %s: write protecting lm context table: %llx\n", __func__, pkvm_virt_to_phys(context));
		if (pkvm_switch_host_ept_ro(pkvm_virt_to_phys(context), VTD_PAGE_SIZE)) {
			pkvm_err("pkvm: %s: failed to write protect lm context table Page!\n", __func__);
			return NULL;
		}
		memset(context, 0, VTD_PAGE_SIZE);

		if (!iommu_coherency(iommu))
			iommu_flush_cache((void *)context, VTD_PAGE_SIZE);
		phy_addr = virt_to_phys((void *)context);
		*entry = phy_addr | 1;
		if (!iommu_coherency(iommu))
			iommu_flush_cache(entry, sizeof(*entry));
	}
	return &context[devfn];
}

unsigned long pkvm_iommu_clear_ce(u64 phys, u64 param_gpa)
{
	struct pkvm_iommu *hyp_iommu = find_iommu_by_reg_phys(phys);
	struct pkvm_block_translation_param *param;
	struct context_entry *context;

	if (!hyp_iommu)
		return -EINVAL;

	param = host_gpa2hva(param_gpa);
	if (!param)
		return -EINVAL;

	pkvm_spin_lock(&hyp_iommu->lock);
	context = pkvm_iommu_context_addr(&hyp_iommu->iommu,
			PCI_BUS_NUM(param->bdf), PCI_DEV_FN(param->bdf), 0);

	if (!context)
		goto out;

	/*
	 * In legacy mode, ptdevs are created during context entry creation and we need
	 * to delete the ptdev during context entry teardown.
	 */
	if (!pkvm_sm_supported(&hyp_iommu->iommu)) {
		struct ptdev_info *ptdev_info = iommu_find_ptdev(hyp_iommu, param->bdf, 0);
		if (ptdev_info) {
			iommu_del_ptdev(hyp_iommu, ptdev_info);
		} else {
			pkvm_err("pkvm: %s: unable to locate ptdev for device[%x:%x:%x] during context clear!\n",
				__func__, PCI_BUS_NUM(param->bdf), PCI_SLOT(PCI_DEV_FN(param->bdf)),
				PCI_FUNC(PCI_DEV_FN(param->bdf)));
		}
	}

	/*
	 * Pass the did back to host for flushing.
	 */
	param->did = context_domain_id(context);
	pkvm_dbg("pkvm: %s: clear context mapping for did: %d\n", __func__, param->did);
	context_clear_entry(context);
	if (!iommu_coherency(&hyp_iommu->iommu))
		iommu_flush_cache(context, sizeof(*context));
out:
	pkvm_spin_unlock(&hyp_iommu->lock);
	return 0;
}

static void pkvm_context_present_cache_flush(struct pkvm_iommu *iommu, u16 bdf, u16 did)
{
	if (cap_caching_mode(iommu->iommu.cap)) {
		flush_context_cache(iommu, 0, bdf, DMA_CCMD_MASK_NOBIT, DMA_CCMD_DEVICE_INVL);
		flush_iotlb(iommu, did, 0, 0, DMA_TLB_DSI_FLUSH);
	} else {
		flush_write_buffer(iommu);
	}
}

unsigned long set_context_entry(struct pkvm_iommu *hyp_iommu, struct pkvm_context_param *param)
{
	struct intel_iommu *iommu = &hyp_iommu->iommu;
	u8 bus = PCI_BUS_NUM(param->bdf);
	u8 devfn = PCI_DEV_FN(param->bdf);
	struct context_entry *context;
	int ret = -ENOMEM;
	int tt;

	pkvm_dbg("pkvm: %s: Set lm context mapping for %02x:%02x.%d, pgd: %llx\n",
		__func__, bus, PCI_SLOT(devfn), PCI_FUNC(devfn), param->domain_pgd_gpa);

	pkvm_spin_lock(&hyp_iommu->lock);

	context = pkvm_iommu_context_addr(iommu, bus, devfn, param->context_gpa);
	if (!context)
		goto out_unlock;

	if (context_present(context)) {
		ret = 0;
		goto out_unlock;
	}

	if (pkvm_sm_supported(iommu) && is_dev_in_satc(param->bdf))
		tt = CONTEXT_TT_DEV_IOTLB;
	else
		tt = CONTEXT_TT_MULTI_LEVEL;

	__set_lm_context(context, param->did, param->domain_agaw,
			tt, param->domain_pgd_gpa);

	ret = validate_lm_context_entry(hyp_iommu, param->bdf, context);
	if (ret)
		goto out_unlock;

	if (!iommu_coherency(iommu))
		iommu_flush_cache(context, sizeof(*context));
	pkvm_context_present_cache_flush(hyp_iommu, param->bdf, param->did);
	ret = 0;

out_unlock:
	pkvm_spin_unlock(&hyp_iommu->lock);

	return ret;
}

unsigned long pkvm_iommu_set_lm_ce(u64 phys, u64 param_gpa)
{
	struct pkvm_iommu *hyp_iommu = find_iommu_by_reg_phys(phys);
	struct pkvm_context_param *param;

	if (!hyp_iommu)
		return -EINVAL;

	param = host_gpa2hva(param_gpa);
	if (!param)
		return -EINVAL;

	param->domain_pgd_gpa = host_gpa2hpa(param->domain_pgd_gpa);
	return set_context_entry(hyp_iommu, param);
}

unsigned long pkvm_iommu_set_lm_ptce(u64 phys, u64 param_gpa)
{
	struct pkvm_iommu *hyp_iommu = find_iommu_by_reg_phys(phys);
	struct pkvm_context_param *param;
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
	return set_context_entry(hyp_iommu, param);
}

static unsigned long context_get_sm_pds(u32 max_pasid)
{
	unsigned long pds, max_pde;

	max_pde = max_pasid >> PASIDDIR_SHIFT;
	pds = find_first_bit(&max_pde, MAX_NR_PASID_BITS);
	if (pds < 7)
		return 0;

	return pds - 7;
}

unsigned long pkvm_iommu_set_sm_ce(u64 phys, u64 param_gpa)
{
	struct pkvm_iommu *hyp_iommu = find_iommu_by_reg_phys(phys);
	struct pkvm_sm_context_param *param;
	struct context_entry *context;
	struct intel_iommu *iommu;
	unsigned long pds;
	u8 bus, devfn;
	int ret = 0;

	if (!hyp_iommu)
		return -EINVAL;

	param = host_gpa2hva(param_gpa);
	if (!param)
		return -EINVAL;

	bus = PCI_BUS_NUM(param->bdf);
	devfn = PCI_DEV_FN(param->bdf);
	if (!param->pasid_dir_gpa) {
		pkvm_err("pkvm: %s pasid_dir gpa not passed in for set_sm_ce(device[%x:%x:%x])\n",
				__func__, bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
		return -EINVAL;
	}

	if (param->ats_supported && !is_dev_in_satc(param->bdf)) {
		pkvm_err("pkvm: %s host reported ats supported for device[%x:%x:%x] not in satc\n",
				__func__, bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
		param->ats_supported = 0;
	}

	pkvm_spin_lock(&hyp_iommu->lock);
	iommu = &hyp_iommu->iommu;

	if (!pkvm_sm_supported(iommu)) {
		pkvm_err("pkvm: %s: trying to set Scalable Mode Context but iommu%d doesn't support it!\n",
				__func__, iommu->seq_id);
		return -EINVAL;
	};

	pkvm_dbg("pkvm: %s: Set sm context mapping for %02x:%02x.%d, pasid_dir: %llx\n",
		__func__, bus, PCI_SLOT(devfn), PCI_FUNC(devfn), param->pasid_dir_gpa);

	context = pkvm_iommu_context_addr(iommu, bus, devfn, param->context_gpa);
	if (!context) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	if (context_present(context))
		goto out_unlock;

	context_clear_entry(context);

	pds = context_get_sm_pds(param->max_pasid);
	context->lo = param->pasid_dir_gpa | context_pdts(pds);
	context_set_sm_rid2pasid(context, IOMMU_NO_PASID);

	if (param->ats_supported)
		context_set_sm_dte(context);
	if (ecap_pasid(iommu->ecap))
		context_set_pasid(context);

	context_set_fault_enable(context);
	context_set_present(context);

	if (validate_sm_context_entries(hyp_iommu, bus, context, devfn >= 0x80)) {
		pkvm_err("pkvm: %s: failed to validate the context entry for device[%x:%x:%x], CE:%llx:%llx!\n",
				__func__, bus, PCI_SLOT(devfn), PCI_FUNC(devfn), context->hi, context->lo);
		context_clear_entry(context);
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!iommu_coherency(iommu))
		iommu_flush_cache(context, sizeof(*context));

	pkvm_context_present_cache_flush(hyp_iommu, param->bdf, 0);

out_unlock:
	pkvm_spin_unlock(&hyp_iommu->lock);

	return ret;
}

unsigned long pkvm_iommu_set_sm_ce_pre(u64 phys, u64 param_gpa)
{
	struct pkvm_iommu *hyp_iommu = find_iommu_by_reg_phys(phys);
	struct pkvm_sm_context_pre_param *param;
	struct context_entry *context;
	struct intel_iommu *iommu;
	u8 bus, devfn;
	int ret = 0;

	if (!hyp_iommu)
		return -EINVAL;

	param = host_gpa2hva(param_gpa);
	if (!param)
		return -EINVAL;

	bus = PCI_BUS_NUM(param->bdf);
	devfn = PCI_DEV_FN(param->bdf);

	pkvm_spin_lock(&hyp_iommu->lock);
	iommu = &hyp_iommu->iommu;

	context = pkvm_iommu_context_addr(iommu, bus, devfn, 0);
	if (!context) {
		ret = -ENODEV;
		goto out_unlock;
	}
	param->did = context_domain_id(context);

	if (param->val)
		context_set_sm_pre(context);
	else
		context_clear_sm_pre(context);

	if (!ecap_coherent(iommu->ecap))
		iommu_flush_cache(context, sizeof(*context));

out_unlock:
	pkvm_spin_unlock(&hyp_iommu->lock);

	return ret;
}
