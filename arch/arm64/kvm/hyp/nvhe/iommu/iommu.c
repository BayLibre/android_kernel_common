// SPDX-License-Identifier: GPL-2.0
/*
 * IOMMU operations for pKVM
 *
 * Copyright (C) 2022 Linaro Ltd.
 */

#include <asm/kvm_hyp.h>
#include <kvm/iommu.h>
#include <nvhe/alloc_mgt.h>
#include <nvhe/iommu.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>

void **kvm_hyp_iommu_domains;
/*
 * This lock protect domain operations, that can't be done using the atomic refcount
 * It is used for alloc/free domains, so it shouldn't have a lot of overhead as
 * these are rare operations, while map/unmap are left lockless.
 */
static DEFINE_HYP_SPINLOCK(iommu_domains_lock);

static struct hyp_pool iommu_host_pool;

DECLARE_PER_CPU(struct kvm_hyp_req, host_hyp_reqs);

void *kvm_iommu_donate_page(void)
{
	void *p;
	struct kvm_hyp_req *req = this_cpu_ptr(&host_hyp_reqs);

	p = hyp_alloc_pages(&iommu_host_pool, 0);
	if (p)
		return p;

	req->type = KVM_HYP_REQ_MEM;
	req->mem.dest = REQ_MEM_IOMMU;
	req->mem.sz_alloc = PAGE_SIZE;
	req->mem.nr_pages = 1;
	return NULL;
}

void kvm_iommu_reclaim_page(void *p)
{
	hyp_put_page(&iommu_host_pool, p);
}

int kvm_iommu_refill(struct kvm_hyp_memcache *host_mc)
{
	void *p;
	unsigned long order;

	while (host_mc->nr_pages) {
		order = host_mc->head & (PAGE_SIZE - 1);
		p = pkvm_admit_host_page(host_mc, order);
		hyp_virt_to_page(p)->order = order;
		hyp_set_page_refcounted(hyp_virt_to_page(p));
		hyp_put_page(&iommu_host_pool, p);
	}

	return 0;
}

void kvm_iommu_reclaim(struct kvm_hyp_memcache *host_mc, int target)
{
	/* TODO */
}

int kvm_iommu_reclaimable(void)
{
	/* TODO */
	return 0;
}

struct hyp_mgt_allocator_ops kvm_iommu_allocator_ops = {
	.refill = kvm_iommu_refill,
	.reclaim = kvm_iommu_reclaim,
	.reclaimable = kvm_iommu_reclaimable,
};

static struct kvm_hyp_iommu_domain *
handle_to_domain(pkvm_handle_t domain_id)
{
	int idx;
	struct kvm_hyp_iommu_domain *domains;

	if (domain_id >= KVM_IOMMU_MAX_DOMAINS)
		return NULL;
	domain_id = array_index_nospec(domain_id, KVM_IOMMU_MAX_DOMAINS);

	idx = domain_id >> KVM_IOMMU_DOMAIN_ID_SPLIT;
	domains = (struct kvm_hyp_iommu_domain *)READ_ONCE(kvm_hyp_iommu_domains[idx]);
	if (!domains) {
		domains = kvm_iommu_donate_page();
		if (!domains)
			return NULL;

		/*
		 * handle_to_domain() does not have to be called under a lock,
		 * but even though we allocate a leaf in all cases, it's only
		 * really a valid thing to do under alloc_domain(), which uses a
		 * lock. Races are therefore a host bug and we don't need to be
		 * delicate about it.
		 */
		if (WARN_ON(cmpxchg64_relaxed(&kvm_hyp_iommu_domains[idx], 0,
					      (void *)domains) != 0))
			return NULL;
	}

	return &domains[domain_id & KVM_IOMMU_DOMAIN_ID_LEAF_MASK];
}

static int domain_get(struct kvm_hyp_iommu_domain *domain)
{
	int old = atomic_fetch_inc_acquire(&domain->refs);

	if (WARN_ON(!old))
		return -EINVAL;
	else if (old < 0 || old + 1 < 0)
		return -EOVERFLOW;
	return 0;
}

static void domain_put(struct kvm_hyp_iommu_domain *domain)
{
	BUG_ON(!atomic_dec_return_release(&domain->refs));
}

int kvm_iommu_alloc_domain(pkvm_handle_t domain_id, unsigned long pgd_hva,
			   unsigned long pgd_size)
{
	int ret = -EINVAL;
	struct kvm_hyp_iommu_domain *domain;

	hyp_spin_lock(&iommu_domains_lock);
	domain = handle_to_domain(domain_id);
	if (!domain)
		goto out_unlock;

	if (atomic_read(&domain->refs))
		goto out_unlock;

	ret = kvm_iommu_ops->alloc_domain(domain, domain_id, pgd_hva, pgd_size);
	if (ret)
		goto out_unlock;
	atomic_set_release(&domain->refs, 1);
out_unlock:
	hyp_spin_unlock(&iommu_domains_lock);
	return ret;
}

int kvm_iommu_free_domain(pkvm_handle_t domain_id)
{
	int ret = -EINVAL;
	struct kvm_hyp_iommu_domain *domain;

	hyp_spin_lock(&iommu_domains_lock);
	domain = handle_to_domain(domain_id);
	if (!domain)
		goto out_unlock;

	if (WARN_ON(atomic_cmpxchg_release(&domain->refs, 1, 0) != 1))
		goto out_unlock;

	ret = kvm_iommu_ops->free_domain(domain, domain_id);
	memset(domain, 0, sizeof(*domain));

out_unlock:
	hyp_spin_unlock(&iommu_domains_lock);

	return ret;
}

int kvm_iommu_attach_dev(pkvm_handle_t iommu_id, pkvm_handle_t domain_id,
			 u32 endpoint_id)
{
	int ret = -EINVAL;
	struct kvm_hyp_iommu *iommu;
	struct kvm_hyp_iommu_domain *domain;

	iommu = kvm_iommu_ops->get_iommu_by_id(iommu_id);
	if (!iommu)
		return -EINVAL;

	hyp_spin_lock(&iommu_domains_lock);
	domain = handle_to_domain(domain_id);
	if (!domain || domain_get(domain))
		goto out_unlock;

	ret = kvm_iommu_ops->attach_dev(iommu, domain_id, domain, endpoint_id);
	if (ret)
		goto err_put_domain;

	hyp_spin_unlock(&iommu_domains_lock);
	return 0;

err_put_domain:
	domain_put(domain);
out_unlock:
	hyp_spin_unlock(&iommu_domains_lock);
	return ret;
}

int kvm_iommu_detach_dev(pkvm_handle_t iommu_id, pkvm_handle_t domain_id,
			 u32 endpoint_id)
{
	int ret = -EINVAL;
	struct kvm_hyp_iommu *iommu;
	struct kvm_hyp_iommu_domain *domain;

	iommu = kvm_iommu_ops->get_iommu_by_id(iommu_id);
	if (!iommu)
		return -EINVAL;

	hyp_spin_lock(&iommu_domains_lock);
	domain = handle_to_domain(domain_id);
	if (!domain || atomic_read(&domain->refs) <= 1)
		goto out_unlock;

	ret = kvm_iommu_ops->detach_dev(iommu, domain_id, domain, endpoint_id);
	if (ret)
		goto out_unlock;

	domain_put(domain);
out_unlock:
	hyp_spin_unlock(&iommu_domains_lock);
	return ret;
}

#define IOMMU_PROT_MASK (IOMMU_READ | IOMMU_WRITE | IOMMU_CACHE |\
			 IOMMU_NOEXEC | IOMMU_MMIO | IOMMU_PRIV)

size_t kvm_iommu_map_pages(pkvm_handle_t domain_id, unsigned long iova,
			   phys_addr_t paddr, size_t pgsize,
			   size_t pgcount, int prot)
{
	size_t size;
	size_t mapped;
	size_t granule;
	int ret;
	struct io_pgtable iopt;
	size_t total_mapped = 0;
	struct kvm_hyp_iommu_domain *domain;

	if (!kvm_iommu_ops)
		return 0;

	if (prot & ~IOMMU_PROT_MASK)
		return 0;

	if (__builtin_mul_overflow(pgsize, pgcount, &size) ||
	    iova + size < iova || paddr + size < paddr)
		return 0;

	/*
	 * TODO: check whether it is safe here to call io-pgtable without a
	 * lock. Does the driver make assumptions that don't hold for the
	 * hypervisor, for example that device drivers don't call map/unmap
	 * concurrently on the same page?
	 *
	 * Command queue and iommu->power_is_off are also protected by the
	 * iommu_lock, taken by the TLB invalidation callbacks.
	 */

	domain = handle_to_domain(domain_id);
	if (!domain || domain_get(domain))
		return 0;

	granule = 1 << __ffs(domain->pgtable->cfg.pgsize_bitmap);
	if (!IS_ALIGNED(iova | paddr | pgsize, granule))
		goto out_put_domain;

	ret = __pkvm_host_share_dma(paddr, size, !(prot & IOMMU_MMIO));
	if (ret)
		goto out_put_domain;

	iopt = domain_to_iopt(domain, domain_id);
	while (pgcount && !ret) {
		mapped = 0;
		ret = iopt_map_pages(&iopt, iova, paddr, pgsize, pgcount, prot,
				     0, &mapped);
		WARN_ON(!IS_ALIGNED(mapped, pgsize));
		WARN_ON(mapped > pgcount * pgsize);

		pgcount -= mapped / pgsize;
		total_mapped += mapped;
		iova += mapped;
		paddr += mapped;
	}

	/*
	 * Unshare the bits that haven't been mapped yet. The host calls back
	 * either to continue mapping, or to unmap and unshare what's been done
	 * so far.
	 */
	if (pgcount)
		__pkvm_host_unshare_dma(paddr, pgcount * pgsize);
out_put_domain:
	domain_put(domain);
	return total_mapped;
}

size_t kvm_iommu_unmap_pages(pkvm_handle_t domain_id,
			     unsigned long iova, size_t pgsize, size_t pgcount)
{
	int ret;
	size_t size;
	size_t granule;
	size_t unmapped;
	phys_addr_t paddr = 0;
	struct io_pgtable iopt;
	size_t total_unmapped = 0;
	struct kvm_hyp_iommu_domain *domain;

	if (!kvm_iommu_ops)
		return 0;

	if (!pgsize || !pgcount)
		return 0;

	if (__builtin_mul_overflow(pgsize, pgcount, &size) ||
	    iova + size < iova)
		return 0;

	domain = handle_to_domain(domain_id);
	if (!domain || domain_get(domain))
		return 0;

	granule = 1 << __ffs(domain->pgtable->cfg.pgsize_bitmap);
	if (!IS_ALIGNED(iova | pgsize, granule))
		goto out_put_domain;

	iopt = domain_to_iopt(domain, domain_id);

	while (total_unmapped < size) {
		/*
		 * One page/block at a time so that we can unshare each page.
		 * The IOVA range provided may not be physically contiguous, and
		 * @pgsize may be larger than the one used when mapping.
		 */
		unmapped = iopt_unmap_leaf(&iopt, iova, pgsize, &paddr);
		if (!unmapped || !paddr)
			goto out_put_domain;

		ret = __pkvm_host_unshare_dma(paddr, unmapped);
		if (WARN_ON(ret))
			goto out_put_domain;

		iova += unmapped;
		total_unmapped += unmapped;
	}

out_put_domain:
	domain_put(domain);
	return total_unmapped;
}

phys_addr_t kvm_iommu_iova_to_phys(pkvm_handle_t domain_id, unsigned long iova)
{
	phys_addr_t phys = 0;
	struct io_pgtable iopt;
	struct kvm_hyp_iommu_domain *domain;

	domain = handle_to_domain(domain_id);
	if (!domain || domain_get(domain))
		return 0;

	iopt = domain_to_iopt(domain, domain_id);
	phys = iopt_iova_to_phys(&iopt, iova);

	domain_put(domain);

	return phys;
}

static int iommu_power_on(struct kvm_power_domain *pd)
{
	struct kvm_hyp_iommu *iommu = container_of(pd, struct kvm_hyp_iommu,
						   power_domain);

	/*
	 * We currently assume that the device retains its architectural state
	 * across power off, hence no save/restore.
	 */
	hyp_spin_lock(&iommu->lock);
	iommu->power_is_off = false;
	hyp_spin_unlock(&iommu->lock);
	return 0;
}

static int iommu_power_off(struct kvm_power_domain *pd)
{
	struct kvm_hyp_iommu *iommu = container_of(pd, struct kvm_hyp_iommu,
						   power_domain);

	hyp_spin_lock(&iommu->lock);
	iommu->power_is_off = true;
	hyp_spin_unlock(&iommu->lock);
	return 0;
}

static const struct kvm_power_domain_ops iommu_power_ops = {
	.power_on	= iommu_power_on,
	.power_off	= iommu_power_off,
};

int kvm_iommu_init_device(struct kvm_hyp_iommu *iommu)
{
	return pkvm_init_power_domain(&iommu->power_domain, &iommu_power_ops);
}

int kvm_iommu_init(struct kvm_iommu_ops *ops, unsigned long init_arg)
{
	int ret;

	if (WARN_ON(!ops->get_iommu_by_id ||
		    !ops->free_domain ||
		    !ops->alloc_domain ||
		    !ops->attach_dev ||
		    !ops->detach_dev))
		return -ENODEV;

	ret = ops->init ? ops->init(init_arg) : 0;
	if (ret)
		return ret;

	ret = pkvm_create_mappings(kvm_hyp_iommu_domains, kvm_hyp_iommu_domains +
				   KVM_IOMMU_DOMAINS_ROOT_ENTRIES, PAGE_HYP);
	if (ret)
		return ret;

	ret = hyp_pool_init(&iommu_host_pool, 0, 64 /* order = 6*/, 0, true);

	kvm_iommu_ops = ops;
	return ret;
}
