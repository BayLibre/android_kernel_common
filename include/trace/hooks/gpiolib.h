/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpiolib

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_GPIOLIB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_GPIOLIB_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_gpio_read,
	TP_PROTO(int *valid_gpio),
	TP_ARGS(valid_gpio));
#else
#define trace_android_vh_gpio_read(valid_gpio)
#endif

#endif /* _TRACE_HOOK_GPIOLIB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
