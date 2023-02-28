// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <trace/hooks/gunyah.h>

#include <uapi/linux/gunyah_deprecated.h>

long ghd_ioctl(unsigned int cmd, unsigned long arg)
{
	long ret = -ENOTTY;

	switch (cmd) {
	case GH_CREATE_VM:
	case GH_CREATE_VCPU:
	case GH_VM_SET_FW_NAME:
	case GH_VM_GET_FW_NAME:
	case GH_VM_GET_VCPU_COUNT:
	case GH_GET_SHARED_MEMORY_SIZE:
	case GH_IOEVENTFD:
	case GH_IRQFD:
	case GH_WAIT_FOR_EVENT:
	case GH_SET_DEVICE_FEATURES:
	case GH_SET_QUEUE_NUM_MAX:
	case GH_SET_DEVICE_CONFIG_DATA:
	case GH_GET_DRIVER_CONFIG_DATA:
	case GH_GET_QUEUE_INFO:
	case GH_GET_DRIVER_FEATURES:
	case GH_ACK_DRIVER_OK:
	case GH_ACK_RESET:
	case GH_VCPU_RUN:
		trace_android_rvh_gunyah_loader_dev_ioctl(cmd, arg, &ret);
		break;
	default:
		break;
	}

	return ret;
}