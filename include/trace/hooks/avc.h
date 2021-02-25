/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM avc

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_AVC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_AVC_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct avc_node;
DECLARE_HOOK(android_vh_selinux_avc_insert, TP_PROTO(const struct avc_node *node), TP_ARGS(node));
DECLARE_HOOK(android_vh_selinux_avc_node_delete, TP_PROTO(const struct avc_node *node), TP_ARGS(node));
DECLARE_HOOK(android_vh_selinux_avc_node_replace, TP_PROTO(const struct avc_node *old, const struct avc_node *new), TP_ARGS(old, new));
DECLARE_HOOK(android_vh_selinux_avc_lookup, TP_PROTO(const struct avc_node *node, u32 ssid, u32 tsid, u16 tclass), TP_ARGS(node, ssid, tsid, tclass));
#else
#define trace_android_vh_selinux_avc_insert(node)
#define trace_android_vh_selinux_avc_node_delete(node)
#define trace_android_vh_selinux_avc_node_replace(old, new)
#define trace_android_vh_selinux_avc_lookup(node, ssid, tsid, tclass);
#endif

#endif /* _TRACE_HOOK_AVC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
