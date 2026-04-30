/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * k1x_isp_drv_uapi.h - Spacemit K1X ISP Driver UAPI definitions
 *
 * Copyright (C) 2023 SPACEMIT Micro Limited.
 */

#ifndef __K1X_ISP_DRV_UAPI_H__
#define __K1X_ISP_DRV_UAPI_H__

#include <linux/types.h>
#include <linux/ioctl.h>

/* Maximum constants */
#define K1X_ISP_MAX_PLANE_NUM		4
#define K1X_ISP_MAX_BUFFER_NUM		4
#define K1XISP_SLICE_MAX_NUM		16
#define K1XISP_SLICE_REG_MAX_NUM	2048
#define ISP_STAT_DWT_OFFSET_NUM		4
#define K1X_ISP_PDC_CHANNEL_NUM		2
#define K1X_ISP_PLANE_RESERVED_NUM	4

/* Device names */
#define K1X_ISP_DEV_NAME		"k1x-isp"
#define K1X_ISP_PIPE_DEV_NAME		"k1x-isp-pipe"

/**
 * enum isp_stat_id - ISP statistics ID
 */
enum isp_stat_id {
	ISP_STAT_ID_PDC = 0,
	ISP_STAT_ID_EIS,
	ISP_STAT_ID_AE,
	ISP_STAT_ID_AWB,
	ISP_STAT_ID_AF,
	ISP_STAT_ID_LTM,
	ISP_STAT_ID_MAX,
};

/**
 * enum isp_pipe_work_type - ISP pipeline work type
 */
enum isp_pipe_work_type {
	ISP_PIPE_WORK_TYPE_INIT = 0,
	ISP_PIPE_WORK_TYPE_PREVIEW,
	ISP_PIPE_WORK_TYPE_CAPTURE,
};

/**
 * enum isp_pipe_task_type - ISP pipeline task type
 */
enum isp_pipe_task_type {
	ISP_PIPE_TASK_TYPE_SOF = 0,
	ISP_PIPE_TASK_TYPE_EOF,
	ISP_PIPE_TASK_TYPE_AF,
	ISP_PIPE_TASK_TYPE_MAX,
};

/**
 * enum isp_hw_pipeline_id - Hardware pipeline ID
 */
enum isp_hw_pipeline_id {
	ISP_HW_PIPELINE_ID_0 = 0,
	ISP_HW_PIPELINE_ID_1,
	ISP_HW_PIPELINE_ID_MAX,
};

/**
 * enum isp_work_status - ISP work status
 */
enum isp_work_status {
	ISP_WORK_STATUS_STOP = 0,
	ISP_WORK_STATUS_START,
};

/**
 * enum isp_job_action - ISP job action types
 */
enum isp_job_action {
	ISP_JOB_ACTION_START = 0,
	ISP_JOB_ACTION_STOP,
	ISP_JOB_ACTION_RESTART,
	ISP_JOB_ACTION_SWITCH,
};

/**
 * struct isp_buffer_plane - Single buffer plane descriptor
 * @m: Memory identifier (fd or physical address)
 * @length: Length of the buffer plane
 * @offset: Offset within the buffer
 * @pitch: Buffer pitch (stride)
 * @reserved: Reserved for additional data (e.g., PDC count/width)
 */
struct isp_buffer_plane {
	union {
		__s32 fd;
		__u64 phy_addr;
	} m;
	__u32 length;
	__u32 offset;
	__u32 pitch;
	__u32 reserved[K1X_ISP_PLANE_RESERVED_NUM];
};

/**
 * struct isp_reg_unit - Register unit for burst read/write
 * @reg_addr: Register address
 * @reg_value: Register value
 * @reg_mask: Register mask for write operations
 */
struct isp_reg_unit {
	__u32 reg_addr;
	__u32 reg_value;
	__u32 reg_mask;
};

/**
 * struct isp_regs_info - Register information for burst operations
 * @mem: Memory identifier (fd or physical address)
 * @size: Number of register units
 * @data: Pointer to register data array
 * @mem_index: Memory index
 */
struct isp_regs_info {
	union {
		__u64 phy_addr;
		__s32 fd;
	} mem;
	__u32 size;
	struct isp_reg_unit __user *data;
	__u32 mem_index;
};

/**
 * struct isp_ubuf_uint - User buffer unit for statistics
 * @buf_index: Buffer index in the queue
 * @plane_count: Number of planes
 * @buf_planes: Array of buffer planes
 */
struct isp_ubuf_uint {
	__s32 buf_index;
	__u32 plane_count;
	struct isp_buffer_plane buf_planes[K1X_ISP_MAX_PLANE_NUM];
};

/**
 * struct isp_sof_task - SOF (Start of Frame) task result
 * @awb_result: Auto white balance result
 * @eis_result: Electronic image stabilization result
 * @ltm_result: Local tone mapping result
 * @ae_result: Auto exposure result
 */
struct isp_sof_task {
	struct isp_ubuf_uint awb_result;
	struct isp_ubuf_uint eis_result;
	struct isp_ubuf_uint ltm_result;
	struct isp_ubuf_uint ae_result;
};

/**
 * struct isp_eof_task - EOF (End of Frame) task result
 * @ae_result: Auto exposure result
 */
struct isp_eof_task {
	struct isp_ubuf_uint ae_result;
};

/**
 * struct isp_af_task - AF (Auto Focus) task result
 * @af_result: Auto focus result
 * @pdc_result: Phase detection result
 */
struct isp_af_task {
	struct isp_ubuf_uint af_result;
	struct isp_ubuf_uint pdc_result;
};

/**
 * struct isp_stats_result - Combined statistics result
 * @sof_task: SOF task results
 * @eof_task: EOF task results
 * @af_task: AF task results
 */
struct isp_stats_result {
	struct isp_sof_task sof_task;
	struct isp_eof_task eof_task;
	struct isp_af_task af_task;
};

/**
 * struct isp_user_task_info - User task information
 * @task_type: Task type (isp_pipe_task_type)
 * @timeout: Timeout in milliseconds
 * @frame_num: Frame number (input)
 * @result_valid: True if result is valid (output)
 * @frame_number: Frame number of result (output)
 * @work_status: Work status (output)
 * @stats_result: Statistics results
 */
struct isp_user_task_info {
	__u32 task_type;
	__u32 timeout;
	__u32 frame_num;
	__u32 result_valid;
	__u32 frame_number;
	__u32 work_status;
	struct isp_stats_result stats_result;
};

/**
 * struct isp_buffer_request_info - Buffer request information
 * @stat_buf_count: Buffer count for each stat type
 */
struct isp_buffer_request_info {
	__u32 stat_buf_count[ISP_STAT_ID_MAX];
};

/**
 * struct isp_buffer_enqueue_info - Buffer enqueue information
 * @ubuf_uint: User buffer units for each stat type
 */
struct isp_buffer_enqueue_info {
	struct isp_ubuf_uint ubuf_uint[ISP_STAT_ID_MAX];
};

/**
 * struct isp_job_describer - Job descriptor
 * @work_type: Work type (isp_pipe_work_type)
 * @action: Job action (isp_job_action)
 */
struct isp_job_describer {
	__u32 work_type;
	__u32 action;
};

/**
 * struct isp_drv_deployment - Driver deployment information
 * @work_type: Work type (isp_pipe_work_type)
 * @fd_buffer: Use fd for buffer if true
 * @reg_mem: Register memory descriptor
 * @reg_mem_size: Register memory size
 * @reg_mem_index: Output: assigned memory index
 */
struct isp_drv_deployment {
	__u32 work_type;
	__u32 fd_buffer;
	struct isp_buffer_plane reg_mem;
	__u32 reg_mem_size;
	__u32 reg_mem_index;
};

/**
 * struct isp_slice_regs - Slice register descriptor
 * @reg_count: Number of registers
 * @data: Pointer to register data
 */
struct isp_slice_regs {
	__u32 reg_count;
	struct isp_reg_unit __user *data;
};

/**
 * struct isp_capture_slice_pack - Capture slice package
 * @slice_width: Width of the slice
 * @raw_read_offset: RAW read offset
 * @yuv_out_offset: YUV output offset
 * @dwt_offset: DWT offsets
 * @slice_reg: Slice registers
 */
struct isp_capture_slice_pack {
	__s32 slice_width;
	__s32 raw_read_offset;
	__s32 yuv_out_offset;
	__s32 dwt_offset[ISP_STAT_DWT_OFFSET_NUM];
	struct isp_slice_regs slice_reg;
};

/**
 * struct isp_capture_package - Capture package
 * @slice_count: Number of slices
 * @capture_slice_packs: Array of slice packages
 */
struct isp_capture_package {
	__u32 slice_count;
	struct isp_capture_slice_pack capture_slice_packs[K1XISP_SLICE_MAX_NUM];
};

/**
 * struct isp_pdc_ctrl_info - PDC control information
 * @enable: Enable PDC
 */
struct isp_pdc_ctrl_info {
	__u32 enable;
};

/**
 * struct isp_endframe_work_info - End frame work information
 * @process_ae_by_sof: Process AE by SOF
 * @get_frameinfo_by_eof: Get frame info by EOF
 */
struct isp_endframe_work_info {
	__u32 process_ae_by_sof;
	__u32 get_frameinfo_by_eof;
};

/* ISP ioctl magic number */
#define IOC_K1X_ISP_TYPE		'I'

/* ISP ioctl command numbers */
#define ISP_IOC_NR_DEPLOY_DRV		0x01
#define ISP_IOC_NR_UNDEPLOY_DRV		0x02
#define ISP_IOC_NR_SET_REG		0x03
#define ISP_IOC_NR_GET_REG		0x04
#define ISP_IOC_NR_SET_PDC		0x05
#define ISP_IOC_NR_SET_JOB		0x06
#define ISP_IOC_NR_GET_INTERRUPT	0x07
#define ISP_IOC_NR_REQUEST_BUFFER	0x08
#define ISP_IOC_NR_ENQUEUE_BUFFER	0x09
#define ISP_IOC_NR_FLUSH_BUFFER		0x0A
#define ISP_IOC_NR_TRIGGER_CAPTURE	0x0B
#define ISP_IOC_NR_SET_SINGLE_REG	0x0C
#define ISP_IOC_NR_SET_END_FRAME_WORK	0x0D

/* ISP ioctl commands */
#define ISP_IOC_DEPLOY_DRV \
	_IOWR(IOC_K1X_ISP_TYPE, ISP_IOC_NR_DEPLOY_DRV, struct isp_drv_deployment)
#define ISP_IOC_UNDEPLOY_DRV \
	_IOW(IOC_K1X_ISP_TYPE, ISP_IOC_NR_UNDEPLOY_DRV, __u32)
#define ISP_IOC_SET_REG \
	_IOW(IOC_K1X_ISP_TYPE, ISP_IOC_NR_SET_REG, struct isp_regs_info)
#define ISP_IOC_GET_REG \
	_IOWR(IOC_K1X_ISP_TYPE, ISP_IOC_NR_GET_REG, struct isp_regs_info)
#define ISP_IOC_SET_PDC \
	_IOW(IOC_K1X_ISP_TYPE, ISP_IOC_NR_SET_PDC, struct isp_pdc_ctrl_info)
#define ISP_IOC_SET_JOB \
	_IOW(IOC_K1X_ISP_TYPE, ISP_IOC_NR_SET_JOB, struct isp_job_describer)
#define ISP_IOC_GET_INTERRUPT \
	_IOWR(IOC_K1X_ISP_TYPE, ISP_IOC_NR_GET_INTERRUPT, struct isp_user_task_info)
#define ISP_IOC_REQUEST_BUFFER \
	_IOW(IOC_K1X_ISP_TYPE, ISP_IOC_NR_REQUEST_BUFFER, struct isp_buffer_request_info)
#define ISP_IOC_ENQUEUE_BUFFER \
	_IOW(IOC_K1X_ISP_TYPE, ISP_IOC_NR_ENQUEUE_BUFFER, struct isp_buffer_enqueue_info)
#define ISP_IOC_FLUSH_BUFFER \
	_IO(IOC_K1X_ISP_TYPE, ISP_IOC_NR_FLUSH_BUFFER)
#define ISP_IOC_TRIGGER_CAPTURE \
	_IOWR(IOC_K1X_ISP_TYPE, ISP_IOC_NR_TRIGGER_CAPTURE, struct isp_capture_package)
#define ISP_IOC_SET_SINGLE_REG \
	_IOW(IOC_K1X_ISP_TYPE, ISP_IOC_NR_SET_SINGLE_REG, struct isp_reg_unit)
#define ISP_IOC_SET_END_FRAME_WORK \
	_IOW(IOC_K1X_ISP_TYPE, ISP_IOC_NR_SET_END_FRAME_WORK, struct isp_endframe_work_info)

#endif /* __K1X_ISP_DRV_UAPI_H__ */
