// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC
 * Author: Mostafa Saleh <smostafa@google.com>
 */

#include <kvm/arm_hypercalls.h>
#include <kvm/device.h>

#include <nvhe/mem_protect.h>

struct pkvm_device *registered_devices;
unsigned long registered_devices_nr;

/*
 * This lock protects all devices in registered_devices when ctxt changes,
 * this is overlocking and can be improved. However, this device context
 * only changes at boot time and at teardown and in theory there shouldn't
 * be congestion on that path, so it's easier to reason about one big lock.
 */
static DEFINE_HYP_SPINLOCK(device_spinlock);

int pkvm_init_devices(void)
{
	size_t dev_sz;
	int ret;

	if (!registered_devices_nr)
		return -ENODEV;
	registered_devices = kern_hyp_va(registered_devices);
	dev_sz = PAGE_ALIGN(size_mul(sizeof(struct pkvm_device),
				     registered_devices_nr));
	ret = __pkvm_host_donate_hyp(hyp_virt_to_phys(registered_devices) >> PAGE_SHIFT,
				     dev_sz >> PAGE_SHIFT);
	if (ret)
		registered_devices_nr = 0;

	return ret;
}

static struct pkvm_device *pkvm_get_device(u64 addr, u64 size)
{
	struct pkvm_device *dev = NULL;
	struct pkvm_dev_resource *res;
	int i, j;

	for (i = 0 ; i < registered_devices_nr ; ++i) {
		dev = &registered_devices[i];
		for (j = 0 ; j < dev->nr_resources; ++j) {
			res = &dev->resources[j];
			if ((addr >= res->base) && (addr + size <= (res->base + res->size)))
				return dev;
		}
	}

	return NULL;
}

bool pkvm_device_is_assignable(u64 pfn)
{
	return pkvm_get_device(hyp_pfn_to_phys(pfn), PAGE_SIZE) != NULL;
}

static void __pkvm_device_reclaim(struct pkvm_device *dev)
{
	dev->ctxt = NULL;
	return 0;
}

static int __pkvm_device_assign(struct pkvm_device *dev, struct pkvm_hyp_vm *vm)
{
	int i;
	struct pkvm_dev_resource *res;
	int ret;

	for (i = 0 ; i < dev->nr_resources; ++i) {
		res = &dev->resources[i];
		ret = hyp_check_range_owned(res->base, res->size);
		if (ret)
			return ret;
	}

	dev->ctxt = vm;
	return 0;
}

static int __pkvm_group_assign(u32 group_id, struct pkvm_hyp_vm *vm)
{
	int i;
	int ret = 0;

	for (i = 0 ; i < registered_devices_nr ; ++i) {
		if (registered_devices[i].group_id != group_id)
			continue;
		if (registered_devices[i].ctxt) {
			ret = -EPERM;
			break;
		}
		ret = __pkvm_device_assign(&registered_devices[i], vm);
		if (ret)
			break;
	}

	if (ret) {
		while (i--) {
			__pkvm_device_reclaim(&registered_devices[i]);
		}
	}
	return ret;
}

int pkvm_device_assign(u64 addr, u64 size, struct pkvm_hyp_vm *vm)
{
	struct pkvm_device *dev;
	int ret = 0;

	if (!vm)
		return -EINVAL;

	dev = pkvm_get_device(addr, size);
	if (!dev)
		return -ENODEV;

	hyp_spin_lock(&device_spinlock);
	if (dev->ctxt == NULL) {
		/*
		 * First time device is assigned to guest, make sure it's resources
		 * have been donated.
		 */
		__pkvm_group_assign(dev->group_id, vm);
	}

	hyp_spin_unlock(&device_spinlock);

	return ret;
}

static int pkvm_get_device_pa(struct pkvm_hyp_vcpu *hyp_vcpu, u64 ipa, u64 ipa_size,
			      u64 *pa, u64 *size, u64 *exit_code)
{
	struct kvm_hyp_req *req;
	int ret;
	u64 pte;
	u32 level;
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);

	ret = kvm_pgtable_get_leaf(&vm->pgt, ipa, &pte, &level);
	if (ret || !kvm_pte_valid(pte)) {
		/* Page not mapped, create a request*/
		req = pkvm_hyp_req_reserve(hyp_vcpu, KVM_HYP_REQ_TYPE_MAP);
		if (!req)
			return -ENOMEM;

		req->map.guest_ipa = ipa;
		req->map.size = ipa_size;
		*exit_code = ARM_EXCEPTION_HYP_REQ;
		/* Repeat next time. */
		write_sysreg_el2(read_sysreg_el2(SYS_ELR) - 4, SYS_ELR);
		return -ENODEV;
	}
	*size = kvm_granule_size(level);
	*pa = kvm_pte_to_phys(pte);
	return 0;
}

bool pkvm_device_request_mmio(struct pkvm_hyp_vcpu *hyp_vcpu, u64 *exit_code)
{
	int i, j, ret;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);
	struct pkvm_dev_resource *res;
	struct pkvm_device *dev;
	u64 ipa = smccc_get_arg1(vcpu);
	u64 size = smccc_get_arg2(vcpu);
	u64 pa, pa_size, total_size = 0, next_pa = 0;
	u64 token;

	do {
		ret = pkvm_get_device_pa(hyp_vcpu, ipa, size - total_size,
					 &pa, &pa_size, exit_code);
		if (ret)
			return false;

		/* The region is not contiguous in PA space. */
		if (total_size && (next_pa != pa)) {
			smccc_set_retval(vcpu, SMCCC_RET_INVALID_PARAMETER, 0, 0, 0);
			return true;
		}

		total_size += pa_size;
		ipa += pa_size;
		next_pa = pa + pa_size;
	} while (total_size < size);

	token = pa + pa_size - total_size;

	hyp_spin_lock(&device_spinlock);
	for (i = 0 ; i < registered_devices_nr ; ++i) {
		dev = &registered_devices[i];
		if (dev->ctxt != vm)
			continue;

		for (j = 0 ; j < dev->nr_resources; ++j) {
			res = &dev->resources[j];
			if ((res->base == token) && (res->size == size)) {
				smccc_set_retval(vcpu, SMCCC_RET_SUCCESS, token, 0, 0);
				goto out_ret;
			}
		}
	}

	smccc_set_retval(vcpu, SMCCC_RET_INVALID_PARAMETER, 0, 0, 0);
out_ret:
	hyp_spin_unlock(&device_spinlock);
	return true;
}

void pkvm_devices_teardown(struct pkvm_hyp_vm *vm)
{
	int i;

	hyp_spin_lock(&device_spinlock);
	for (i = 0 ; i < registered_devices_nr ; ++i) {
		if (registered_devices[i].ctxt != vm)
			continue;
		registered_devices[i].ctxt = NULL;
	}
	hyp_spin_unlock(&device_spinlock);
}

static struct pkvm_device *pkvm_get_device_by_iommu(u64 id, u64 endpoint)
{
	struct pkvm_device *dev = NULL;
	struct pkvm_dev_iommu *iommu;
	int i, j;

	for (i = 0 ; i < registered_devices_nr ; ++i) {
		dev = &registered_devices[i];
		for (j = 0 ; j < dev->nr_iommus; ++j) {
			iommu = &dev->iommus[j];
			if ((id == iommu->id) && (endpoint == iommu->endpoint))
				return dev;
		}
	}

	return NULL;
}

/*
 * Check if a VM or host(NULL) is allowed to access this IOMMU.
 * We should prevent VMs attaching to each other devices, and host
 * attaching to pVM, while allow host attaching to non protected
 * VMs.
 * dev->ctxt is only set for protected VMs.
 */
bool pkvm_devices_iommu_vcpu_allowed(u64 id, u64 endpoint, struct pkvm_hyp_vcpu *vcpu)
{
	struct pkvm_device *dev;
	struct pkvm_hyp_vm *vm = NULL;
	bool ret;

	dev = pkvm_get_device_by_iommu(id, endpoint);

	/* Not assignable device, only allowed to host */
	if (!dev)
		return !vcpu;

	/*
	 * We treat non protected vcpus as the host, as they don't go through the
	 * device assignment flow and the host controls the VM view of the physical
	 * IOMMUs so it can prevent it from attaching to any device.
	 */
	if (vcpu && pkvm_hyp_vcpu_is_protected(vcpu))
		vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);


	hyp_spin_lock(&device_spinlock);
	ret = (dev->ctxt == vm);
	hyp_spin_unlock(&device_spinlock);

	return ret;
}
