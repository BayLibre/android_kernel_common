// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC
 * Author: Mostafa Saleh <smostafa@google.com>
 */

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
