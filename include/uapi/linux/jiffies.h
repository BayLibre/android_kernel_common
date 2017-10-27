#ifndef _UAPI_JIFFIES_H
#define _UAPI_JIFFIES_H

/*
 * Suppose we want to divide two numbers NOM and DEN: NOM/DEN, then we can
 * improve accuracy by shifting LSH bits, hence calculating:
 *     (NOM << LSH) / DEN
 * This however means trouble for large NOM, because (NOM << LSH) may no
 * longer fit in 32 bits. The following way of calculating this gives us
 * some slack, under the following conditions:
 *   - (NOM / DEN) fits in (32 - LSH) bits.
 *   - (NOM % DEN) fits in (32 - LSH) bits.
 */
#define SH_DIV(NOM, DEN, LSH) ((((NOM) / (DEN)) << (LSH))		\
			     + ((((NOM) % (DEN)) << (LSH)) + (DEN) / 2) / (DEN))

/* LATCH is used in the interval timer and ftape setup. */
#define LATCH ((CLOCK_TICK_RATE + HZ/2) / HZ)	/* For divider */

/* TICK_NSEC is the time between ticks in nsec assuming SHIFTED_HZ */
#define TICK_NSEC ((NSEC_PER_SEC+HZ/2)/HZ)

/* TICK_USEC is the time between ticks in usec assuming fake USER_HZ */
#define TICK_USEC ((1000000UL + USER_HZ/2) / USER_HZ)

/*
 *	These inlines deal with timer wrapping correctly. You are
 *	strongly encouraged to use them
 *	1. Because people otherwise forget
 *	2. Because if the timer wrap changes in future you won't have to
 *	   alter your driver code.
 *
 * time_after(a,b) returns true if the time a is after time b.
 *
 * Do this with "<0" and ">=0" to only test the sign of the result. A
 * good compiler would generate better code (and a really good compiler
 * wouldn't care). Gcc is currently neither.
 */
#define time_after(a, b)		\
	(typecheck(unsigned long, a) &&	\
	 typecheck(unsigned long, b) &&	\
	 ((long)((b) - (a)) < 0))
#define time_before(a, b)	time_after(b, a)

#define time_after_eq(a, b)		\
	(typecheck(unsigned long, a) &&	\
	 typecheck(unsigned long, b) &&	\
	 ((long)((a) - (b)) >= 0))
#define time_before_eq(a, b)	time_after_eq(b, a)

/*
 * Calculate whether a is in the range of [b, c].
 */
#define time_in_range(a, b, c)	\
	(time_after_eq(a, b) &&	\
	 time_before_eq(a, c))

/*
 * Calculate whether a is in the range of [b, c).
 */
#define time_in_range_open(a, b, c)	\
	(time_after_eq(a, b) &&		\
	 time_before(a, c))

/*
 * Same as above, but does so with platform independent 64bit types.
 * These must be used when utilizing jiffies_64 (i.e. return value of
 * get_jiffies_64()
 */
#define time_after64(a, b)	\
	(typecheck(__u64, a) &&	\
	 typecheck(__u64, b) &&	\
	 ((__s64)((b) - (a)) < 0))
#define time_before64(a, b)	time_after64(b, a)

#define time_after_eq64(a, b)	\
	(typecheck(__u64, a) &&	\
	 typecheck(__u64, b) &&	\
	 ((__s64)((a) - (b)) >= 0))
#define time_before_eq64(a, b)	time_after_eq64(b, a)

#define time_in_range64(a, b, c)	\
	(time_after_eq64(a, b) &&	\
	 time_before_eq64(a, c))

#endif
