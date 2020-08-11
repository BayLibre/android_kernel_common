/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM futex
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_FUTEX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FUTEX_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct futex_q;
struct futex_hash_bucket;
DECLARE_HOOK(android_vh_alter_futex_plist_add,
	TP_PROTO(struct futex_q *q,
		 struct futex_hash_bucket *hb,
		 bool *already_on_hb),
	TP_ARGS(q, hb, already_on_hb));
#else
#define trace_android_vh_alter_futex_plist_add(q, hb, already_on_hb)
#endif
#endif /* _TRACE_HOOK_FUTEX_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
