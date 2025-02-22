/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LOCAL_LOCK_H
#define _LINUX_LOCAL_LOCK_H

#include <linux/local_lock_internal.h>

/**
 * local_lock_init - Runtime initialize a lock instance
 */
#define local_lock_init(lock)		__local_lock_init(lock)

/**
 * local_lock - Acquire a per CPU local lock
 * @lock:	The lock variable
 */
#define local_lock(lock)		__local_lock(lock)

/**
 * local_lock_irq - Acquire a per CPU local lock and disable interrupts
 * @lock:	The lock variable
 */
#define local_lock_irq(lock)		__local_lock_irq(lock)

/**
 * local_lock_irqsave - Acquire a per CPU local lock, save and disable
 *			 interrupts
 * @lock:	The lock variable
 * @flags:	Storage for interrupt flags
 */
#define local_lock_irqsave(lock, flags)				\
	__local_lock_irqsave(lock, flags)

/**
 * local_unlock - Release a per CPU local lock
 * @lock:	The lock variable
 */
#define local_unlock(lock)		__local_unlock(lock)

/**
 * local_unlock_irq - Release a per CPU local lock and enable interrupts
 * @lock:	The lock variable
 */
#define local_unlock_irq(lock)		__local_unlock_irq(lock)

/**
 * local_unlock_irqrestore - Release a per CPU local lock and restore
 *			      interrupt flags
 * @lock:	The lock variable
 * @flags:      Interrupt flags to restore
 */
#define local_unlock_irqrestore(lock, flags)			\
	__local_unlock_irqrestore(lock, flags)

/**
 * localtry_lock_init - Runtime initialize a lock instance
 */
#define localtry_lock_init(lock)		__localtry_lock_init(lock)

/**
 * localtry_lock - Acquire a per CPU local lock
 * @lock:	The lock variable
 */
#define localtry_lock(lock)		__localtry_lock(lock)

/**
 * localtry_lock_irq - Acquire a per CPU local lock and disable interrupts
 * @lock:	The lock variable
 */
#define localtry_lock_irq(lock)		__localtry_lock_irq(lock)

/**
 * localtry_lock_irqsave - Acquire a per CPU local lock, save and disable
 *			 interrupts
 * @lock:	The lock variable
 * @flags:	Storage for interrupt flags
 */
#define localtry_lock_irqsave(lock, flags)				\
	__localtry_lock_irqsave(lock, flags)

/**
 * localtry_trylock - Try to acquire a per CPU local lock.
 * @lock:	The lock variable
 *
 * The function can be used in any context such as NMI or HARDIRQ. Due to
 * locking constrains it will _always_ fail to acquire the lock in NMI or
 * HARDIRQ context on PREEMPT_RT.
 */
#define localtry_trylock(lock)		__localtry_trylock(lock)

/**
 * localtry_trylock_irqsave - Try to acquire a per CPU local lock, save and disable
 *			      interrupts if acquired
 * @lock:	The lock variable
 * @flags:	Storage for interrupt flags
 *
 * The function can be used in any context such as NMI or HARDIRQ. Due to
 * locking constrains it will _always_ fail to acquire the lock in NMI or
 * HARDIRQ context on PREEMPT_RT.
 */
#define localtry_trylock_irqsave(lock, flags)				\
	__localtry_trylock_irqsave(lock, flags)

/**
 * local_unlock - Release a per CPU local lock
 * @lock:	The lock variable
 */
#define localtry_unlock(lock)		__localtry_unlock(lock)

/**
 * local_unlock_irq - Release a per CPU local lock and enable interrupts
 * @lock:	The lock variable
 */
#define localtry_unlock_irq(lock)		__localtry_unlock_irq(lock)

/**
 * localtry_unlock_irqrestore - Release a per CPU local lock and restore
 *			      interrupt flags
 * @lock:	The lock variable
 * @flags:      Interrupt flags to restore
 */
#define localtry_unlock_irqrestore(lock, flags)			\
	__localtry_unlock_irqrestore(lock, flags)

#endif
