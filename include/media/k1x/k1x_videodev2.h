/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * k1x_videodev2.h - Spacemit K1X V4L2 UAPI extensions
 *
 * Copyright(C) 2023 SPACEMIT Micro Limited.
 */

#ifndef __K1X_VIDEODEV2_H__
#define __K1X_VIDEODEV2_H__

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/videodev2.h>
#include <linux/media-bus-format.h>

/*
 * Spacemit K1X specific V4L2 pixel formats
 * Using fourcc values in the vendor-specific range
 */

/* Spacemit packed Greyscale/Bayer formats */
#define V4L2_PIX_FMT_SPACEMITGB8P	v4l2_fourcc('S', 'G', '8', 'P')
#define V4L2_PIX_FMT_SPACEMITGB10P	v4l2_fourcc('S', 'G', 'A', 'P')
#define V4L2_PIX_FMT_SPACEMITGB12P	v4l2_fourcc('S', 'G', 'C', 'P')
#define V4L2_PIX_FMT_SPACEMITGB14P	v4l2_fourcc('S', 'G', 'E', 'P')

/* AFBC compressed NV12 */
#define V4L2_PIX_FMT_NV12_AFBC		v4l2_fourcc('N', 'A', 'F', 'C')

/* P210: 10-bit YUV 4:2:2 planar format (Y, UV interleaved) */
#define V4L2_PIX_FMT_P210		v4l2_fourcc('P', '2', '1', '0')

/* DWT output formats - D010 with different layer indices */
#define V4L2_PIX_FMT_D010_1		v4l2_fourcc('D', '0', '1', '1')
#define V4L2_PIX_FMT_D010_2		v4l2_fourcc('D', '0', '1', '2')
#define V4L2_PIX_FMT_D010_3		v4l2_fourcc('D', '0', '1', '3')
#define V4L2_PIX_FMT_D010_4		v4l2_fourcc('D', '0', '1', '4')

/* DWT output formats - D210 with different layer indices */
#define V4L2_PIX_FMT_D210_1		v4l2_fourcc('D', '2', '1', '1')
#define V4L2_PIX_FMT_D210_2		v4l2_fourcc('D', '2', '1', '2')
#define V4L2_PIX_FMT_D210_3		v4l2_fourcc('D', '2', '1', '3')
#define V4L2_PIX_FMT_D210_4		v4l2_fourcc('D', '2', '1', '4')

/*
 * Spacemit K1X specific media bus formats
 * Using values in a vendor-specific range starting at 0x8000
 */

/* Spacemit packed RGB formats */
#define MEDIA_BUS_FMT_SRGB8_SPACEMITPACK_1X8	0x8001
#define MEDIA_BUS_FMT_SRGB10_SPACEMITPACK_1X10	0x8002
#define MEDIA_BUS_FMT_SRGB12_SPACEMITPACK_1X12	0x8003
#define MEDIA_BUS_FMT_SRGB14_SPACEMITPACK_1X14	0x8004

/* YUV 1.5x formats (4:2:0 subsampling) - Spacemit extensions */
/* Note: MEDIA_BUS_FMT_YUYV8_1_5X8 (0x2004) is already defined in media-bus-format.h */
#define MEDIA_BUS_FMT_YUYV8_1_5X8_AFBC		0x8011
#define MEDIA_BUS_FMT_YUYV10_1_5X10		0x8012
#define MEDIA_BUS_FMT_YUYV10_1_5X10_D1		0x8013
#define MEDIA_BUS_FMT_YUYV10_1_5X10_D2		0x8014
#define MEDIA_BUS_FMT_YUYV10_1_5X10_D3		0x8015
#define MEDIA_BUS_FMT_YUYV10_1_5X10_D4		0x8016

/* YVYU 1.5x formats (4:2:0 subsampling) with DWT layer indices */
#define MEDIA_BUS_FMT_YVYU10_1_5X10_D1		0x8017
#define MEDIA_BUS_FMT_YVYU10_1_5X10_D2		0x8018
#define MEDIA_BUS_FMT_YVYU10_1_5X10_D3		0x8019
#define MEDIA_BUS_FMT_YVYU10_1_5X10_D4		0x801A

/*
 * Spacemit K1X specific V4L2 buffer flags
 * Using vendor-specific flag bits
 */
#define V4l2_BUF_FLAG_FORCE_SHADOW	0x10000000
#define V4L2_BUF_FLAG_IGNOR		0x20000000
#define V4L2_BUF_FLAG_SLICES_DONE	0x40000000
#define V4L2_BUF_FLAG_ERROR_HW		0x01000000
#define V4L2_BUF_FLAG_ERROR_SW		0x02000000
#define V4L2_BUF_FLAG_IDI_OVERRUN	0x04000000

/*
 * Spacemit VI format field encoding
 * The v4l2_mbus_framefmt.field is used to pass VI-specific data:
 * Bits 0-15:  Primary data (sensor ID, MIPI lane count, etc.)
 * Bits 16-31: Switch flags
 */
#define SPACEMIT_VI_PRI_DATA_MASK		0x0000FFFF
#define SPACEMIT_VI_SWITCH_FLAGS_SHIFT		16

/* Primary data field bit layout */
#define SPACEMIT_VI_MIPI_LANE_MASK		0x000000FF
#define SPACEMIT_VI_SENSOR_ID_SHIFT		8
#define SPACEMIT_VI_SENSOR_ID_MASK		0x00000F00

/* Switch flags (applied after SPACEMIT_VI_SWITCH_FLAGS_SHIFT) */
#define SPACEMIT_VI_FLAG_BACK_TO_PREVIEW	0x0001
#define SPACEMIT_VI_FLAG_CLK_HIGH		0x0002
#define SPACEMIT_VI_FLAG_FORCE_SW_GAP		0x0004

/*
 * Spacemit K1X specific media entity function
 */
#define MEDIA_ENT_F_K1X_VI		0x80000001

/*
 * V4L2 subdev name size
 */
#define V4L2_SUBDEV_NAME_SIZE		32

/*
 * Spacemit VI entity name length
 */
#define SPACEMIT_VI_ENTITY_NAME_LEN	32

/*
 * Spacemit K1X specific V4L2 structures
 */

/**
 * struct v4l2_vi_entity_info - VI entity information
 * @id: media entity ID
 * @name: entity name
 */
struct v4l2_vi_entity_info {
	__u32 id;
	char name[SPACEMIT_VI_ENTITY_NAME_LEN];
};

/**
 * struct v4l2_vi_slice_info - VI slice information
 * @timeout: timeout in milliseconds
 * @slice_id: current slice ID
 * @total_slice_cnt: total number of slices
 */
struct v4l2_vi_slice_info {
	__u32 timeout;
	__s32 slice_id;
	__s32 total_slice_cnt;
};

/**
 * struct v4l2_vi_debug_dump - VI debug dump information
 * @reason: reason for the debug dump
 */
struct v4l2_vi_debug_dump {
	__u32 reason;
};

/**
 * struct v4l2_vi_port_cfg - VI port configuration
 * @port_entity_id: entity ID for the port
 * @offset: FIFO offset
 * @depth: FIFO depth
 * @weight: FIFO weight
 * @div_mode: division mode
 * @usage: port usage flags
 */
struct v4l2_vi_port_cfg {
	__u32 port_entity_id;
	__u32 offset;
	__u32 depth;
	__u32 weight;
	__u32 div_mode;
	__u32 usage;
};

/**
 * enum vi_input_interface_type - VI input interface types
 */
enum vi_input_interface_type {
	VI_INPUT_INTERFACE_MIPI = 0,
	VI_INPUT_INTERFACE_OFFLINE,
	VI_INPUT_INTERFACE_OFFLINE_SLICE,
};

/**
 * struct v4l2_vi_input_interface - VI input interface configuration
 * @type: input interface type (vi_input_interface_type)
 * @ccic_idx: CCIC index for MIPI interface
 */
struct v4l2_vi_input_interface {
	__u32 type;
	__u32 ccic_idx;
};

/**
 * struct v4l2_vi_dbg_reg - VI debug register access
 * @addr: register address (offset)
 * @value: register value
 * @mask: register mask for write operations
 */
struct v4l2_vi_dbg_reg {
	__u32 addr;
	__u32 value;
	__u32 mask;
};

/*
 * Spacemit K1X specific V4L2 ioctl commands
 * Using vendor-specific ioctl range (starting at 0xC0)
 */
#define VIDIOC_BASE_K1X_VI		0xC0

#define VIDIOC_GET_PIPELINE \
	_IO('V', VIDIOC_BASE_K1X_VI + 0)
#define VIDIOC_PUT_PIPELINE \
	_IOW('V', VIDIOC_BASE_K1X_VI + 1, int)
#define VIDIOC_APPLY_PIPELINE \
	_IO('V', VIDIOC_BASE_K1X_VI + 2)
#define VIDIOC_START_PIPELINE \
	_IO('V', VIDIOC_BASE_K1X_VI + 3)
#define VIDIOC_STOP_PIPELINE \
	_IO('V', VIDIOC_BASE_K1X_VI + 4)
#define VIDIOC_RESET_PIPELINE \
	_IOW('V', VIDIOC_BASE_K1X_VI + 5, int)
#define VIDIOC_G_ENTITY_INFO \
	_IOR('V', VIDIOC_BASE_K1X_VI + 6, struct v4l2_vi_entity_info)
#define VIDIOC_G_SLICE_MODE \
	_IOR('V', VIDIOC_BASE_K1X_VI + 7, int)
#define VIDIOC_QUERY_SLICE_READY \
	_IOWR('V', VIDIOC_BASE_K1X_VI + 8, struct v4l2_vi_slice_info)
#define VIDIOC_S_SLICE_DONE \
	_IOW('V', VIDIOC_BASE_K1X_VI + 9, int)
#define VIDIOC_CPU_Z1 \
	_IOR('V', VIDIOC_BASE_K1X_VI + 10, int)
#define VIDIOC_DEBUG_DUMP \
	_IOW('V', VIDIOC_BASE_K1X_VI + 11, struct v4l2_vi_debug_dump)
#define VIDIOC_G_PIPE_STATUS \
	_IOR('V', VIDIOC_BASE_K1X_VI + 12, unsigned int)
#define VIDIOC_S_PORT_CFG \
	_IOW('V', VIDIOC_BASE_K1X_VI + 13, struct v4l2_vi_port_cfg)
#define VIDIOC_CFG_INPUT_INTF \
	_IOW('V', VIDIOC_BASE_K1X_VI + 14, struct v4l2_vi_input_interface)
#define VIDIOC_S_BANDWIDTH \
	_IOW('V', VIDIOC_BASE_K1X_VI + 15, unsigned int)
#define VIDIOC_DBG_REG_READ \
	_IOWR('V', VIDIOC_BASE_K1X_VI + 16, struct v4l2_vi_dbg_reg)
#define VIDIOC_DBG_REG_WRITE \
	_IOW('V', VIDIOC_BASE_K1X_VI + 17, struct v4l2_vi_dbg_reg)
#define VIDIOC_GLOBAL_RESET \
	_IO('V', VIDIOC_BASE_K1X_VI + 18)
#define VIDIOC_FLUSH_BUFFERS \
	_IO('V', VIDIOC_BASE_K1X_VI + 19)

#endif /* __K1X_VIDEODEV2_H__ */
