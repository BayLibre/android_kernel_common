/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022 Intel Corporation. */

#ifndef _PKVM_PTDEV_H_
#define _PKVM_PTDEV_H_

#include "pkvm_hyp.h"
#include "pgtable.h"

struct pkvm_device {
	atomic_t refcount;
	struct hlist_node hnode;
	u16 bdf;
	unsigned long index;
	bool iommu_coherency;

	pkvm_spinlock_t lock;

	DECLARE_HASHTABLE(ptdev_hash, 8);

	u32 max_pasid;
	void *pasid_table;
};

struct pkvm_ptdev {
	atomic_t refcount;
	unsigned long index;
	pkvm_spinlock_t lock;
	u32 pasid;
	u16 did;
	int shadow_vm_handle;
	struct list_head vm_node;
	struct list_head iommu_node;

	/* Represents the page table maintained by primary VM */
	struct pkvm_pgtable vpgt;
	/* Represents the page table maintained by pKVM */
	struct pkvm_pgtable *pgt;

	struct hlist_node hnode;
	struct pkvm_device *dev;
};

struct pkvm_device *pkvm_alloc_device(u16 bdf, bool coherency);
struct pkvm_device *pkvm_get_device(u16 bdf);
void pkvm_put_device(struct pkvm_device *dev);

struct pkvm_ptdev *pkvm_alloc_ptdev(u16 bdf, u32 pasid, bool coherency);
struct pkvm_ptdev *pkvm_get_ptdev(u16 bdf, u32 pasid);
void pkvm_put_ptdev(struct pkvm_ptdev *ptdev);

void pkvm_setup_ptdev_vpgt(struct pkvm_ptdev *ptdev, unsigned long root_gpa,
			   const struct pkvm_mm_ops *mm_ops,
			   const struct pkvm_pgtable_ops *paging_ops,
			   const struct pkvm_pgtable_cap *cap, bool shadowed);
void pkvm_setup_ptdev_did(struct pkvm_ptdev *ptdev, u16 did);
void pkvm_detach_ptdev(struct pkvm_ptdev *ptdev, struct pkvm_shadow_vm *vm);
int pkvm_attach_ptdev(u16 bdf, u32 pasid, struct pkvm_shadow_vm *vm);

static inline bool match_pkvm_ptdev(struct pkvm_ptdev *ptdev, u32 pasid)
{
	return ptdev && (ptdev->pasid == pasid);
}

static inline bool match_pkvm_device(struct pkvm_device *dev, u16 bdf)
{
	return dev && (dev->bdf == bdf);
}

static inline bool ptdev_attached_to_vm(struct pkvm_ptdev *ptdev)
{
	/* Attached ptdev has non-zero shadow_vm_handle */
	return cmpxchg(&ptdev->shadow_vm_handle, 0, 0) != 0;
}
#endif
