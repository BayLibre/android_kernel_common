/*
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __VDSO_DATAPAGE_H
#define __VDSO_DATAPAGE_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <linux/types.h>

struct vdso_data {
	__u64 cs_cycle_last;	/* Timebase at clocksource init */
	__u64 raw_time_sec;	/* Raw time */
	__u64 raw_time_nsec;
	__u64 xtime_clock_sec;	/* Kernel time */
	__u64 xtime_clock_nsec;
	__u64 xtime_coarse_sec;	/* Coarse time */
	__u64 xtime_coarse_nsec;
	__u64 wtm_clock_sec;	/* Wall to monotonic time */
	__u64 wtm_clock_nsec;
	__u64 btm_nsec;		/* Monotonic to boot time */
	__u64 tai_sec;		/* International Atomic Time */
	__u64 cs_mono_mask;	/* NTP-adjusted clocksource mask */
	__u64 cs_raw_mask;	/* Raw clocksource mask */
	__u32 tb_seq_count;	/* Timebase sequence counter */
	__u32 cs_mono_mult;	/* NTP-adjusted clocksource multiplier */
	__u32 cs_shift;		/* Clocksource shift (mono = raw) */
	__u32 cs_raw_mult;	/* Raw clocksource multiplier */
	__u32 tz_minuteswest;	/* Whacky timezone stuff */
	__u32 tz_dsttime;
	__u32 use_syscall;
};

#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __VDSO_DATAPAGE_H */
