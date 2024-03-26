/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google, Inc.
 */

#ifndef _LINUX_BINDER_GENL_H
#define _LINUX_BINDER_GENL_H

#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/genetlink.h>
#include <uapi/linux/android/binder.h>

#include "binder_internal.h"

int init_binder_netlink(void);

bool binder_report_enabled(uint32_t mask);

void binder_send_report(struct binder_context *context, int err,
		int from_pid, int from_tid, int to_pid, int to_tid,
		int reply, int flags, int code, int size);

#endif /* _LINUX_BINDER_GENL_H */