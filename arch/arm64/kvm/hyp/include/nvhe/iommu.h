/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM64_KVM_NVHE_IOMMU_H__
#define __ARM64_KVM_NVHE_IOMMU_H__

#include <asm/kvm_pgtable.h>

#include <kvm/iommu.h>
#include <linux/io-pgtable.h>

#if IS_ENABLED(CONFIG_ARM_SMMU_V3_PKVM)
#include <linux/io-pgtable-arm.h>

int kvm_arm_io_pgtable_init(struct io_pgtable_cfg *cfg,
			    struct arm_lpae_io_pgtable *data);
int kvm_arm_io_pgtable_alloc(struct io_pgtable *iop, unsigned long pgd_hva);
int kvm_arm_io_pgtable_free(struct io_pgtable *iop);
size_t kvm_arm_io_pgtable_size(struct io_pgtable *iopt);
#endif /* CONFIG_ARM_SMMU_V3_PKVM */

#if IS_ENABLED(CONFIG_KVM_IOMMU)
int kvm_iommu_init(struct kvm_iommu_ops *ops,
		   struct kvm_hyp_memcache *idmap_mc,
		   unsigned long init_arg);
int kvm_iommu_init_device(struct kvm_hyp_iommu *iommu);
void *kvm_iommu_donate_pages(u8 order, bool request);
void kvm_iommu_reclaim_pages(void *p, u8 order);
void *kvm_iommu_donate_pages_iopt(u8 order, bool request, void *cookie);
void kvm_iommu_reclaim_pages_iopt(void *p, u8 order, void *cookie);
int kvm_iommu_request(struct kvm_hyp_req *req);

/* Hypercall handlers */
int kvm_iommu_alloc_domain(pkvm_handle_t domain_id);
int kvm_iommu_free_domain(pkvm_handle_t domain_id);
int kvm_iommu_attach_dev(pkvm_handle_t iommu_id, pkvm_handle_t domain_id,
			 u32 endpoint_id);
int kvm_iommu_detach_dev(pkvm_handle_t iommu_id, pkvm_handle_t domain_id,
			 u32 endpoint_id);
size_t kvm_iommu_map_pages(pkvm_handle_t domain_id,
			   unsigned long iova, phys_addr_t paddr, size_t pgsize,
			   size_t pgcount, int prot);
size_t kvm_iommu_unmap_pages(pkvm_handle_t domain_id,
			     unsigned long iova, size_t pgsize, size_t pgcount);
phys_addr_t kvm_iommu_iova_to_phys(pkvm_handle_t domain_id, unsigned long iova);
void kvm_iommu_host_stage2_idmap(phys_addr_t start, phys_addr_t end,
				 enum kvm_pgtable_prot prot);
#else /* !CONFIG_KVM_IOMMU */
static inline int kvm_iommu_alloc_domain(pkvm_handle_t domain_id)
{
	return -ENODEV;
}

static inline int kvm_iommu_free_domain(pkvm_handle_t domain_id)
{
	return -ENODEV;
}

static inline int kvm_iommu_attach_dev(pkvm_handle_t iommu_id,
				       pkvm_handle_t domain_id,
				       u32 endpoint_id)
{
	return -ENODEV;
}

static inline int kvm_iommu_detach_dev(pkvm_handle_t iommu_id,
				       pkvm_handle_t domain_id,
				       u32 endpoint_id)
{
	return -ENODEV;
}

static inline size_t kvm_iommu_map_pages(pkvm_handle_t domain_id,
					 unsigned long iova, phys_addr_t paddr,
					 size_t pgsize, size_t pgcount, int prot)
{
	return 0;
}

static inline size_t kvm_iommu_unmap_pages(pkvm_handle_t domain_id,
					   unsigned long iova, size_t pgsize,
					   size_t pgcount)
{
	return 0;
}

static inline phys_addr_t kvm_iommu_iova_to_phys(pkvm_handle_t domain_id,
						 unsigned long iova)
{
	return 0;
}

static inline int kvm_iommu_init(struct kvm_iommu_ops *ops,
				 struct kvm_hyp_memcache *idmap_mc,
				 unsigned long init_arg)
{
	return -ENODEV;
}

static inline void kvm_iommu_host_stage2_idmap(phys_addr_t start, phys_addr_t end,
					       enum kvm_pgtable_prot prot)
{
}
#endif /* CONFIG_KVM_IOMMU */

struct kvm_iommu_tlb_cookie {
	pkvm_handle_t		domain_id;
	struct kvm_hyp_iommu_domain *domain;
};

struct kvm_iommu_ops {
	int (*init)(unsigned long arg);
	struct kvm_hyp_iommu *(*get_iommu_by_id)(pkvm_handle_t smmu_id);
	int (*free_domain)(struct kvm_hyp_iommu_domain *domain, pkvm_handle_t domain_id);
	int (*attach_dev)(struct kvm_hyp_iommu *iommu, pkvm_handle_t domain_id,
			  struct kvm_hyp_iommu_domain *domain, u32 endpoint_id);
	int (*detach_dev)(struct kvm_hyp_iommu *iommu, pkvm_handle_t domain_id,
			  struct kvm_hyp_iommu_domain *domain, u32 endpoint_id);
	int (*alloc_domain)(struct kvm_hyp_iommu_domain *domain, pkvm_handle_t domain_id);
	int (*suspend)(struct kvm_hyp_iommu *iommu);
	int (*resume)(struct kvm_hyp_iommu *iommu);
};

extern struct kvm_iommu_ops *kvm_iommu_ops;

#define domain_to_iopt(_domain, _domain_id)		\
	(struct io_pgtable) {					\
		.ops = &(_domain)->pgtable->ops,		\
		.pgd = (_domain)->pgd,				\
		.cookie = &(struct kvm_iommu_tlb_cookie) {	\
			.domain_id	= (_domain_id),		\
			.domain		= (_domain),		\
		},						\
	}

extern struct hyp_mgt_allocator_ops kvm_iommu_allocator_ops;

#endif /* __ARM64_KVM_NVHE_IOMMU_H__ */
