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
#include "mem_protect.h"
#include "iommu_internal.h"
#include "debug.h"
#include "iommu_spgt.h"
#include "bug.h"
#include "iommu.h"

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

struct context_entry *pkvm_iommu_context_addr(struct intel_iommu *iommu, u8 bus,
					 u8 devfn, u64 *context_phys)
{
	struct root_entry *root = &iommu->root_entry[bus];
	struct context_entry *context;
	u64 *entry;

	entry = &root->lo;
	if (sm_supported(iommu)) {
		if (devfn >= 0x80) {
			devfn -= 0x80;
			entry = &root->hi;
		}
		devfn *= 2;
	}
	if (*entry & 1)
		context = pkvm_phys_to_virt(*entry & VTD_PAGE_MASK);
	else {
		if (!context_phys || !*context_phys)
			return NULL;

		context = (struct context_entry *)host_gpa2hva(*context_phys);
		if (!context)
			return NULL;

		pkvm_dbg("pkvm: %s: write protecting lm context table: %llx\n",
				__func__, pkvm_virt_to_phys(context));
		if (__pkvm_host_donate_hyp_request_ro(pkvm_virt_to_phys(context), PAGE_SIZE)) {
			pkvm_err("pkvm: %s: failed to write protect lm context table Page!\n", __func__);
			return NULL;
		}
		memset(context, 0, PAGE_SIZE);

		__pkvm_iommu_flush_cache(iommu, context, PAGE_SIZE);
		*entry = host_gpa2hpa(*context_phys) | 1;
		__pkvm_iommu_flush_cache(iommu, entry, sizeof(*entry));

		/*
		 * Unset the context_phys to let host driver know that pkvm has
		 * used the donated page for context table.
		 */
		*context_phys = 0;
	}
	return &context[devfn];
}

unsigned long pkvm_iommu_clear_ce(u64 param_va)
{
	struct pkvm_clear_translation_param *param;
	struct context_entry *context;
	struct pkvm_iommu *hyp_iommu;
	int ret = 0;

	if (!param_va)
		return -EINVAL;

	param = (struct pkvm_clear_translation_param *)kern_pkvm_va((void *)param_va);
	if (WARN_ON_ONCE(param != this_pv_param(clear_translation_param)))
		return -EINVAL;

	hyp_iommu = find_iommu_by_reg_phys(param->phys);
	if (!hyp_iommu)
		return -EINVAL;

	pkvm_spin_lock(&hyp_iommu->lock);
	context = pkvm_iommu_context_addr(&hyp_iommu->iommu,
			PCI_BUS_NUM(param->bdf), PCI_DEV_FN(param->bdf), NULL);

	if (!context)
		goto out;

	if (sm_supported(&hyp_iommu->iommu) && context_present(context)) {
		ret = pkvm_pasid_free_table(
				pkvm_phys_to_virt(context->lo & VTD_PAGE_MASK),
				1 << (((context->lo >> 9) & 0x7) + 7));
		if (ret)
			goto out;
	}

	/*
	 * Pass the did back to host for iommu cache flush.
	 */
	param->did = context_domain_id(context);
	pkvm_dbg("pkvm: %s: clear_ce: dev[%x] did: %u\n",
			__func__, param->bdf, param->did);
	context_clear_entry(context);
	__pkvm_iommu_flush_cache(&hyp_iommu->iommu, context, sizeof(*context));
out:
	pkvm_spin_unlock(&hyp_iommu->lock);
	return ret;
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

unsigned long set_context_entry(struct pkvm_iommu *hyp_iommu,
		struct pkvm_lm_context_param *param)
{
	struct intel_iommu *iommu = &hyp_iommu->iommu;
	u8 bus = PCI_BUS_NUM(param->bdf);
	u8 devfn = PCI_DEV_FN(param->bdf);
	struct context_entry *context;
	u8 tt;

	context = pkvm_iommu_context_addr(iommu, bus, devfn, &param->context_gpa);
	if (!context)
		return -ENOMEM;

	if (context_present(context))
		return -EBUSY;

	if (param->ats_supported)
		tt = CONTEXT_TT_DEV_IOTLB;
	else
		tt = CONTEXT_TT_MULTI_LEVEL;

	__set_lm_context(context, param->did, param->domain_agaw,
			tt, param->domain_pgd_gpa);

	__pkvm_iommu_flush_cache(&hyp_iommu->iommu, context, sizeof(*context));
	pkvm_context_present_cache_flush(hyp_iommu, param->bdf, param->did);

	return 0;
}

unsigned long pkvm_iommu_set_lm_ce(u64 param_va)
{
	struct pkvm_lm_context_param *param;
	struct pkvm_iommu *hyp_iommu;
	struct intel_iommu *iommu;
	int ret;

	if (!param_va)
		return -EINVAL;

	param = (struct pkvm_lm_context_param *)kern_pkvm_va((void *)param_va);
	if (WARN_ON_ONCE(param != this_pv_param(lm_context_param)))
		return -EINVAL;

	hyp_iommu = find_iommu_by_reg_phys(param->phys);
	if (!hyp_iommu)
		return -EINVAL;

	pkvm_spin_lock(&hyp_iommu->lock);
	iommu = &hyp_iommu->iommu;

	if (param->did == FLPT_DEFAULT_DID) {
		/*
		 * Passthrough will break pkvm security guarantees as
		 * device would be able to access the whole physical
		 * memory range. Use Second stage translation with host ept
		 * as second stage pagetable so as to limit device access
		 * to host memory..
		 */
		param->domain_agaw = level_to_agaw(pkvm_host_ept_level());
		param->domain_pgd_gpa = pkvm_host_ept_pgd();
	} else {
		param->domain_pgd_gpa = host_gpa2hpa(param->domain_pgd_gpa);
	}
	pkvm_dbg("pkvm: %s: set_lm_ce: dev[%x] did:%u, agaw: %u, pgd: %llx\n",
			__func__, param->bdf, param->did,
			param->domain_agaw, param->domain_pgd_gpa);

	ret = set_context_entry(hyp_iommu, param);

	pkvm_spin_unlock(&hyp_iommu->lock);

	return ret;
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

unsigned long pkvm_iommu_set_sm_ce(u64 param_va)
{
	struct pkvm_sm_context_param *param;
	struct context_entry *context;
	struct pkvm_iommu *hyp_iommu;
	struct intel_iommu *iommu;
	unsigned long pds;
	u8 bus, devfn;
	int ret = 0;

	if (!param_va)
		return -EINVAL;

	param = (struct pkvm_sm_context_param *)kern_pkvm_va((void *)param_va);
	if (WARN_ON_ONCE(param != this_pv_param(sm_context_param)))
		return -EINVAL;

	hyp_iommu = find_iommu_by_reg_phys(param->phys);
	if (!hyp_iommu)
		return -EINVAL;

	if (!param->pasid_dir_gpa) {
		return -EINVAL;
	}

	if (param->ats_supported && !is_dev_in_satc(param->bdf)) {
		pkvm_err("pkvm: %s host reports ats supported for device[%x], but not in satc\n",
				__func__, param->bdf);
		param->ats_supported = 0;
	}

	pkvm_spin_lock(&hyp_iommu->lock);
	iommu = &hyp_iommu->iommu;

	pkvm_dbg("pkvm: %s: set_sm_ce: dev[%x] max_pasid: %u, pasid_dir: %llx\n",
			__func__, param->bdf, param->max_pasid, param->pasid_dir_gpa);

	if (!sm_supported(iommu)) {
		pkvm_err("pkvm: %s: iommu%d doesn't support scalable mode!\n",
				__func__, iommu->seq_id);
		return -EINVAL;
	};

	bus = PCI_BUS_NUM(param->bdf);
	devfn = PCI_DEV_FN(param->bdf);
	context = pkvm_iommu_context_addr(iommu, bus, devfn, &param->context_gpa);
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

	__pkvm_iommu_flush_cache(iommu, context, sizeof(*context));
	pkvm_context_present_cache_flush(hyp_iommu, param->bdf, 0);

out_unlock:
	pkvm_spin_unlock(&hyp_iommu->lock);

	return ret;
}
