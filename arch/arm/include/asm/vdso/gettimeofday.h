/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 ARM Limited
 */
#ifndef __ASM_VDSO_GETTIMEOFDAY_H
#define __ASM_VDSO_GETTIMEOFDAY_H

#ifndef __ASSEMBLY__

#include <asm/arch_timer.h>
#include <asm/unistd.h>
#include <uapi/linux/time.h>

#define __arch_get_realtime_res(x)	(MONOTONIC_RES_NSEC)
#define __arch_get_coarse_res(x)	(LOW_RES_NSEC)

extern struct vdso_data *__get_datapage(void);

static __always_inline notrace int gettimeofday_fallback(
					struct __vdso_timeval *_tv,
					struct timezone *_tz)
{
	register struct timezone *tz asm("r1") = _tz;
	register struct __vdso_timeval *tv asm("r0") = _tv;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_gettimeofday;

	asm volatile(
	"	swi #0\n"
	: "=r" (ret)
	: "r" (tv), "r" (tz), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline notrace long clock_gettime_fallback(
						clockid_t _clkid,
						struct __vdso_timespec *_ts)
{
	register struct __vdso_timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_clock_gettime;

	asm volatile(
	"	swi #0\n"
	: "=r" (ret)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline notrace int clock_getres_fallback(
						clockid_t _clkid,
						struct __vdso_timespec *_ts)
{
	register struct __vdso_timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_clock_getres;

	asm volatile(
	"       swi #0\n"
	: "=r" (ret)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline notrace u64 __arch_get_hw_counter(int clock_mode)
{
	return arch_counter_get_cntvct();
}

static __always_inline notrace const struct vdso_data *__arch_get_vdso_data(void)
{
	return __get_datapage();
}

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETTIMEOFDAY_H */
