/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 ARM Ltd.
 */

#ifndef __LINUX_TRUSTY_FFA_H
#define __LINUX_TRUSTY_FFA_H

#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/arm_ffa.h>

#define TRUSTY_FFA_VERSION_MAJOR	(1U)
#define TRUSTY_FFA_VERSION_MINOR	(0U)
#define TRUSTY_FFA_VERSION_MAJOR_SHIFT	(16U)
#define TRUSTY_FFA_VERSION_MAJOR_MASK	(0x7fffU)
#define TRUSTY_FFA_VERSION_MINOR_SHIFT	(0U)
#define TRUSTY_FFA_VERSION_MINOR_MASK	(0U)

#define TO_TRUSTY_FFA_MAJOR(v)					\
	  ((u16)((v >> TRUSTY_FFA_VERSION_MAJOR_SHIFT) &	\
		 TRUSTY_FFA_VERSION_MAJOR_MASK))

#define TO_TRUSTY_FFA_MINOR(v)					\
	  ((u16)((v >> TRUSTY_FFA_VERSION_MINOR_SHIFT) &	\
		 TRUSTY_FFA_VERSION_MINOR_MASK))

#endif /* __LINUX_TRUSTY_FFA_H */
