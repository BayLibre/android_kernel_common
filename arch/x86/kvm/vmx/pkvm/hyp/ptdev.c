// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022 Intel Corporation. */

#include <linux/hashtable.h>
#include <asm/pkvm_spinlock.h>
#include <pkvm.h>
#include "pkvm_hyp.h"
#include "iommu.h"
#include "ptdev.h"
#include "iommu_spgt.h"
#include "bug.h"
#include "debug.h"
#include "memory.h"
#include <pkvm/vmx/vmx.h>

#define MAX_PTDEV_INFO_NUM	(PKVM_MAX_PDEV_NUM + PKVM_MAX_PASID_PDEV_NUM)
#define MAX_PTDEV_NUM	PKVM_MAX_PDEV_NUM
static DEFINE_HASHTABLE(ptdev_hasht, 8);
static DECLARE_BITMAP(ptdevs_bitmap, MAX_PTDEV_NUM);
static struct pkvm_ptdev pkvm_ptdev[MAX_PTDEV_NUM];
static pkvm_spinlock_t ptdev_lock = __PKVM_SPINLOCK_UNLOCKED;

static DECLARE_BITMAP(ptdev_info_bitmap, MAX_PTDEV_INFO_NUM);
static struct ptdev_info pkvm_ptdev_info[MAX_PTDEV_INFO_NUM];

struct pkvm_ptdev *pkvm_alloc_ptdev(u16 bdf, bool coherency)
{
	struct pkvm_ptdev *ptdev = NULL;
	unsigned long index;

	pkvm_spin_lock(&ptdev_lock);

	index = find_next_zero_bit(ptdevs_bitmap, MAX_PTDEV_NUM, 0);
	if (index < MAX_PTDEV_NUM) {
		__set_bit(index, ptdevs_bitmap);
		ptdev = &pkvm_ptdev[index];
		ptdev->bdf = bdf;
		ptdev->iommu_coherency = coherency;
		ptdev->index = index;
		atomic_set(&ptdev->refcount, 1);
		pkvm_spin_lock_init(&ptdev->lock);
		hash_init(ptdev->devinfo_hash);
		hash_add(ptdev_hasht, &ptdev->hnode, bdf);
		INIT_LIST_HEAD(&ptdev->domain_node);
	}

	pkvm_spin_unlock(&ptdev_lock);

	return ptdev;
}

struct pkvm_ptdev *pkvm_get_ptdev(u16 bdf)
{
	struct pkvm_ptdev *ptdev = NULL, *tmp;

	pkvm_spin_lock(&ptdev_lock);

	hash_for_each_possible(ptdev_hasht, tmp, hnode, bdf) {
		if (match_ptdev(tmp, bdf)) {
			ptdev = atomic_inc_not_zero(&tmp->refcount) ? tmp : NULL;
			if (ptdev)
				break;
		}
	}

	pkvm_spin_unlock(&ptdev_lock);
	return ptdev;
}

void pkvm_put_ptdev(struct pkvm_ptdev *ptdev)
{
	if (!atomic_dec_and_test(&ptdev->refcount))
		return;

	pkvm_spin_lock(&ptdev_lock);

	hlist_del(&ptdev->hnode);

	__clear_bit(ptdev->index, ptdevs_bitmap);

	memset(ptdev, 0, sizeof(struct pkvm_ptdev));

	pkvm_spin_unlock(&ptdev_lock);
}

struct ptdev_info *alloc_ptdev_info(struct pkvm_ptdev *ptdev, u32 pasid)
{
	struct ptdev_info *ptdev_info = NULL;
	unsigned long index;

	pkvm_spin_lock(&ptdev->lock);

	index = find_next_zero_bit(ptdev_info_bitmap, MAX_PTDEV_INFO_NUM, 0);
	if (index < MAX_PTDEV_INFO_NUM) {
		__set_bit(index, ptdev_info_bitmap);
		ptdev_info = &pkvm_ptdev_info[index];
		ptdev_info->pasid = pasid;
		ptdev_info->index = index;
		ptdev_info->pgt = pkvm_hyp->host_vm.ept;
		ptdev_info->ptdev = ptdev;
		INIT_LIST_HEAD(&ptdev_info->vm_node);
		INIT_LIST_HEAD(&ptdev_info->iommu_node);
		atomic_set(&ptdev_info->refcount, 1);
		pkvm_spin_lock_init(&ptdev_info->lock);
		hash_add(ptdev->devinfo_hash, &ptdev_info->hnode, pasid);
	}

	pkvm_spin_unlock(&ptdev->lock);

	return ptdev_info;
}

struct ptdev_info *pkvm_alloc_ptdev_info(u16 bdf, u32 pasid, bool coherency)
{
	struct pkvm_ptdev *ptdev = pkvm_get_ptdev(bdf);
	struct ptdev_info *ptdev_info;

	if (!ptdev) {
		ptdev = pkvm_alloc_ptdev(bdf, coherency);
		if (!ptdev)
			return NULL;
	}

	ptdev_info = alloc_ptdev_info(ptdev, pasid);
	if (!ptdev_info) {
		pkvm_put_ptdev(ptdev);
		return NULL;
	}

	return ptdev_info;
}

struct ptdev_info *pkvm_get_ptdev_info(u16 bdf, u32 pasid)
{
	struct ptdev_info *ptdev_info = NULL, *tmp;
	struct pkvm_ptdev *ptdev;

	ptdev = pkvm_get_ptdev(bdf);
	if (!ptdev)
		return NULL;

	pkvm_spin_lock(&ptdev->lock);

	hash_for_each_possible(ptdev->devinfo_hash, tmp, hnode, pasid) {
		if (match_ptdev_info(tmp, pasid)) {
			ptdev_info = atomic_inc_not_zero(&tmp->refcount) ? tmp : NULL;
			if (ptdev_info)
				break;
		}
	}

	pkvm_spin_unlock(&ptdev->lock);
	if (!ptdev_info)
		pkvm_put_ptdev(ptdev);

	return ptdev_info;
}

void pkvm_put_ptdev_info(struct ptdev_info *ptdev_info)
{
	struct pkvm_ptdev *ptdev;

	if (!atomic_dec_and_test(&ptdev_info->refcount))
		return;

	ptdev = ptdev_info->ptdev;
	pkvm_spin_lock(&ptdev->lock);

	hlist_del(&ptdev_info->hnode);

	__clear_bit(ptdev_info->index, ptdev_info_bitmap);

	if (ptdev_info->pgt != pkvm_hyp->host_vm.ept)
		pkvm_put_host_iommu_spgt(ptdev_info->pgt, ptdev->iommu_coherency);

	memset(ptdev_info, 0, sizeof(struct ptdev_info));
	pkvm_spin_unlock(&ptdev->lock);

	pkvm_put_ptdev(ptdev);
}

void pkvm_setup_ptdev_vpgt(struct ptdev_info *ptdev_info, unsigned long root_gpa,
			   struct pkvm_mm_ops *mm_ops, struct pkvm_pgtable_ops *paging_ops,
			   struct pkvm_pgtable_cap *cap, bool shadowed)
{
	struct pkvm_ptdev *ptdev;

	pkvm_spin_lock(&ptdev_info->lock);
	ptdev = ptdev_info->ptdev;

	if (ptdev_info->pgt != pkvm_hyp->host_vm.ept &&
			(!shadowed || root_gpa != ptdev_info->vpgt.root_pa) &&
			!ptdev_attached_to_vm(ptdev_info)) {
		pkvm_put_host_iommu_spgt(ptdev_info->pgt, ptdev->iommu_coherency);
		ptdev_info->pgt = pkvm_hyp->host_vm.ept;
	}

	if (!root_gpa || root_gpa == INVALID_ADDR || !mm_ops || !paging_ops || !cap) {
		memset(&ptdev_info->vpgt, 0, sizeof(struct pkvm_pgtable));
		goto out;
	}

	ptdev_info->vpgt.root_pa = root_gpa;
	PKVM_ASSERT(pkvm_pgtable_init(&ptdev_info->vpgt, mm_ops, paging_ops, cap, false) == 0);

	if (shadowed && ptdev_info->pgt == pkvm_hyp->host_vm.ept) {
		ptdev_info->pgt = pkvm_get_host_iommu_spgt(root_gpa, ptdev->iommu_coherency);
		PKVM_ASSERT(ptdev_info->pgt);
	}
out:
	pkvm_spin_unlock(&ptdev_info->lock);
}

void pkvm_setup_ptdev_did(struct ptdev_info *ptdev_info, u16 did)
{
	ptdev_info->did = did;
}

/*
 * pkvm_detach_ptdev()	- detach a ptdev from the shadow VM it is attached.
 * Basically it reverts what pkvm_attach_ptdev() does.
 *
 * @ptdev:	The target ptdev.
 * @vm:		The shadow VM which will be attached to.
 */
void pkvm_detach_ptdev(struct ptdev_info *ptdev_info, struct pkvm_shadow_vm *vm)
{
	/* Reset what the attach API has set */
	pkvm_spin_lock(&ptdev_info->lock);
	ptdev_info->shadow_vm_handle = 0;
	ptdev_info->pgt = pkvm_hyp->host_vm.ept;
	pkvm_spin_unlock(&ptdev_info->lock);

	pkvm_shadow_vm_unlink_ptdev(vm, &ptdev_info->vm_node,
				    ptdev_info->ptdev->iommu_coherency);
	pkvm_iommu_sync(ptdev_info->ptdev->bdf, ptdev_info->pasid);

	pkvm_put_ptdev_info(ptdev_info);
}

/*
 * pkvm_attach_ptdev() - attach a ptdev to a shadow VM so it will be isolated
 * from the primary VM.
 *
 * @bdf:	The bdf of this ptdev.
 * @pasid:	The pasid of this ptdev.
 * @vm:		The shadow VM which will be attached to.
 *
 * FIXME:
 * The passthrough devices attached to the protected VM is relying on KVM
 * high to send vmcall so that pKVM can know which device should be isolated.
 * But if KVM high has created a passthrough device for a protected VM without
 * using this vmcall to notify pKVM, pKVM should still be able to isolate this
 * passthrough device. To guarantee this, either needs pKVM to know the
 * passthrough devices information to isolate them independently or needs
 * protected VM to check with pKVM about its passthrough device info through
 * some vmcall. Currently neither way is available.
 */
int pkvm_attach_ptdev(u16 bdf, u32 pasid, struct pkvm_shadow_vm *vm)
{
	struct ptdev_info *ptdev_info = pkvm_get_ptdev_info(bdf, pasid);
	struct pkvm_ptdev *ptdev;
	int vm_handle;

	if (!ptdev_info) {
		ptdev_info = pkvm_alloc_ptdev_info(bdf, pasid, pkvm_iommu_coherency(bdf));
		if (!ptdev_info)
			return -ENODEV;
	}

	pkvm_spin_lock(&ptdev_info->lock);
	ptdev = ptdev_info->ptdev;

	vm_handle = shadow_to_kvm(vm)->arch.pkvm.pkvm_vm_handle;
	if (cmpxchg(&ptdev_info->shadow_vm_handle, 0, vm_handle) != 0) {
		pkvm_err("%s: ptdev with bdf 0x%x pasid 0x%x is already attached\n",
			 __func__, bdf, pasid);
		pkvm_spin_unlock(&ptdev_info->lock);
		pkvm_put_ptdev_info(ptdev_info);
		return -ENODEV;
	}

	PKVM_ASSERT(ptdev_info->pgt != &vm->pgstate_pgt);
	if (ptdev_info->pgt != pkvm_hyp->host_vm.ept)
		pkvm_put_host_iommu_spgt(ptdev_info->pgt, ptdev->iommu_coherency);

	/*
	 * Reset pgt of this ptdev to VM's pgstate_pgt so need to update
	 * IOMMU page table accordingly.
	 */
	ptdev_info->pgt = &vm->pgstate_pgt;

	pkvm_spin_unlock(&ptdev_info->lock);

	pkvm_shadow_vm_link_ptdev(vm, &ptdev_info->vm_node,
				  ptdev->iommu_coherency);
	if (pkvm_iommu_sync(ptdev->bdf, ptdev_info->pasid)) {
		pkvm_detach_ptdev(ptdev_info, vm);
		return -ENODEV;
	}

	return 0;
}
