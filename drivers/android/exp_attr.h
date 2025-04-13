// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * exp_attr.h - Kernel Experiments module
 *
 * Copyright (C) 2025 Deepa Dinamani <deepadinamani@google.com>
 */

#ifndef EXPERIMENTS_ATTRIBUTE_H
#define EXPERIMENTS_ATTRIBUTE_H

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/tracepoint.h>

struct exp_attr {
	struct attribute attr;
	int enable;
	struct tracepoint *tp;
	void* probe_fn;
};

#define EXPERIMENT(_name) { \
	.attr = { .name = __stringify(_name), .mode = 0664 }, \
	.enable = 0, \
	.tp = &__tracepoint_##android_vh_##_name, \
	.probe_fn = (void*)android_vh_##_name, \
}

extern const size_t experiment_count;
extern struct exp_attr experiments[];

#endif
