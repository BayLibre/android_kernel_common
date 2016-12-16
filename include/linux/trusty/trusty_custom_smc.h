/*
 * Copyright (C) 2016-2017 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __LINUX_TRUSTY_TRUSTY_CUSTOM_SMC_H
#define __LINUX_TRUSTY_TRUSTY_CUSTOM_SMC_H

#ifdef CONFIG_TRUSTY_CUSTOM_SMC

#include <linux/kernel.h>
#include <linux/trusty/sm_err.h>
#include <linux/device.h>
#include <linux/pagemap.h>

struct trusty_custom_smc {
	ulong (*smc)(ulong r0, ulong r1, ulong r2, ulong r3,
		     struct trusty_custom_smc *dev);
};

static inline struct trusty_custom_smc *trusty_custom_smc_get_drvdata(
	const struct device *dev)
{
	return dev_get_drvdata(dev);
}

static inline void trusty_custom_smc_set_drvdata(struct device *dev,
						 struct trusty_custom_smc *data)
{
	dev_set_drvdata(dev, data);
}

#else

struct trusty_custom_smc;

#endif

#endif
