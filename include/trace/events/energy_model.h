/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM energy_model

#if !defined(_TRACE_ENERGY_MODEL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ENERGY_MODEL_H

#include <linux/tracepoint.h>

TRACE_EVENT(em_perf_state,
	TP_PROTO(const char *dev_name, int nr_perf_states, int state,
		 unsigned long ps_frequency, unsigned long ps_power,
		 unsigned long ps_cost, unsigned long ps_flags),

	TP_ARGS(dev_name, nr_perf_states, state, ps_frequency, ps_power, ps_cost,
		ps_flags),

	TP_STRUCT__entry(
		__string(name, dev_name)
		__field(int, num_states)
		__field(int, state)
		__field(unsigned long, frequency)
		__field(unsigned long, power)
		__field(unsigned long, cost)
		__field(unsigned long, flags)
	),

	TP_fast_assign(
		__assign_str(name, dev_name);
		__entry->num_states = nr_perf_states;
		__entry->state = state;
		__entry->frequency = ps_frequency;
		__entry->power = ps_power;
		__entry->cost = ps_cost;
		__entry->flags = ps_flags;
	),

	TP_printk("dev_name=%s nr_perf_states=%d state=%d frequency=%lu power=%lu cost=%lu flags=%lu",
		__get_str(name), __entry->num_states, __entry->state,
		__entry->frequency, __entry->power, __entry->cost,
		__entry->flags)
);
#endif /* _TRACE_ENERGY_MODEL_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
