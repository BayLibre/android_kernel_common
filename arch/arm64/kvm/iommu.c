// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 * Author: Mostafa Saleh <smostafa@google.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>

#include <iommu-pages.h>

#include <linux/cma.h>
#include <linux/of_reserved_mem.h>

/* HVCs returning an error code or 0: handle pending hyp_req and retry */
#define kvm_call_hyp_nvhe_mc(...)							\
({											\
	struct arm_smccc_res __res;							\
	int __ret;									\
	do {										\
		__res = kvm_call_hyp_nvhe_smccc(__VA_ARGS__);				\
		__ret = __pkvm_handle_smccc_req(&__res, (void *)(uintptr_t)GFP_KERNEL); \
	} while (__res.a1 && !__ret);							\
	__ret;										\
})

/* HVCs returning a number of elements: handle pending hyp_req and return num elems */
#define kvm_call_hyp_nvhe_elem(res, gfp, ret, ...)					\
({											\
	struct kvm_hyp_req __req;							\
	int __num_elems;								\
	ret = 0;									\
	res = kvm_call_hyp_nvhe_smccc(__VA_ARGS__);					\
	__num_elems = res.a1;								\
	if (smccc_to_hyp_req(&__req, &res))						\
		ret = handle_hyp_req(NULL, &__req, (void *)(uintptr_t)gfp);		\
	__num_elems;									\
})

extern size_t kvm_nvhe_sym(hyp_kvm_iommu_pages);

static DEFINE_MUTEX(kvm_iommu_reg_lock);
/* Protected by kvm_iommu_reg_lock. */
static LIST_HEAD(kvm_iommu_drivers);

static struct cma *kvm_iommu_cma;
extern phys_addr_t kvm_nvhe_sym(cma_base);
extern size_t kvm_nvhe_sym(cma_size);

static int __init pkvm_iommu_cma_setup(struct reserved_mem *rmem)
{
	int err;

	if (!IS_ALIGNED(rmem->base | rmem->size, PMD_SIZE))
		kvm_info("pKVM IOMMU reserved memory not PMD-aligned\n");

	err = cma_init_reserved_mem(rmem->base, rmem->size, 0, rmem->name,
				    &kvm_iommu_cma, false);
	if (err) {
		kvm_err("Failed to init pKVM IOMMU reserved memory\n");
		kvm_iommu_cma = NULL;
		return err;
	}

	kvm_nvhe_sym(cma_base) = cma_get_base(kvm_iommu_cma);
	kvm_nvhe_sym(cma_size) = cma_get_size(kvm_iommu_cma);

	return 0;
}
RESERVEDMEM_OF_DECLARE(pkvm_cma, "pkvm,iommu-cma", pkvm_iommu_cma_setup);

static const u8 pmd_order = PMD_SHIFT - PAGE_SHIFT;

struct page *kvm_iommu_cma_alloc(void)
{
	if (!kvm_iommu_cma)
		return NULL;

	return cma_alloc(kvm_iommu_cma, (1 << pmd_order), pmd_order, true);
}
EXPORT_SYMBOL(kvm_iommu_cma_alloc);

bool kvm_iommu_cma_release(struct page *p)
{
	if (!kvm_iommu_cma || !p)
		return false;

	return cma_release(kvm_iommu_cma, p, 1 << pmd_order);
}
EXPORT_SYMBOL(kvm_iommu_cma_release);

int kvm_iommu_register_driver(struct kvm_iommu_driver *kern_ops, size_t pool_pages)
{
	static size_t requested_pool_pages;

	if (!kern_ops)
		return -ENODEV;

	guard(mutex)(&kvm_iommu_reg_lock);
	/* See kvm_iommu_pages() */
	if (pool_pages + requested_pool_pages > kvm_nvhe_sym(hyp_kvm_iommu_pages)) {
		kvm_err("Missing memory for the IOMMU pool, need 0x%zx pages, check kvm-arm.hyp_iommu_pages",
			 pool_pages);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&kern_ops->node);
	list_add(&kern_ops->node, &kvm_iommu_drivers);
	requested_pool_pages += pool_pages;
	return 0;
}
EXPORT_SYMBOL(kvm_iommu_register_driver);

int kvm_iommu_register_hyp_ops(struct kvm_iommu_ops *hyp_ops, pkvm_handle_t *drv_id)
{
	struct arm_smccc_res res;
	int ret;

	if (!hyp_ops)
		return -ENODEV;

	res = kvm_call_hyp_nvhe_smccc(__pkvm_iommu_register_ops, hyp_ops);
	ret = res.a1;
	if (ret)
		return ret;
	*drv_id = res.a2;
	return ret;
}
EXPORT_SYMBOL(kvm_iommu_register_hyp_ops);

int kvm_iommu_init_driver(void)
{
	struct kvm_iommu_driver *driver;
	int ret = -ENODEV;

	guard(mutex)(&kvm_iommu_reg_lock);

	list_for_each_entry(driver, &kvm_iommu_drivers, node) {
		if (driver->init_driver) {
			ret = driver->init_driver();
			if (ret)
				break;
		}
	}

	if (ret)
		kvm_err("Failed to init iommu driver, do not run confidential workloads in virtual machines: %d\n", ret);
	return ret;
}

size_t kvm_iommu_pages(void)
{
	/*
	 * This is called very early during setup_arch() where no initcalls,
	 * so this has to call specific functions per each KVM driver.
	 * So we allow a config option that can set the defaul value for
	 * the IOMMU pool that can overridden by a command line option.
	 * When the driver registers it will pass the number pages needed
	 * for it's page tables, if less that what the system has already
	 * allocated we fail.
	 */
	return kvm_nvhe_sym(hyp_kvm_iommu_pages);
}

/* Number of pages to reserve for iommu pool*/
static int __init early_hyp_iommu_pages(char *arg)
{
	return kstrtoul(arg, 10, &kvm_nvhe_sym(hyp_kvm_iommu_pages));
}
early_param("kvm-arm.hyp_iommu_pages", early_hyp_iommu_pages);

/*
 * Note: iommu_alloc_pages_sz() returns a compound page when an order above 0 is used. Therefore,
 * it is important that the address passed to hyp_mc_iommu_free_fn() is the same as what was
 * returned by hyp_mc_iommu_alloc_gfp_fn().
 */
static void *hyp_mc_iommu_alloc_gfp_fn(void *gfp_flags_p, unsigned long order)
{
	return iommu_alloc_pages_sz(*(gfp_t *)gfp_flags_p, PAGE_SIZE << order);
}

static void hyp_mc_iommu_free_fn(void *virt, void *arg, unsigned long order)
{
	iommu_free_pages(virt);
}

unsigned long __pkvm_free_iommu_hyp_memcache(struct kvm_hyp_memcache *mc)
{
	return __free_hyp_memcache(mc, hyp_mc_iommu_free_fn, kvm_host_va, mc);
}

static phys_addr_t __topup_virt_to_phys(void *virt)
{
	return __pa(virt);
}

static int __kvm_iommu_topup_memcache_from_cma(size_t size, gfp_t gfp, size_t *allocated)
{
	*allocated = 0;

	if ((gfp & GFP_ATOMIC) == GFP_ATOMIC)
		return -ENOMEM;

	while (*allocated < size) {
		struct page *p = kvm_iommu_cma_alloc();
		struct kvm_hyp_memcache mc;

		if (!p)
			return -ENOMEM;

		init_hyp_memcache(&mc);
		push_hyp_memcache(&mc, page_to_virt(p), __topup_virt_to_phys,
				  PMD_SHIFT - PAGE_SHIFT);

		if (__pkvm_topup_hyp_alloc_mgt_mc(HYP_ALLOC_MGT_IOMMU_ID, &mc)) {
			kvm_iommu_cma_release(p);
			return -EINVAL;
		}

		*allocated += PMD_SIZE;
	}

	return 0;
}

int __pkvm_topup_hyp_iommu(unsigned long nr_pages, unsigned long sz_alloc, gfp_t gfp)
{
	struct kvm_hyp_memcache mc;
	int ret;
	unsigned long order = get_order(sz_alloc);
	size_t size = sz_alloc * nr_pages;
	size_t from_cma = 0;

	if (!is_protected_kvm_enabled())
		return 0;

	if (order > PAGE_SHIFT)
		return -E2BIG;

	if (sz_alloc <= PMD_SIZE) {
		ret = __kvm_iommu_topup_memcache_from_cma(size, gfp, &from_cma);
		if (!ret)
			return 0;
	}

	nr_pages -= from_cma / sz_alloc;

	init_hyp_memcache(&mc);

	ret = __topup_hyp_memcache(&mc, nr_pages, hyp_mc_iommu_alloc_gfp_fn, kvm_host_pa, &gfp,
				   order);
	if (ret)
		return ret;

	ret = __pkvm_topup_hyp_alloc_mgt_mc(HYP_ALLOC_MGT_IOMMU_ID, &mc);
	if (ret) {
		kvm_err("Failed topup iommu heap pages = %ld, size = %ld err = %d, freeing %ld pages\n",
			nr_pages, sz_alloc, ret, mc.nr_pages);
		__free_hyp_memcache(&mc, hyp_mc_iommu_free_fn, kvm_host_va, &mc);
	}

	return ret;
}

/* Hypercall abstractions exposed to kernel IOMMU drivers */
int kvm_iommu_attach_dev(pkvm_handle_t iommu_id, pkvm_handle_t domain_id,
			 unsigned int endpoint, unsigned int pasid,
			 unsigned int ssid_bits, unsigned long flags)
{
	return kvm_call_hyp_nvhe_mc(__pkvm_host_iommu_attach_dev, iommu_id, domain_id,
				    endpoint, pasid, ssid_bits, flags);
}
EXPORT_SYMBOL(kvm_iommu_attach_dev);

int kvm_iommu_attach_dev_nested(pkvm_handle_t iommu_id, pkvm_handle_t domain_id,
				unsigned int endpoint, unsigned int pasid,
				unsigned long flags, void *s1_desc_hva, size_t s1_desc_size)
{
	return kvm_call_hyp_nvhe_mc(__pkvm_host_iommu_attach_dev_nested, iommu_id, domain_id,
				    endpoint, pasid, flags, s1_desc_hva, s1_desc_size);
}
EXPORT_SYMBOL(kvm_iommu_attach_dev_nested);

int kvm_iommu_detach_dev(pkvm_handle_t iommu_id, pkvm_handle_t domain_id,
			 unsigned int endpoint, unsigned int pasid)
{
	return kvm_call_hyp_nvhe(__pkvm_host_iommu_detach_dev, iommu_id, domain_id,
				endpoint, pasid);
}
EXPORT_SYMBOL(kvm_iommu_detach_dev);

int kvm_iommu_alloc_domain(pkvm_handle_t drv_id, pkvm_handle_t iommu_id,
			   pkvm_handle_t domain_id, int type)
{
	return kvm_call_hyp_nvhe_mc(__pkvm_host_iommu_alloc_domain, drv_id,
				    iommu_id, domain_id, type);
}
EXPORT_SYMBOL(kvm_iommu_alloc_domain);

int kvm_iommu_free_domain(pkvm_handle_t domain_id)
{
	return kvm_call_hyp_nvhe(__pkvm_host_iommu_free_domain, domain_id);
}
EXPORT_SYMBOL(kvm_iommu_free_domain);

int kvm_iommu_map_pages(pkvm_handle_t domain_id, unsigned long iova,
			phys_addr_t paddr, size_t pgsize, size_t pgcount,
			int prot, gfp_t gfp, size_t *total_mapped)
{
	size_t mapped;
	size_t size = pgsize * pgcount;
	struct arm_smccc_res res;
	int ret;

	do {
		mapped = kvm_call_hyp_nvhe_elem(res, gfp, ret,
						__pkvm_host_iommu_map_pages, domain_id,
						iova, paddr, pgsize, pgcount, prot);
		iova += mapped;
		paddr += mapped;
		WARN_ON(mapped % pgsize);
		WARN_ON(mapped > pgcount * pgsize);
		pgcount -= mapped / pgsize;
		*total_mapped += mapped;
	} while (*total_mapped < size && !ret);

	/* See handle___pkvm_host_iommu_map_pages */
	if (*total_mapped < size)
		return res.a0 ? res.a0 : ret;
	return 0;
}
EXPORT_SYMBOL(kvm_iommu_map_pages);

size_t kvm_iommu_unmap_pages(pkvm_handle_t domain_id, unsigned long iova,
			     size_t pgsize, size_t pgcount)
{
	size_t unmapped;
	size_t total_unmapped = 0;
	size_t size = pgsize * pgcount;
	struct arm_smccc_res res;
	int ret;

	do {
		unmapped = kvm_call_hyp_nvhe_elem(res, GFP_ATOMIC, ret,
						  __pkvm_host_iommu_unmap_pages,
						  domain_id, iova, pgsize, pgcount);
		total_unmapped += unmapped;
		iova += unmapped;
		WARN_ON(unmapped % pgsize);
		pgcount -= unmapped / pgsize;

		/*
		 * The page table driver can unmap less than we asked for. If it
		 * didn't unmap anything at all, then it either reached the end
		 * of the range, or it needs a page in the memcache to break a
		 * block mapping.
		 */
	} while (total_unmapped < size && (unmapped || !ret));

	return total_unmapped;

}
EXPORT_SYMBOL(kvm_iommu_unmap_pages);

phys_addr_t kvm_iommu_iova_to_phys(pkvm_handle_t domain_id, unsigned long iova)
{
	return kvm_call_hyp_nvhe(__pkvm_host_iommu_iova_to_phys, domain_id, iova);
}
EXPORT_SYMBOL(kvm_iommu_iova_to_phys);

int kvm_iommu_iotlb_inv_nested_domain(pkvm_handle_t domain_id, unsigned long iova,
				      size_t size, size_t granule, bool leaf)
{
	return kvm_call_hyp_nvhe(__pkvm_host_iommu_iotlb_inv_nested_domain, domain_id, iova, size,
				 granule, leaf);
}
EXPORT_SYMBOL(kvm_iommu_iotlb_inv_nested_domain);

int kvm_iommu_nested_cfg_sync(pkvm_handle_t drv_id, pkvm_handle_t iommu_id, void *cmd_desc_hva,
			      size_t cmd_desc_size)
{
	return kvm_call_hyp_nvhe(__pkvm_host_iommu_nested_cfg_sync, drv_id, iommu_id, cmd_desc_hva,
				 cmd_desc_size);
}
EXPORT_SYMBOL(kvm_iommu_nested_cfg_sync);

int pkvm_iommu_suspend(int device_id)
{
	return kvm_call_hyp_nvhe(__pkvm_host_hvc_pd, device_id, 0);
}
EXPORT_SYMBOL(pkvm_iommu_suspend);

int pkvm_iommu_resume(int device_id)
{
	return kvm_call_hyp_nvhe(__pkvm_host_hvc_pd, device_id, 1);
}
EXPORT_SYMBOL(pkvm_iommu_resume);

int kvm_iommu_set_identity(pkvm_handle_t drv_id, pkvm_handle_t iommu,
			   pkvm_handle_t dev, bool on)
{
	return kvm_call_hyp_nvhe_mc(__pkvm_host_iommu_set_identity, drv_id,
				    iommu, dev, on);
}
EXPORT_SYMBOL(kvm_iommu_set_identity);

int kvm_iommu_share_hyp_sg(struct kvm_iommu_sg *sg, unsigned int nents)
{
	size_t nr_pages = PAGE_ALIGN(sizeof(*sg) * nents) >> PAGE_SHIFT;
	phys_addr_t sg_pfn = virt_to_phys(sg) >> PAGE_SHIFT;
	int i;
	int ret;

	for (i = 0 ; i < nr_pages ; ++i) {
		ret = kvm_call_hyp_nvhe(__pkvm_host_share_hyp, sg_pfn + i);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(kvm_iommu_share_hyp_sg);

int kvm_iommu_unshare_hyp_sg(struct kvm_iommu_sg *sg, unsigned int nents)
{
	size_t nr_pages = PAGE_ALIGN(sizeof(*sg) * nents) >> PAGE_SHIFT;
	phys_addr_t sg_pfn = virt_to_phys(sg) >> PAGE_SHIFT;
	int i;
	int ret;

	for (i = 0 ; i < nr_pages ; ++i) {
		ret = kvm_call_hyp_nvhe(__pkvm_host_unshare_hyp, sg_pfn + i);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(kvm_iommu_unshare_hyp_sg);

size_t kvm_iommu_map_sg(pkvm_handle_t domain_id, struct kvm_iommu_sg *sg,
			unsigned long iova, unsigned int nent,
			unsigned int prot, gfp_t gfp)
{
	size_t mapped, total_mapped = 0;
	struct arm_smccc_res res;
	int ret;

	do {
		mapped = kvm_call_hyp_nvhe_elem(res, gfp, ret,
						__pkvm_host_iommu_map_sg,
						domain_id, iova, sg, nent, prot);
		iova += mapped;
		total_mapped += mapped;
		/* Skip mapped */
		while (mapped) {
			if (mapped < (sg->pgsize * sg->pgcount)) {
				sg->phys += mapped;
				sg->pgcount -= mapped / sg->pgsize;
				mapped = 0;
			} else {
				mapped -= sg->pgsize * sg->pgcount;
				sg++;
				nent--;
			}
		}

	} while (nent && !ret);

	return total_mapped;
}
EXPORT_SYMBOL(kvm_iommu_map_sg);

int kvm_get_iommu_id_by_of(struct device_node *np, pkvm_handle_t *out_id)
{
	int ret = -ENODEV;
	struct kvm_iommu_driver *driver;

	/* Find a driver that handles this device */
	mutex_lock(&kvm_iommu_reg_lock);
	list_for_each_entry(driver, &kvm_iommu_drivers, node) {
		if (driver->get_iommu_id_by_of) {
			ret = driver->get_iommu_id_by_of(np, out_id);
			if (ret == 0)
				break;
		}
	}

	mutex_unlock(&kvm_iommu_reg_lock);
	return ret;
}

int kvm_get_iommu_endpoint(struct of_phandle_args *iommu_spec, u64 *out_endpoint)
{
	int ret = -ENODEV;
	struct kvm_iommu_driver *driver;

	/* Find a driver that handles this device */
	mutex_lock(&kvm_iommu_reg_lock);
	list_for_each_entry(driver, &kvm_iommu_drivers, node) {
		if (driver->get_iommu_endpoint) {
			ret = driver->get_iommu_endpoint(iommu_spec, out_endpoint);
			if (ret == 0)
				break;
		}
	}

	mutex_unlock(&kvm_iommu_reg_lock);
	return ret;
}

int kvm_iommu_device_num_ids(struct device *dev)
{
	int ret = 0;
	struct kvm_iommu_driver *driver;

	mutex_lock(&kvm_iommu_reg_lock);
	list_for_each_entry(driver, &kvm_iommu_drivers, node) {
		if (driver->get_device_iommu_num_ids) {
			ret = driver->get_device_iommu_num_ids(dev);
			if (ret)
				break;
		}
	}

	mutex_unlock(&kvm_iommu_reg_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(kvm_iommu_device_num_ids);

int kvm_iommu_device_id(struct device *dev, u32 idx,
			pkvm_handle_t *out_iommu, u32 *out_sid)
{
	int ret = -ENODEV;
	struct kvm_iommu_driver *driver;

	mutex_lock(&kvm_iommu_reg_lock);
	list_for_each_entry(driver, &kvm_iommu_drivers, node) {
		if (driver->get_device_iommu_id) {
			ret = driver->get_device_iommu_id(dev, idx, out_iommu, out_sid);
			if (ret == 0)
				break;
		}
	}

	mutex_unlock(&kvm_iommu_reg_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(kvm_iommu_device_id);

int kvm_iommu_guest_alloc_mc(struct kvm_hyp_memcache *mc, u32 pgsize, u32 nr_pages)
{
	return topup_hyp_memcache(mc, nr_pages, get_order(pgsize));
}

void kvm_iommu_guest_free_mc(struct kvm_hyp_memcache *mc)
{
		free_hyp_memcache(mc);
}
