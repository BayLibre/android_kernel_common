/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dtask
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DTASK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DTASK_H

#include <trace/hooks/vendor_hooks.h>

<<<<<<< HEAD   (1e3041af01622a8956e6130548f6dfb66e10871c Reapply "FROMGIT: memfd,selinux: call security_inode_init_se)
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

struct mutex;
struct mutex_waiter;
struct rt_mutex_base;
struct rw_semaphore;

DECLARE_HOOK(android_vh_alter_mutex_list_add,
	TP_PROTO(struct mutex *lock,
		struct mutex_waiter *waiter,
		struct list_head *list,
		bool *already_on_list),
	TP_ARGS(lock, waiter, list, already_on_list));
DECLARE_HOOK(android_vh_mutex_unlock_slowpath,
	TP_PROTO(struct mutex *lock),
	TP_ARGS(lock));

DECLARE_HOOK(android_vh_mutex_wait_start,
	TP_PROTO(struct mutex *lock),
	TP_ARGS(lock));
DECLARE_HOOK(android_vh_mutex_wait_finish,
	TP_PROTO(struct mutex *lock),
	TP_ARGS(lock));

DECLARE_HOOK(android_vh_rtmutex_wait_start,
	TP_PROTO(struct rt_mutex_base *lock),
	TP_ARGS(lock));
DECLARE_HOOK(android_vh_rtmutex_wait_finish,
	TP_PROTO(struct rt_mutex_base *lock),
	TP_ARGS(lock));

DECLARE_HOOK(android_vh_rwsem_read_wait_start,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_read_wait_finish,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_write_wait_start,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_write_wait_finish,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));

#endif /* _TRACE_HOOK_DTASK_H */

||||||| BASE   (d28522eb370c4657206477446388172870735d37 ANDROID: sched: Update android_rvh_check_preempt_wakeup hook)
=======
struct task_struct;
DECLARE_HOOK(android_vh_sched_show_task,
	TP_PROTO(struct task_struct *task),
	TP_ARGS(task));

#endif /* _TRACE_HOOK_DTASK_H */
>>>>>>> CHANGE (f0ff9d0ac58cf16ce9c24586e88cf893313dfad1 ANDROID: vendor_hooks: add waiting information for blocked t)
/* This part must be outside protection */
#include <trace/define_trace.h>
