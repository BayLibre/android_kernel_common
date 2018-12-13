/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 ARM Limited
 */
#ifndef __COMPAT_BARRIER_H
#define __COMPAT_BARRIER_H

#include <asm/barrier.h>

#if __LINUX_ARM_ARCH__ >= 8
#define aarch32_smp_mb()	dmb(ish)
#define aarch32_smp_rmb()	dmb(ishld)
#define aarch32_smp_wmb()	dmb(ishst)
#else
#define aarch32_smp_mb()	dmb(ish)
#define aarch32_smp_rmb()	aarch32_smp_mb()
#define aarch32_smp_wmb()	dmb(ishst)
#endif

/*
 * Warning: This code is meant to be used with
 * ENABLE_COMPAT_VDSO only.
 */
#ifndef ENABLE_COMPAT_VDSO
#error This header is meant to be used with ENABLE_COMPAT_VDSO only
#endif

#undef smp_mb
#undef smp_rmb
#undef smp_wmb

#define smp_mb()	aarch32_smp_mb()
#define smp_rmb()	aarch32_smp_rmb()
#define smp_wmb()	aarch32_smp_wmb()

#endif /* __COMPAT_BARRIER_H */
