// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * exp_attr.c - Kernel Experiments module
 *
 * Copyright (C) 2025 Deepa Dinamani <deepadinamani@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/printk.h>

#include "exp_attr.h"

#include <trace/hooks/experiments.h>

struct exp_attr experiments[] = {
};

const size_t experiment_count = ARRAY_SIZE(experiments);
