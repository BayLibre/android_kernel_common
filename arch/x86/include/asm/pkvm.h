/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2022 Intel Corporation
 */
#ifndef _ASM_X86_PKVM_H
#define _ASM_X86_PKVM_H

#include <asm/kvm_para.h>
#include <asm/io.h>
#include <asm/coco.h>

/* PKVM Hypercalls */
#define PKVM_HC_INIT_FINALISE		1
#define PKVM_HC_INIT_SHADOW_VM		2
#define PKVM_HC_INIT_SHADOW_VCPU	3
#define PKVM_HC_TEARDOWN_SHADOW_VM	4
#define PKVM_HC_TEARDOWN_SHADOW_VCPU	5
#define PKVM_HC_MMIO_ACCESS		6
#define PKVM_HC_ACTIVATE_IOMMU		7
#define PKVM_HC_TLB_REMOTE_FLUSH_RANGE	8
#define PKVM_HC_SET_MMIO_VE		9
#define PKVM_HC_ADD_PTDEV		10

/*
 * 15bits for PASID, DO NOT change it, based on it,
 * the size of PASID DIR table can kept as one page
 */
#define PKVM_MAX_PASID_BITS	15
#define PKVM_MAX_PASID		(1 << PKVM_MAX_PASID_BITS)

#ifdef CONFIG_PKVM_GUEST

void pkvm_guest_init_coco(void);
bool pkvm_is_protected_guest(void);
int pkvm_set_mem_host_visibility(unsigned long addr, int numpages, bool enc);

#else

static inline void pkvm_guest_init_coco(void) { }
static inline bool pkvm_is_protected_guest(void) { return false; }
static inline int
pkvm_set_mem_host_visibility(unsigned long addr, int numpages, bool enc) { return 0; }

#endif

#endif
