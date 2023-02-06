/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM drm_property

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DRM_PROPERTY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DRM_PROPERTY_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_drm_property_alloc,
	TP_PROTO(size_t size, void *drm_property),
	TP_ARGS(size, drm_property));

#endif /* _TRACE_HOOK_DRM_PROPERTY_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
