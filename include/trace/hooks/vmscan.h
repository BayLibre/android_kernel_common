/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmscan

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_VMSCAN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VMSCAN_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_set_balance_anon_file_reclaim,
			TP_PROTO(bool *balance_anon_file_reclaim),
			TP_ARGS(balance_anon_file_reclaim), 1);
<<<<<<< HEAD   (b68cddb455684dd92a6f86a1dc93f9395fc1adc0 ANDROID: sched: Export cpu_busy_with_softirqs)
||||||| BASE   (5fa4c0d2c0fc53593c70acb5876e2208ea0eba24 ANDROID: mm/cma: add vendor_hook in cma_alloc for retries)
DECLARE_HOOK(android_vh_check_folio_look_around_ref,
	TP_PROTO(struct folio *folio, int *skip),
	TP_ARGS(folio, skip));
DECLARE_HOOK(android_vh_tune_swappiness,
	TP_PROTO(int *swappiness),
	TP_ARGS(swappiness));
DECLARE_HOOK(android_vh_async_psi_bypass,
	TP_PROTO(bool *bypass),
	TP_ARGS(bypass));
=======
DECLARE_HOOK(android_vh_check_folio_look_around_ref,
	TP_PROTO(struct folio *folio, int *skip),
	TP_ARGS(folio, skip));
DECLARE_HOOK(android_vh_tune_swappiness,
	TP_PROTO(int *swappiness),
	TP_ARGS(swappiness));
DECLARE_HOOK(android_vh_async_psi_bypass,
	TP_PROTO(bool *bypass),
	TP_ARGS(bypass));
DECLARE_HOOK(android_vh_mglru_should_abort_scan,
	TP_PROTO(unsigned long nr_reclaimed, unsigned long nr_to_reclaim,
	unsigned int order, bool *bypass),
	TP_ARGS(nr_to_reclaim, nr_reclaimed, order, bypass));
>>>>>>> CHANGE (621fb7eb3c71abbc52c0ebe93636e47a92f9ef23 ANDROID: mm: add vendor hook to abort scan)
#endif /* _TRACE_HOOK_VMSCAN_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
