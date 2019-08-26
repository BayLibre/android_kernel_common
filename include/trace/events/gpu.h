/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu

#if !defined(_TRACE_GPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPU_H

#include <linux/tracepoint.h>

/*
 * The gpu_frequency event indicates frequency changes on the GPU clock domain.
 *
 * This event should be traced whenever there's a frequency change on the GPU
 * clock domain. At the start of tracing, this event should be additionally
 * emitted to record the initial GPU clock for the tracing period. Whenever GPU
 * goes into idle state, this event must be emitted for accurate tracing.
 *
 * @state: The new GPU frequency in kHz.
 * @gpu_id: GPU clock domain for frequency change.
 *
 */
TRACE_EVENT(gpu_frequency,
	TP_PROTO(
		uint32_t state,
		uint32_t gpu_id
	),
	TP_ARGS(
		state,
		gpu_id
	),
	TP_STRUCT__entry(
		__field(uint32_t, state)
		__field(uint32_t, gpu_id)
	),
	TP_fast_assign(
		__entry->state = state;
		__entry->gpu_id = gpu_id;
	),
	TP_printk(
		"state=%u "
		"gpu_id=%u",
		__entry->state,
		__entry->gpu_id
	)
);

#endif /* _TRACE_GPU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
