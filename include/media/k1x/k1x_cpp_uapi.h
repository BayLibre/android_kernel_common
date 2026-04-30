/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * k1x_cpp_uapi.h - Spacemit K1X CPP UAPI definitions
 *
 * Copyright(C) 2023 SPACEMIT Micro Limited.
 */

#ifndef __K1X_CPP_UAPI_H__
#define __K1X_CPP_UAPI_H__

#include <linux/videodev2.h>
#include <media/media-entity.h>

/* Maximum number of register commands per frame */
#define MAX_REG_CMDS		64

/* Maximum number of register data entries per command */
#define MAX_REG_DATA		512

/* Maximum number of DMA channels per port */
#define MAX_DMA_CHNLS_UAPI	17

/* Maximum number of layers */
#define CPP_MAX_LAYERS		5

/* Maximum number of planes per layer */
#define CPP_MAX_PLANAR		2

/* CPP hardware versions */
#define CPP_HW_VERSION_1_0	0x10
#define CPP_HW_VERSION_2_0	0x20
#define CPP_HW_VERSION_2_1	0x21

/* Media entity function for K1X CPP */
#define MEDIA_ENT_F_K1X_CPP	(MEDIA_ENT_F_PROC_VIDEO_COMPOSER + 0x1000)

/* CPP pixel formats */
enum cpp_pix_format {
	PIXFMT_NV12,
	PIXFMT_NV21,
	PIXFMT_P010,
	PIXFMT_DWT,
	PIXFMT_FBC_DWT,
	PIXFMT_MAX,
};

/* CPP register configuration types */
enum cpp_reg_cfg_type {
	CPP_WRITE32,
	CPP_READ32,
	CPP_WRITE32_RLX,
	CPP_WRITE32_NOP,
};

/**
 * struct reg_val_mask_info - Register value and mask information
 * @reg_offset: Register offset
 * @val: Register value
 * @mask: Register mask
 */
struct reg_val_mask_info {
	__u32 reg_offset;
	__u32 val;
	__u32 mask;
};

/**
 * struct cpp_reg_cfg_cmd - CPP register configuration command
 * @reg_type: Type of register operation
 * @reg_len: Number of register entries
 * @reg_data: Pointer to array of register value/mask info
 */
struct cpp_reg_cfg_cmd {
	enum cpp_reg_cfg_type reg_type;
	__u32 reg_len;
	struct reg_val_mask_info *reg_data;
};

/**
 * struct k1x_cpp_reg_cfg - K1X CPP register configuration (single reg)
 * @cmd_type: Command type (CPP_WRITE32, CPP_READ32, etc.)
 * @u: Union for different config types
 * @u.rw_info: Read/write info for single register operations
 */
struct k1x_cpp_reg_cfg {
	enum cpp_reg_cfg_type cmd_type;
	union {
		struct reg_val_mask_info rw_info;
	} u;
};

/**
 * struct cpp_hw_info - CPP hardware information
 * @cpp_hw_version: Hardware version identifier
 * @low_pwr_mode: Low power mode flag
 */
struct cpp_hw_info {
	__u32 cpp_hw_version;
	__u32 low_pwr_mode;
};

/**
 * struct cpp_bandwidth_info - CPP bandwidth information
 * @rsum: Read bandwidth sum
 * @wsum: Write bandwidth sum
 */
struct cpp_bandwidth_info {
	__s32 rsum;
	__s32 wsum;
};

/**
 * struct cpp_clock_info - CPP clock information
 * @func_rate: Function clock rate
 * @bus_rate: Bus clock rate
 */
struct cpp_clock_info {
	__u64 func_rate;
	__u64 bus_rate;
};

/**
 * struct cpp_plane_info - CPP plane buffer information
 * @m: Memory union for different buffer types
 * @m.fd: File descriptor for dma-buf
 * @m.userptr: User pointer for userptr buffers
 * @length: Length of the buffer
 * @data_offset: Data offset within the buffer
 */
struct cpp_plane_info {
	union {
		__s32 fd;
		unsigned long userptr;
	} m;
	__u32 length;
	__u32 data_offset;
};

/**
 * struct cpp_buffer_info - CPP buffer information for frame processing
 * @index: Buffer index
 * @num_layers: Number of DWT layers
 * @format: Pixel format (enum cpp_pix_format)
 * @kgain_used: Whether K-gain planes are used
 * @reserved: Reserved for alignment
 * @dwt_planes: DWT plane info array [layer][plane]
 * @kgain_planes: K-gain plane info array [layer]
 */
struct cpp_buffer_info {
	__u32 index;
	__u32 num_layers;
	enum cpp_pix_format format;
	__u32 kgain_used;
	__u32 reserved[2];
	struct cpp_plane_info dwt_planes[CPP_MAX_LAYERS][CPP_MAX_PLANAR];
	struct cpp_plane_info kgain_planes[CPP_MAX_LAYERS];
};

/**
 * struct cpp_frame_info - CPP frame processing information
 * @frame_id: Frame identifier
 * @client_id: Client identifier
 * @regs: Array of register configuration commands
 * @src_buf_info: Source buffer information
 * @dst_buf_info: Destination buffer information
 * @pre_buf_info: Previous frame buffer information (for temporal filtering)
 */
struct cpp_frame_info {
	__u32 frame_id;
	__u32 client_id;
	struct cpp_reg_cfg_cmd regs[MAX_REG_CMDS];
	struct cpp_buffer_info src_buf_info;
	struct cpp_buffer_info dst_buf_info;
	struct cpp_buffer_info pre_buf_info;
};

/**
 * struct cpp_frame_done_info - CPP frame done event info
 * @success: Whether frame processing was successful
 * @frame_id: Frame identifier
 * @client_id: Client identifier
 * @seg_reg_cfg: Time segment for register configuration (ns)
 * @seg_stream: Time segment for streaming (ns)
 */
struct cpp_frame_done_info {
	__u32 success;
	__u32 frame_id;
	__u32 client_id;
	__u64 seg_reg_cfg;
	__u64 seg_stream;
};

/**
 * struct cpp_frame_err_info - CPP frame error event info
 * @err_type: Error type
 * @frame_id: Frame identifier
 * @client_id: Client identifier
 */
struct cpp_frame_err_info {
	__u32 err_type;
	__u32 frame_id;
	__u32 client_id;
};

/**
 * struct k1x_cpp_event_data - CPP event data for V4L2 events
 * @u: Union for different event types
 * @u.done_info: Frame done event info
 * @u.err_info: Frame error event info
 */
struct k1x_cpp_event_data {
	union {
		struct cpp_frame_done_info done_info;
		struct cpp_frame_err_info err_info;
	} u;
};

/* CPP V4L2 event types */
#define V4L2_EVENT_CPP_FRAME_DONE	(V4L2_EVENT_PRIVATE_START + 0)
#define V4L2_EVENT_CPP_FRAME_ERR	(V4L2_EVENT_PRIVATE_START + 1)

/* CPP private ioctls */
#define VIDIOC_K1X_CPP_HW_INFO		_IOR('V', BASE_VIDIOC_PRIVATE + 0, struct cpp_hw_info)
#define VIDIOC_K1X_CPP_PROCESS_FRAME	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct cpp_frame_info)
#define VIDIOC_K1X_CPP_REG_CFG		_IOW('V', BASE_VIDIOC_PRIVATE + 2, struct k1x_cpp_reg_cfg)
#define VIDIOC_K1X_CPP_HW_RST		_IO('V', BASE_VIDIOC_PRIVATE + 3)
#define VIDIOC_K1X_CPP_LOW_PWR		_IOW('V', BASE_VIDIOC_PRIVATE + 4, __u32)
#define VIDIOC_K1X_CPP_FLUSH_QUEUE	_IO('V', BASE_VIDIOC_PRIVATE + 5)
#define VIDIOC_K1X_CPP_IOMMU_ATTACH	_IO('V', BASE_VIDIOC_PRIVATE + 6)
#define VIDIOC_K1X_CPP_IOMMU_DETACH	_IO('V', BASE_VIDIOC_PRIVATE + 7)
#define VIDIOC_K1X_CPP_UPDATE_BANDWIDTH	_IOW('V', BASE_VIDIOC_PRIVATE + 8, struct cpp_bandwidth_info)
#define VIDIOC_K1X_CPP_UPDATE_CLOCKRATE	_IOW('V', BASE_VIDIOC_PRIVATE + 9, struct cpp_clock_info)

#endif /* __K1X_CPP_UAPI_H__ */
