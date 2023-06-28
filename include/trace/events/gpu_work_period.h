/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GPU memory trace points
 *
 * Copyright (C) 2023 Google, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM power

#if !defined(_TRACE_GPU_WORK_PERIOD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPU_WORK_PERIOD_H

#include <linux/tracepoint.h>

/*
 * The gpu_memory_total event indicates that there's an update to either the
 * global or process total gpu memory counters.
 *
 * This event should be emitted whenever the kernel device driver allocates,
 * frees, imports, unimports memory in the GPU addressable space.
 *
 * @gpu_id: This is the gpu id.
 *
 * @pid: Put 0 for global total, while positive pid for process total.
 *
 * @size: Size of the allocation in bytes.
 *
 */
TRACE_EVENT(gpu_work_period,

	TP_PROTO(uint32_t gpu_id, uint32_t uid, uint64_t start_time_ns, uint64_t end_time_ns, uint64_t total_active_duration_ns),

	TP_ARGS(gpu_id, uid, start_time_ns, end_time_ns, total_active_duration_ns),

	TP_STRUCT__entry(
		__field(uint32_t, gpu_id)
		__field(uint32_t, uid)
		__field(uint64_t, start_time_ns)
        __field(uint64_t, end_time_ns)
        __field(uint64_t, total_active_duration_ns)
	),

	TP_fast_assign(
		__entry->gpu_id = gpu_id;
		__entry->uid = uid;
		__entry->start_time_ns = start_time_ns;
        __entry->end_time_ns = end_time_ns;
        __entry->total_active_duration_ns = total_active_duration_ns;
	),

	TP_printk("gpu_id=%u uid=%u start_time_ns=%llu end_time_ns=%llu total_active_duration_ns=%llu",
		__entry->gpu_id,
		__entry->uid,
		__entry->start_time_ns,
        __entry->end_time_ns,
        __entry->total_active_duration_ns,
        )
);

#endif /* _TRACE_GPU_WORK_PERIOD_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
