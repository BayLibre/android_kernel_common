// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/arm-smccc.h>
#include <linux/err.h>
#include <linux/uaccess.h>

#include <linux/gzvm.h>
#include <linux/gzvm_drv.h>
#include "gzvm_arch.h"

/**
 * geniezone_hypercall_wrapper()
 *
 * Return: The wrapper helps caller to convert geniezone errno to Linux errno.
 */
int gzvm_hypcall_wrapper(unsigned long a0, unsigned long a1, unsigned long a2,
			 unsigned long a3, unsigned long a4, unsigned long a5,
			 unsigned long a6, unsigned long a7,
			 struct arm_smccc_res *res)
{
	arm_smccc_hvc(a0, a1, a2, a3, a4, a5, a6, a7, res);
	return gz_err_to_errno(res->a0);
}

int gzvm_arch_probe(void)
{
	struct arm_smccc_res res;

	arm_smccc_hvc(MT_HVC_GZVM_PROBE, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 == 0)
		return 0;

	return -ENXIO;
}

int gzvm_arch_set_memregion(gzvm_id_t vm_id, size_t buf_size,
			    phys_addr_t region)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_SET_MEMREGION, vm_id,
				    buf_size, region, 0, 0, 0, 0, &res);
}

static int gzvm_cap_arm_vm_ipa_size(void __user *argp)
{
	__u64 value = CONFIG_ARM64_PA_BITS;

	if (copy_to_user(argp, &value, sizeof(__u64)))
		return -EFAULT;

	return 0;
}

int gzvm_arch_check_extension(struct gzvm *gzvm, __u64 cap, void __user *argp)
{
	int ret = -EOPNOTSUPP;

	switch (cap) {
	case GZVM_CAP_ARM_PROTECTED_VM: {
		__u64 success = 1;

		if (copy_to_user(argp, &success, sizeof(__u64)))
			return -EFAULT;
		ret = 0;
		break;
	}
	case GZVM_CAP_ARM_VM_IPA_SIZE: {
		ret = gzvm_cap_arm_vm_ipa_size(argp);
		break;
	}
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/**
 * gzvm_arch_create_vm()
 *
 * Return:
 * * positive value	- VM ID
 * * -ENOMEM		- Memory not enough for storing VM data
 */
int gzvm_arch_create_vm(void)
{
	struct arm_smccc_res res;
	int ret;

	ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_VM, 0, 0, 0, 0, 0, 0, 0,
				   &res);

	if (ret == 0)
		return res.a1;
	else
		return ret;
}

int gzvm_arch_destroy_vm(gzvm_id_t vm_id)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_DESTROY_VM, vm_id, 0, 0, 0, 0,
				    0, 0, &res);
}

int gzvm_vm_arch_enable_cap(struct gzvm *gzvm, struct gzvm_enable_cap *cap,
			    struct arm_smccc_res *res)
{
	return gzvm_hypcall_wrapper(MT_HVC_GZVM_ENABLE_CAP, gzvm->vm_id,
				   cap->cap, cap->args[0], cap->args[1],
				   cap->args[2], cap->args[3], cap->args[4],
				   res);
}

/**
 * gzvm_vm_ioctl_get_pvmfw_size() - Get pvmfw size from hypervisor, return
 *				    in x1, and return to userspace in args.
 *
 * Return:
 * * 0			- Succeed
 * * -EINVAL		- Hypervisor return invalid results
 * * -EFAULT		- Fail to copy back to userspace buffer
 */
static int gzvm_vm_ioctl_get_pvmfw_size(struct gzvm *gzvm,
					struct gzvm_enable_cap *cap,
					void __user *argp)
{
	struct arm_smccc_res res = {0};

	if (gzvm_vm_arch_enable_cap(gzvm, cap, &res) != 0)
		return -EINVAL;

	cap->args[1] = res.a1;
	if (copy_to_user(argp, cap, sizeof(*cap)))
		return -EFAULT;

	return 0;
}

/**
 * gzvm_vm_ioctl_cap_pvm() - Proceed GZVM_CAP_ARM_PROTECTED_VM's subcommands
 *
 * Return:
 * * 0			- Succeed
 * * -EINVAL		- Invalid subcommand or arguments
 */
static int gzvm_vm_ioctl_cap_pvm(struct gzvm *gzvm, struct gzvm_enable_cap *cap,
				 void __user *argp)
{
	int ret = -EINVAL;
	struct arm_smccc_res res = {0};

	switch (cap->args[0]) {
	case GZVM_CAP_ARM_PVM_SET_PVMFW_IPA:
		ret = gzvm_vm_arch_enable_cap(gzvm, cap, &res);
		break;
	case GZVM_CAP_ARM_PVM_GET_PVMFW_SIZE:
		ret = gzvm_vm_ioctl_get_pvmfw_size(gzvm, cap, argp);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int gzvm_vm_ioctl_arch_enable_cap(struct gzvm *gzvm, struct gzvm_enable_cap *cap,
				  void __user *argp)
{
	int ret = -EINVAL;

	switch (cap->cap) {
	case GZVM_CAP_ARM_PROTECTED_VM:
		ret = gzvm_vm_ioctl_cap_pvm(gzvm, cap, argp);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int gzvm_arch_vcpu_update_one_reg(struct gzvm_vcpu *vcpu, __u64 reg_id,
				  bool is_write, __u64 *data)
{
	struct arm_smccc_res res;
	unsigned long a1;
	int ret;

	/* reg id follows KVM's encoding */
	switch (reg_id & GZVM_REG_ARM_COPROC_MASK) {
	case GZVM_REG_ARM_CORE:
		break;
	default:
		return -EOPNOTSUPP;
	}

	a1 = assemble_vm_vcpu_tuple(vcpu->gzvm->vm_id, vcpu->vcpuid);
	if (!is_write) {
		ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_GET_ONE_REG,
					   a1, reg_id, 0, 0, 0, 0, 0, &res);
		if (ret == 0)
			*data = res.a1;
	} else {
		ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_SET_ONE_REG,
					   a1, reg_id, *data, 0, 0, 0, 0, &res);
	}

	return ret;
}

int gzvm_arch_vcpu_run(struct gzvm_vcpu *vcpu, __u64 *exit_reason)
{
	struct arm_smccc_res res;
	unsigned long a1;
	int ret;

	a1 = assemble_vm_vcpu_tuple(vcpu->gzvm->vm_id, vcpu->vcpuid);
	ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_RUN, a1, 0, 0, 0, 0, 0,
				   0, &res);
	*exit_reason = res.a1;
	return ret;
}

int gzvm_arch_destroy_vcpu(gzvm_id_t vm_id, int vcpuid)
{
	struct arm_smccc_res res;
	unsigned long a1;

	a1 = assemble_vm_vcpu_tuple(vm_id, vcpuid);
	gzvm_hypcall_wrapper(MT_HVC_GZVM_DESTROY_VCPU, a1, 0, 0, 0, 0, 0, 0,
			     &res);

	return 0;
}

/**
 * gzvm_arch_create_vcpu() - Call smc to gz hypervisor to create vcpu
 * @run: Virtual address of vcpu->run
 */
int gzvm_arch_create_vcpu(gzvm_id_t vm_id, int vcpuid, void *run)
{
	struct arm_smccc_res res;
	unsigned long a1, a2;
	int ret;

	a1 = assemble_vm_vcpu_tuple(vm_id, vcpuid);
	a2 = (__u64)virt_to_phys(run);
	ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_VCPU, a1, a2, 0, 0, 0, 0,
				   0, &res);

	return ret;
}

int gzvm_arch_create_device(gzvm_id_t vm_id, struct gzvm_create_device *gzvm_dev)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_DEVICE, vm_id,
				    virt_to_phys(gzvm_dev), 0, 0, 0, 0, 0, &res);
}

int gzvm_arch_inject_irq(struct gzvm *gzvm, unsigned int vcpu_idx, u32 irq_type,
			 u32 irq, bool level)
{
	/* default use spi */
	return gzvm_vgic_inject_spi(gzvm, vcpu_idx, irq, level);
}

void gzvm_sync_hwstate(struct gzvm_vcpu *vcpu)
{
	gzvm_sync_vgic_state(vcpu);
}
