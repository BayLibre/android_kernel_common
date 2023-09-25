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
static struct hyp_pool iommu_idmap_pool;

DECLARE_PER_CPU(struct kvm_hyp_req, host_hyp_reqs);

static atomic_t kvm_iommu_idmap_initialized;

static int snapshot_host_stage2(void);

static void host_lock_component(void)
{
	hyp_spin_lock(&host_mmu.lock);
}

static void host_unlock_component(void)
{
	hyp_spin_unlock(&host_mmu.lock);
}

static inline void kvm_iommu_idmap_init_done(void)
{
	atomic_set_release(&kvm_iommu_idmap_initialized, 1);
}

static inline bool kvm_iommu_is_ready(void)
{
	return atomic_read(&kvm_iommu_idmap_initialized) == 1;
}

void *kvm_iommu_donate_pages(u8 order, bool request)
{
	void *p;
	struct kvm_hyp_req *req = this_cpu_ptr(&host_hyp_reqs);

	p = hyp_alloc_pages(&iommu_host_pool, order);
	if (p)
		return p;

	if (request) {
		req->type = KVM_HYP_REQ_MEM;
		req->mem.dest = REQ_MEM_IOMMU;
		req->mem.sz_alloc = (1 << order) * PAGE_SIZE;
		req->mem.nr_pages = 1;
	}
	return NULL;
}

static void __kvm_iommu_reclaim_pages(struct hyp_pool *pool, void *p, u8 order)
{
	/*
	 * Order MUST be same allocated page, however the buddy allocator
	 * is allowed to give higher order pages.
	 */
	BUG_ON(order > hyp_virt_to_page(p)->order);

	hyp_put_page(pool, p);
}

void kvm_iommu_reclaim_pages(void *p, u8 order)
{
	__kvm_iommu_reclaim_pages(&iommu_host_pool, p, order);
}

void *kvm_iommu_donate_pages_iopt(u8 order, bool request, void *cookie)
{
	if (cookie && ((struct kvm_iommu_tlb_cookie *)cookie)->domain_id ==
	    KVM_IOMMU_DOMAIN_IDMAP_ID) {
		return hyp_alloc_pages(&iommu_idmap_pool, order);
	}
	return kvm_iommu_donate_pages(order, request);
}

void kvm_iommu_reclaim_pages_iopt(void *p, u8 order, void *cookie)
{
	if (cookie && ((struct kvm_iommu_tlb_cookie *)cookie)->domain_id ==
	    KVM_IOMMU_DOMAIN_IDMAP_ID) {
		__kvm_iommu_reclaim_pages(&iommu_idmap_pool, p, order);
	}
	kvm_iommu_reclaim_pages(p, order);
}

/* Request to hypervisor. */
int kvm_iommu_request(struct kvm_hyp_req *req)
{
	struct kvm_hyp_req *cur_req = this_cpu_ptr(&host_hyp_reqs);

	if (cur_req->type != KVM_HYP_REQ_EMP)
		return -EBUSY;

	memcpy(cur_req, req, sizeof(struct kvm_hyp_req));

	return 0;
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
	void *p;
	struct hyp_page *page;

	while (target--) {
		p = hyp_alloc_pages(&iommu_host_pool, 0);
		if (!p)
			return;
		page = hyp_virt_to_page(p);
		push_hyp_memcache(host_mc, p, hyp_virt_to_phys, page->order);
		WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(p), 1 << page->order));
		memset(page, 0, sizeof(struct hyp_page));
	}
}

int kvm_iommu_reclaimable(void)
{
	return hyp_pool_free_pages(&iommu_host_pool);
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
		domains = kvm_iommu_donate_pages(0, true);
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

int kvm_iommu_alloc_domain(pkvm_handle_t domain_id)
{
	int ret = -EINVAL;
	struct kvm_hyp_iommu_domain *domain;

	hyp_spin_lock(&iommu_domains_lock);
	domain = handle_to_domain(domain_id);
	if (!domain)
		goto out_unlock;

	if (atomic_read(&domain->refs))
		goto out_unlock;

	ret = kvm_iommu_ops->alloc_domain(domain, domain_id);
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

	if (domain_id == KVM_IOMMU_DOMAIN_IDMAP_ID)
		return -EINVAL;

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

	/* Protected against parallel attach with iommu_domains_lock*/
	if (domain_id == KVM_IOMMU_DOMAIN_IDMAP_ID) {
		host_lock_component();
		snapshot_host_stage2();
		host_unlock_component();
		kvm_iommu_idmap_init_done();
	}
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

	if (domain_id == KVM_IOMMU_DOMAIN_IDMAP_ID)
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

	ret = __pkvm_host_use_dma(paddr, size, !(prot & IOMMU_MMIO));
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
	 * unuse the bits that haven't been mapped yet. The host calls back
	 * either to continue mapping, or to unmap and unuse what's been done
	 * so far.
	 */
	if (pgcount)
		__pkvm_host_unuse_dma(paddr, pgcount * pgsize);
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

	if (domain_id == KVM_IOMMU_DOMAIN_IDMAP_ID)
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
		 * One page/block at a time so that we can unuse each page.
		 * The IOVA range provided may not be physically contiguous, and
		 * @pgsize may be larger than the one used when mapping.
		 */
		unmapped = iopt_unmap_leaf(&iopt, iova, pgsize, &paddr);
		if (!unmapped || !paddr)
			goto out_put_domain;

		ret = __pkvm_host_unuse_dma(paddr, unmapped);
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

	if (domain_id == KVM_IOMMU_DOMAIN_IDMAP_ID)
		return iova;

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
	bool prev;
	int ret;

	hyp_spin_lock(&iommu->lock);
	prev = iommu->power_is_off;
	iommu->power_is_off = false;
	ret = kvm_iommu_ops->resume ? kvm_iommu_ops->resume(iommu) : 0;
	if (ret)
		iommu->power_is_off = prev;
	hyp_spin_unlock(&iommu->lock);
	return ret;
}

static int iommu_power_off(struct kvm_power_domain *pd)
{
	struct kvm_hyp_iommu *iommu = container_of(pd, struct kvm_hyp_iommu,
						   power_domain);
	bool prev;
	int ret;

	hyp_spin_lock(&iommu->lock);
	prev = iommu->power_is_off;
	iommu->power_is_off = true;
	ret = kvm_iommu_ops->suspend ? kvm_iommu_ops->suspend(iommu) : 0;
	if (ret)
		iommu->power_is_off = prev;
	hyp_spin_unlock(&iommu->lock);
	return ret;
}

static const struct kvm_power_domain_ops iommu_power_ops = {
	.power_on	= iommu_power_on,
	.power_off	= iommu_power_off,
};

int kvm_iommu_init_device(struct kvm_hyp_iommu *iommu)
{
	return pkvm_init_power_domain(&iommu->power_domain, &iommu_power_ops);
}

int kvm_iommu_init(struct kvm_iommu_ops *ops, struct kvm_hyp_memcache *idmap_mc,
		   unsigned long init_arg)
{
	int ret;
	void *p;
	u8 order;

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

	/* Driver want's to opt in for idmapped devices. */
	if (idmap_mc->head) {
		ret = hyp_pool_init(&iommu_idmap_pool, 0,
				    64 /* order = 6*/, 0, true);
		if (ret)
			return ret;
		while (idmap_mc->nr_pages) {
			order = idmap_mc->head & (PAGE_SIZE - 1);
			p = pkvm_admit_host_page(idmap_mc, order);
			hyp_set_page_refcounted(hyp_virt_to_page(p));
			hyp_virt_to_page(p)->order = order;
			hyp_put_page(&iommu_idmap_pool, p);
		}

		/* A bit hacky way to populate first domain to be used immediately. */
		kvm_hyp_iommu_domains[0] = hyp_alloc_pages(&iommu_idmap_pool, 0);
		kvm_iommu_ops = ops;
		ret = kvm_iommu_alloc_domain(KVM_IOMMU_DOMAIN_IDMAP_ID);
	} else {
		kvm_iommu_ops = ops;
	}

	if (ret)
		kvm_iommu_ops = NULL;
	return ret;
}

void __kvm_iommu_host_stage2_idmap(phys_addr_t start, phys_addr_t end,
				   enum kvm_pgtable_prot prot)
{
	int pgcount = (end - start) >> PAGE_SHIFT;
	size_t mapped, unmapped;
	int ret;
	struct io_pgtable iopt;
	struct kvm_hyp_iommu_domain *domain;

	domain = handle_to_domain(KVM_IOMMU_DOMAIN_IDMAP_ID);
	iopt = domain_to_iopt(domain, KVM_IOMMU_DOMAIN_IDMAP_ID);
	if (prot) {
		while (pgcount) {
			mapped = 0;
			ret = iopt_map_pages(&iopt, start, start, PAGE_SIZE, pgcount, prot,
					     0, &mapped);
			pgcount -= mapped / PAGE_SIZE;
			start += mapped;
			if (!mapped || ret)
				return;
		}
	} else {
		while (pgcount) {
			unmapped = iopt_unmap_pages(&iopt, start, PAGE_SIZE, pgcount, NULL);
			pgcount -= unmapped / PAGE_SIZE;
			start += unmapped;
			if (!unmapped)
				return;
		}
	}
}

void kvm_iommu_host_stage2_idmap(phys_addr_t start, phys_addr_t end,
				 enum kvm_pgtable_prot prot)
{
	if (!kvm_iommu_is_ready())
		return;
	__kvm_iommu_host_stage2_idmap(start, end, prot);
}

static int __snapshot_host_stage2(const struct kvm_pgtable_visit_ctx *ctx,
				  enum kvm_pgtable_walk_flags visit)
{
	u64 start = ctx->addr;
	kvm_pte_t pte = *ctx->ptep;
	u32 level = ctx->level;
	u64 end = start + kvm_granule_size(level);
	enum kvm_pgtable_prot prot;

	if ((!pte || kvm_pte_valid(pte)) && addr_is_memory(start)) {
		prot = PKVM_HOST_MEM_PROT;
		__kvm_iommu_host_stage2_idmap(start, end, prot);
	}

	return 0;
}

static int snapshot_host_stage2(void)
{
	struct kvm_pgtable_walker walker = {
		.cb	= __snapshot_host_stage2,
		.flags	= KVM_PGTABLE_WALK_LEAF,
	};
	struct kvm_pgtable *pgt = &host_mmu.pgt;

	/*
	 * We only snapshot memory now, as the MMIO regions are unknown to
	 * the hypervisor.
	 * Unmap all MMIO, so they are faulted again and then we can map them in
	 * the host page table.
	 * This is not opimal but should work for now.
	 */
	host_stage2_unmap_unmoveable_regs();

	return kvm_pgtable_walk(pgt, 0, BIT(pgt->ia_bits), &walker);
}
