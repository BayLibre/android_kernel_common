/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * k1x_ccic_uapi.h - Spacemit K1X CCIC UAPI definitions
 *
 * Copyright(C) 2023 SPACEMIT Micro Limited.
 */

#ifndef __K1X_CCIC_UAPI_H__
#define __K1X_CCIC_UAPI_H__

#include <linux/videodev2.h>

/* CCIC operating modes */
#define CCIC_MODE_NM		0	/* Normal mode */
#define CCIC_MODE_VC		1	/* Virtual Channel mode */

/* CCIC channel modes */
#define CCIC_CH_MODE_MAIN	0	/* Main channel */
#define CCIC_CH_MODE_SUB	1	/* Sub channel */

/**
 * struct v4l2_ccic_params - CCIC configuration parameters
 * @lane_num: Number of MIPI lanes
 * @ccic_mode: CCIC operating mode (CCIC_MODE_NM or CCIC_MODE_VC)
 * @ch_mode: Channel mode (CCIC_CH_MODE_MAIN or CCIC_CH_MODE_SUB)
 * @main_vc: Main virtual channel ID
 * @sub_vc: Sub virtual channel ID
 * @main_dt: Main data type
 * @sub_dt: Sub data type
 * @main_ccic_id: Main CCIC controller ID
 */
struct v4l2_ccic_params {
	__u32 lane_num;
	__u32 ccic_mode;
	__u32 ch_mode;
	__u32 main_vc;
	__u32 sub_vc;
	__u32 main_dt;
	__u32 sub_dt;
	__u32 main_ccic_id;
};

/* CCIC private ioctls */
#define VIDIOC_CCIC_S_PARAMS	_IOW('V', BASE_VIDIOC_PRIVATE + 0, struct v4l2_ccic_params)

#endif /* __K1X_CCIC_UAPI_H__ */
