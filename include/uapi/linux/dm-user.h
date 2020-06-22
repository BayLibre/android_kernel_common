/* SPDX-License-Identifier: LGPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (C) 2020 Palmer Dabbelt <palmerdabbelt@google.com>
 */

#ifndef _LINUX_DM_USER_H
#define _LINUX_DM_USER_H

#include <linux/types.h>

/*
 * dm-user proxies device mapper ops between the kernel and userspace.  It's
 * essentially just an RPC mechanism: all kernel calls create a request,
 * userspace handles that with a response.  Userspace obtains requests via
 * read() and provides responses via write().
 */

#define DM_USER_MAP_READ	0
#define DM_USER_MAP_WRITE	1

struct dm_user_message {
	unsigned long seq;
	unsigned long type;
	unsigned long flags;
	unsigned long len;
	char buf[];
};

#endif
