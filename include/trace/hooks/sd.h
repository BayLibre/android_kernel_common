/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sd

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SD_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(__GENKSYMS__) || !defined(CONFIG_BLOCK)
struct gendisk;
struct scsi_device
#else
/*struct gendisk*/
#include <linux/genhd.h>
/*struct scsi_device*/
#include <scsi/scsi_device.h>
#endif /* __GENKSYMS__ */

DECLARE_HOOK(android_vh_sd_probe,
	TP_PROTO(struct gendisk *gd, struct scsi_device *sdp),
	TP_ARGS(gd, sdp));

#endif /* _TRACE_HOOK_SD_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
