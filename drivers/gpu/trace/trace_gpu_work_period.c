// SPDX-License-Identifier: GPL-2.0
/*
 * GPU memory trace points
 *
 * Copyright (C) 2023 Google, Inc.
 */

#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/gpu_worker_period.h>

EXPORT_TRACEPOINT_SYMBOL(gpu_work_period);
