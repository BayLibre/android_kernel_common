// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2025 Google. */


#include <linux/hashtable.h>
#include <asm/pkvm_spinlock.h>
#include <pkvm.h>
#include "pkvm_hyp.h"
#include "debug.h"
#include "iommu_internal.h"
#include "iommu.h"
#include "iommu_domain.h"
#include "bug.h"

/*
 * TODO: Make this a dynamic value.
 */
#define MAX_IOMMU_DOMAIN_NUM	128
static DEFINE_HASHTABLE(iommu_domain_hasht, 8);
static DECLARE_BITMAP(iommu_domains_bitmap, MAX_IOMMU_DOMAIN_NUM);
static struct pkvm_iommu_domain iommu_domains[MAX_IOMMU_DOMAIN_NUM];
static pkvm_spinlock_t iommu_domain_lock = __PKVM_SPINLOCK_UNLOCKED;

static inline struct pkvm_iommu_domain *__pkvm_get_iommu_domain_locked(u64 pgd)
{
	struct pkvm_iommu_domain *domain = NULL, *tmp;

	hash_for_each_possible(iommu_domain_hasht, tmp, hnode, pgd) {
		if (tmp->pgd == pgd) {
			domain = atomic_inc_not_zero(&tmp->refcount) ? tmp : NULL;
			if (domain)
				break;
		}
	}

	return domain;
}

struct pkvm_iommu_domain *pkvm_get_iommu_domain(u64 pgd)
{
	struct pkvm_iommu_domain *domain;
	pkvm_spin_lock(&iommu_domain_lock);

	domain = __pkvm_get_iommu_domain_locked(pgd);

	pkvm_spin_unlock(&iommu_domain_lock);

	return domain;
}

void pkvm_put_iommu_domain(struct pkvm_iommu_domain *domain)
{
	if (!atomic_dec_and_test(&domain->refcount))
		return;

	pkvm_spin_lock(&iommu_domain_lock);

	hlist_del(&domain->hnode);

	__clear_bit(domain->index, iommu_domains_bitmap);

	memset(domain, 0, sizeof(struct pkvm_iommu_domain));

	pkvm_spin_unlock(&iommu_domain_lock);
}

struct pkvm_iommu_domain *pkvm_alloc_iommu_domain(u64 pgd)
{
	struct pkvm_iommu_domain *domain = NULL;
	unsigned long index;

	pkvm_spin_lock(&iommu_domain_lock);

	domain = __pkvm_get_iommu_domain_locked(pgd);
	if (domain)
		goto out;

	index = find_next_zero_bit(iommu_domains_bitmap, MAX_IOMMU_DOMAIN_NUM, 0);
	if (index < MAX_IOMMU_DOMAIN_NUM) {
		__set_bit(index, iommu_domains_bitmap);
		domain = &iommu_domains[index];
		domain->pgd = pgd;
		domain->index = index;
		atomic_set(&domain->refcount, 1);
		pkvm_spin_lock_init(&domain->lock);
		hash_add(iommu_domain_hasht, &domain->hnode, pgd);
	}

out:
	pkvm_spin_unlock(&iommu_domain_lock);

	return domain;
}

void pkvm_free_iommu_domain(u64 pgd)
{
	struct pkvm_iommu_domain *domain = NULL;

	pkvm_spin_lock(&iommu_domain_lock);
	hash_for_each_possible(iommu_domain_hasht, domain, hnode, pgd) {
		if (domain->pgd == pgd)
			break;
	}
	pkvm_spin_unlock(&iommu_domain_lock);

	if (domain && domain->pgd == pgd)
		pkvm_put_iommu_domain(domain);
}
