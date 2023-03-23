#include <asm/kvm_pkvm_module.h>
#include <nvhe/trap_handler.h>

#include "events.h"
#define HYP_EVENT_FILE ../../../../drivers/misc/pkvm-selftest/hyp/events.h
#include <nvhe/define_events.h>

extern char __hyp_event_ids_start[];
extern char __hyp_event_ids_end[];

const struct pkvm_module_ops *pkvm_ops;

void *tracing_reserve_entry(unsigned long length)
{
	return pkvm_ops->tracing_reserve_entry(length);
}

void tracing_commit_entry(void)
{
	pkvm_ops->tracing_commit_entry();
}

void pkvm_selftest_event(struct kvm_cpu_context *ctx)
{
	trace_selftest();

	cpu_reg(ctx, 1) = 0;
}

int pkvm_selftest_init(const struct pkvm_module_ops *ops)
{
	ops->register_hyp_event_ids((unsigned long)__hyp_event_ids_start,
				    (unsigned long)__hyp_event_ids_end);

	pkvm_ops = ops;

	return 0;
}
