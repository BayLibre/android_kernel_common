/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ufshcd
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_UFSHCD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_UFSHCD_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(__GENKSYMS__) || !IS_ENABLED(CONFIG_SCSI_UFSHCD)
struct ufs_hba;
struct ufshcd_lrb;
struct uic_command;
struct request;
struct scsi_device;
struct scsi_cmnd;
#else
/* struct ufs_hba, struct ufshcd_lrb, struct uic_command */
#include <../drivers/scsi/ufs/ufshcd.h>
/* struct request */
#include <linux/blkdev.h>
/* struct scsi_device */
#include <scsi/scsi_device.h>
/* struct scsi_cmnd */
#include <scsi/scsi_cmnd.h>
#endif /* __GENKSYMS__ */

DECLARE_HOOK(android_vh_ufs_fill_prdt,
	TP_PROTO(struct ufs_hba *hba, struct ufshcd_lrb *lrbp,
		 unsigned int segments, int *err),
	TP_ARGS(hba, lrbp, segments, err));

DECLARE_RESTRICTED_HOOK(android_rvh_ufs_complete_init,
			TP_PROTO(struct ufs_hba *hba),
			TP_ARGS(hba), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_ufs_reprogram_all_keys,
			TP_PROTO(struct ufs_hba *hba, int *err),
			TP_ARGS(hba, err), 1);

DECLARE_HOOK(android_vh_ufs_prepare_command,
	TP_PROTO(struct ufs_hba *hba, struct request *rq,
		 struct ufshcd_lrb *lrbp, int *err),
	TP_ARGS(hba, rq, lrbp, err));

DECLARE_HOOK(android_vh_ufs_update_sysfs,
	TP_PROTO(struct ufs_hba *hba),
	TP_ARGS(hba));

DECLARE_HOOK(android_vh_ufs_send_command,
	TP_PROTO(struct ufs_hba *hba, struct ufshcd_lrb *lrbp),
	TP_ARGS(hba, lrbp));

DECLARE_HOOK(android_vh_ufs_compl_command,
	TP_PROTO(struct ufs_hba *hba, struct ufshcd_lrb *lrbp),
	TP_ARGS(hba, lrbp));

DECLARE_HOOK(android_vh_ufs_send_uic_command,
	TP_PROTO(struct ufs_hba *hba, struct uic_command *ucmd,
		 const char *str),
	TP_ARGS(hba, ucmd, str));

DECLARE_HOOK(android_vh_ufs_send_tm_command,
	TP_PROTO(struct ufs_hba *hba, int tag, const char *str),
	TP_ARGS(hba, tag, str));

DECLARE_HOOK(android_vh_ufs_check_int_errors,
	TP_PROTO(struct ufs_hba *hba, bool queue_eh_work),
	TP_ARGS(hba, queue_eh_work));

DECLARE_HOOK(android_vh_ufs_update_sdev,
	TP_PROTO(struct scsi_device *sdev),
	TP_ARGS(sdev));

DECLARE_HOOK(android_vh_ufs_clock_scaling,
	TP_PROTO(struct ufs_hba *hba, bool *force_out, bool *force_scaling, bool *scale_up),
	TP_ARGS(hba, force_out, force_scaling, scale_up));

DECLARE_HOOK(android_vh_ufs_send_command_post_change,
	TP_PROTO(struct ufs_hba *hba, struct ufshcd_lrb *lrbp),
	TP_ARGS(hba, lrbp));

DECLARE_HOOK(android_vh_ufs_perf_huristic_ctrl,
	TP_PROTO(struct ufs_hba *hba,
		 struct ufshcd_lrb *lrbp, int *err),
	TP_ARGS(hba, lrbp, err));

DECLARE_HOOK(android_vh_ufs_abort_success_ctrl,
	TP_PROTO(struct ufs_hba *hba,
		 struct ufshcd_lrb *lrbp),
	TP_ARGS(hba, lrbp));

DECLARE_HOOK(android_vh_ufs_err_handler,
	TP_PROTO(struct ufs_hba *hba,
		 bool *err_handled),
	TP_ARGS(hba, err_handled));

DECLARE_HOOK(android_vh_ufs_compl_rsp_check_done,
	TP_PROTO(struct ufs_hba *hba,
		 struct ufshcd_lrb *lrbp, bool *done),
	TP_ARGS(hba, lrbp, done));

DECLARE_HOOK(android_vh_ufs_err_print_ctrl,
	TP_PROTO(struct ufs_hba *hba,
		 bool *skip),
	TP_ARGS(hba, skip));

DECLARE_HOOK(android_vh_ufs_err_check_ctrl,
	TP_PROTO(struct ufs_hba *hba,
		 bool *err_check),
	TP_ARGS(hba, err_check));

DECLARE_HOOK(android_vh_ufshcd_any_tag_in_use,
	TP_PROTO(bool *skip, int *busy, struct ufs_hba *hba),
	TP_ARGS(skip, busy, hba));

DECLARE_HOOK(android_vh_ufshcd_release,
	TP_PROTO(bool *skip, struct ufs_hba *hba),
	TP_ARGS(skip, hba));

DECLARE_HOOK(android_vh_ufshcd_prepare_req_desc_hdr,
	TP_PROTO(struct ufshcd_lrb *lrbp, u8 *upiu_flags),
	TP_ARGS(lrbp, upiu_flags));

DECLARE_HOOK(android_vh_ufshcd_queuecommand_first,
	TP_PROTO(bool *skip, int *tag, struct ufs_hba *hba, struct scsi_cmnd *cmd),
	TP_ARGS(skip, tag, hba, cmd));

DECLARE_HOOK(android_vh_ufshcd_queuecommand_second,
	TP_PROTO(struct ufs_hba *hba, int tag),
	TP_ARGS(hba, tag));

DECLARE_HOOK(android_vh_ufshcd_queuecommand_third,
	TP_PROTO(bool *skip, struct ufs_hba *hba),
	TP_ARGS(skip, hba));

DECLARE_HOOK(android_vh_ufshcd_queuecommand_fourth,
	TP_PROTO(struct ufs_hba *hba, int tag),
	TP_ARGS(hba, tag));

DECLARE_HOOK(android_vh_ufshcd_set_queue_depth,
	TP_PROTO(struct scsi_device *sdev, u8 *lun_qdepth),
	TP_ARGS(sdev, lun_qdepth));

DECLARE_HOOK(android_vh_ufshcd_change_queue_depth,
	TP_PROTO(bool *skip, struct scsi_device *sdev, int *depth),
	TP_ARGS(skip, sdev, depth));

DECLARE_HOOK(android_vh_ufshcd_compl_command_second,
	TP_PROTO(struct ufs_hba *hba, int index),
	TP_ARGS(hba, index));

DECLARE_HOOK(android_vh_ufshcd_abort_first,
	TP_PROTO(bool *skip, unsigned int *tag, struct scsi_cmnd *cmd),
	TP_ARGS(skip, tag, cmd));

DECLARE_HOOK(android_vh_ufshcd_abort_second,
	TP_PROTO(struct ufs_hba *hba, int tag),
	TP_ARGS(hba, tag));

DECLARE_HOOK(android_vh_ufshcd_init,
	TP_PROTO(struct ufs_hba *hba, struct Scsi_Host *host),
	TP_ARGS(hba, host));

DECLARE_HOOK(android_vh_ufshcd_memory_alloc,
	TP_PROTO(size_t *ucdl_size, struct ufs_hba *hba),
	TP_ARGS(ucdl_size, hba));

DECLARE_HOOK(android_vh_ufshcd_host_memory_configure,
	TP_PROTO(int *cmd_desc_size, struct ufs_hba *hba),
	TP_ARGS(cmd_desc_size, hba));

DECLARE_HOOK(android_vh_ufshcd_init_lrb,
	TP_PROTO(struct ufs_hba *hba, struct utp_transfer_cmd_desc **cmd_descp,
		 dma_addr_t *cmd_desc_element_addr, int i),
	TP_ARGS(hba, cmd_descp, cmd_desc_element_addr, i));
#endif /* _TRACE_HOOK_UFSHCD_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
