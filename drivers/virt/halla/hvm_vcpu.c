// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Samsung Electronics Co., Ltd.
 *            http://www.samsung.com/
 */

#include <linux/anon_inodes.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/percpu.h>

#include "exynos-hvc.h"
#include "hvm_drv.h"

/* maximum size needed for holding an integer */
#define ITOA_MAX_LEN 12

static long hvm_vcpu_update_one_reg(struct hvm_vcpu *vcpu,
				     void __user *argp,
				     bool is_write)
{
	struct hvm_one_reg reg;
	void __user *reg_addr;
	uint64_t data = 0;
	uint64_t reg_size;
	long ret;

	if (copy_from_user(&reg, argp, sizeof(reg)))
		return -EFAULT;

	reg_addr = u64_to_user_ptr(reg.addr);
	reg_size = (reg.id & HVM_REG_SIZE_MASK) >> HVM_REG_SIZE_SHIFT;
	reg_size = BIT(reg_size);

	if (reg_size != 1 && reg_size != 2 && reg_size != 4 && reg_size != 8)
		return -EINVAL;

	if (is_write) {
		/* Halla hypervisor would filter out invalid vcpu register access */
		if (copy_from_user(&data, reg_addr, reg_size))
			return -EFAULT;
	} else {
		return -EOPNOTSUPP;
	}

	if (is_write) {
		ret = exynos_hvc(HVC_FID_HVM_SET_ONE_REG,
				 vcpu->hvm->vm_id, vcpu->vcpuid,
				 reg.id, data);
	} else {
		/* Read is not supported yet */
		ret = exynos_hvc(HVC_FID_HVM_GET_ONE_REG,
				 vcpu->hvm->vm_id, vcpu->vcpuid,
				 reg.id, 0);
		data = ret;
	}

	if (ret)
		return ret;

	return 0;
}

/**
 * hvm_vcpu_handle_mmio() - Handle mmio in kernel space.
 * @vcpu: Pointer to vcpu.
 *
 * Return:
 * * true - This mmio exit has been processed.
 * * false - This mmio exit has not been processed, require userspace.
 */
static bool hvm_vcpu_handle_mmio(struct hvm_vcpu *vcpu)
{
	uint64_t addr;
	uint32_t len;
	const void *val_ptr;

	/* So far, we don't have in-kernel mmio read handler */
	if (!vcpu->run->mmio.is_write)
		return false;
	addr = vcpu->run->mmio.phys_addr;
	len = vcpu->run->mmio.size;
	val_ptr = &vcpu->run->mmio.data;

	return hvm_ioevent_write(vcpu, addr, len, val_ptr);
}

/**
 * hvm_handle_guest_exception() - Handle guest exception
 * @vcpu: Pointer to struct hvm_vcpu_run in userspace
 * Return:
 * * true - This exception has been processed, no need to back to VMM.
 * * false - This exception has not been processed, require userspace.
 */
static bool hvm_handle_guest_exception(struct hvm_vcpu *vcpu)
{
	int ret;

	for (int i = 0; i < ARRAY_SIZE(vcpu->run->exception.reserved); i++) {
		if (vcpu->run->exception.reserved[i])
			return false;
	}

	switch (vcpu->run->exception.exception) {
	case HVM_EXCEPTION_PAGE_FAULT:
		ret = hvm_handle_page_fault(vcpu);
		break;
	case HVM_EXCEPTION_UNKNOWN:
		fallthrough;
	default:
		ret = -EFAULT;
	}

	if (!ret)
		return true;
	else
		return false;
}

static void hvm_vcpu_load(struct hvm_vcpu *vcpu)
{
	uint32_t tuple = ((uint32_t)vcpu->hvm->vm_id << 16 | (uint16_t)vcpu->vcpuid);

	__this_cpu_write(*hvm_get_running_vcpus(), tuple);

	hvm_restore_vtimer(vcpu);
}

static void hvm_vcpu_put(struct hvm_vcpu *vcpu)
{
	hvm_save_vtimer(vcpu);

	__this_cpu_write(*hvm_get_running_vcpus(), (uint32_t)(~0));
}

static bool handle_guest_exit(struct hvm_vcpu *vcpu, uint64_t exit_reason)
{
	switch (exit_reason) {
	case HVM_EXIT_MMIO:
		if (!hvm_vcpu_handle_mmio(vcpu))
			return true;
		break;
	/**
	 * it's Halla's responsibility to fill corresponding data
	 * structure
	 */
	case HVM_EXIT_EXCEPTION:
		if (!hvm_handle_guest_exception(vcpu))
			return true;
		break;
	case HVM_EXIT_INTERNAL_ERROR:
		fallthrough;
	case HVM_EXIT_SYSTEM_EVENT:
		fallthrough;
	case HVM_EXIT_SHUTDOWN:
		return true;
	case HVM_EXIT_HALLA:
		fallthrough;
	case HVM_EXIT_IRQ:
		break;
	default:
		pr_err("vcpu unknown exit\n");
		return true;
	}

	return false;
}

/**
 * hvm_vcpu_run() - Handle vcpu run ioctl, entry point to guest and exit
 *		     point from guest
 * @vcpu: Pointer to struct hvm_vcpu
 * @argp: Pointer to struct hvm_vcpu_run in userspace
 *
 * Return:
 * * 0			- Success.
 * * Negative		- Failure.
 */
static long hvm_vcpu_run(struct hvm_vcpu *vcpu, void __user *argp)
{
	bool need_userspace = false;
	uint64_t exit_reason;

	if (copy_from_user(vcpu->run, argp, sizeof(struct hvm_vcpu_run)))
		return -EFAULT;

	for (int i = 0; i < ARRAY_SIZE(vcpu->run->pad); i++) {
		if (vcpu->run->pad[i])
			return -EINVAL;
	}

	if (vcpu->run->immediate_exit == 1)
		return -EINTR;

	while (!need_userspace && !signal_pending(current)) {
		preempt_disable();
		hvm_vcpu_load(vcpu);

		exit_reason = exynos_hvc(HVC_FID_HVM_VCPU_RUN,
				vcpu->hvm->vm_id, vcpu->vcpuid, 0, 0);

		local_irq_enable();
		isb();

		hvm_vcpu_put(vcpu);
		preempt_enable();

		need_userspace = handle_guest_exit(vcpu, exit_reason);
	}

	if (copy_to_user(argp, vcpu->run, sizeof(struct hvm_vcpu_run)))
		return -EFAULT;

	if (signal_pending(current)) {
		// invoke hvc to inform halla to map memory
		exynos_hvc(HVC_FID_HVM_NOTIFY_EXIT,
				vcpu->hvm->vm_id, 0, 0, 0);
		return -ERESTARTSYS;
	}

	return 0;
}

static long hvm_vcpu_ioctl(struct file *filp, unsigned int ioctl,
			    unsigned long arg)
{
	int ret = -ENOTTY;
	void __user *argp = (void __user *)arg;
	struct hvm_vcpu *vcpu = filp->private_data;

	switch (ioctl) {
	case HVM_VCPU_RUN:
		ret = hvm_vcpu_run(vcpu, argp);
		break;
	case HVM_GET_ONE_REG:
		/* !is_write */
		ret = -EOPNOTSUPP;
		break;
	case HVM_SET_ONE_REG:
		/* is_write */
		ret = hvm_vcpu_update_one_reg(vcpu, argp, true);
		break;
	default:
		break;
	}

	return ret;
}

static const struct file_operations hvm_vcpu_fops = {
	.unlocked_ioctl = hvm_vcpu_ioctl,
};

/* caller must hold the vm lock */
static void hvm_destroy_vcpu(struct hvm_vcpu *vcpu)
{
	if (!vcpu)
		return;

	/* Destroy vcpu in Halla */
	exynos_hvc(HVC_FID_HVM_DESTROY_VCPU,
		   ((vcpu->hvm->vm_id << 16) | vcpu->vcpuid),
		   0, 0, 0);

	/* clean guest's data */
	memset(vcpu->run, 0, HVM_VCPU_RUN_MAP_SIZE);
	free_pages_exact(vcpu->run, HVM_VCPU_RUN_MAP_SIZE);
	kfree(vcpu);
}

/**
 * hvm_destroy_vcpus() - Destroy all vcpus, caller has to hold the vm lock
 *
 * @hvm: vm struct that owns the vcpus
 */
void hvm_destroy_vcpus(struct hvm *hvm)
{
	int i;

	for (i = 0; i < HVM_MAX_VCPUS; i++) {
		hvm_destroy_vcpu(hvm->vcpus[i]);
		hvm->vcpus[i] = NULL;
	}
}

/* create_vcpu_fd() - Allocates an inode for the vcpu. */
static int create_vcpu_fd(struct hvm_vcpu *vcpu)
{
	/* sizeof("hvm-vcpu:") + max(strlen(itoa(vcpuid))) + null */
	char name[10 + ITOA_MAX_LEN + 1];

	snprintf(name, sizeof(name), "hvm-vcpu:%d", vcpu->vcpuid);
	return anon_inode_getfd(name, &hvm_vcpu_fops, vcpu, O_RDWR | O_CLOEXEC);
}

/**
 * hvm_vm_ioctl_create_vcpu() - for HVM_CREATE_VCPU
 * @hvm: Pointer to struct hvm
 * @cpuid: equals arg
 *
 * Return: Fd of vcpu, negative errno if error occurs
 */
int hvm_vm_ioctl_create_vcpu(struct hvm *hvm, uint32_t cpuid)
{
	struct hvm_vcpu *vcpu;
	int ret;

	if (cpuid >= HVM_MAX_VCPUS)
		return -EINVAL;

	vcpu = kzalloc(sizeof(*vcpu), GFP_KERNEL);
	if (!vcpu)
		return -ENOMEM;

	/**
	 * Allocate 2 pages for data sharing between driver and halla hypervisor
	 *
	 * |- page 0           -|- page 1      -|
	 * |hvm_vcpu_run|......|not_used_yet|.......|
	 *
	 */
	vcpu->run = alloc_pages_exact(HVM_VCPU_RUN_MAP_SIZE,
				      GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!vcpu->run) {
		ret = -ENOMEM;
		goto free_vcpu;
	}
	vcpu->vcpuid = cpuid;
	vcpu->hvm = hvm;
	mutex_init(&vcpu->lock);

	ret = (int)exynos_hvc(HVC_FID_HVM_CREATE_VCPU,
			 hvm->vm_id, vcpu->vcpuid,
			 (uint64_t)virt_to_phys(vcpu->run), 0);
	if (ret < 0)
		goto free_vcpu_run;

	ret = create_vcpu_fd(vcpu);
	if (ret < 0)
		goto free_vcpu_run;
	hvm->vcpus[cpuid] = vcpu;

	hvm_vtimer_init_regs(vcpu);

	return ret;

free_vcpu_run:
	free_pages_exact(vcpu->run, HVM_VCPU_RUN_MAP_SIZE);
free_vcpu:
	kfree(vcpu);
	return ret;
}
