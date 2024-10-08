/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dev_shutdown
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_DEV_SHUTDOWN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DEV_SHUTDOWN_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

struct device;
DECLARE_HOOK(android_vh_device_shutdown,
	TP_PROTO(struct device *dev),
	TP_ARGS(dev));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_DEV_SHUTDOWN_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
