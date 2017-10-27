#ifndef _UAPI_HRTIMER_H
#define _UAPI_HRTIMER_H

/*
 * The resolution of the clocks. The resolution value is returned in
 * the clock_getres() system call to give application programmers an
 * idea of the (in)accuracy of timers. Timer values are rounded up to
 * this resolution values.
 */
#ifndef LOW_RES_NSEC
#define LOW_RES_NSEC		TICK_NSEC
#endif

#ifdef CONFIG_HIGH_RES_TIMERS

# define HIGH_RES_NSEC		1
# define MONOTONIC_RES_NSEC	HIGH_RES_NSEC

#else

# define MONOTONIC_RES_NSEC	LOW_RES_NSEC

#endif

#endif
