// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google

#include <uapi/linux/types.h>
#include <uapi/linux/bpf_fuse.h>

#define SEC(NAME) __attribute__((section(NAME), used))

static long (*bpf_trace_printk)(const char *fmt, __u32 fmt_size, ...)
	= (void *) 6;

#define bpf_printk(fmt, ...)					\
	({			                                \
		char ____fmt[] = fmt;                           \
	        bpf_trace_printk(____fmt, sizeof(____fmt),      \
		                 ##__VA_ARGS__);                \
	})

SEC("test_trace")

int trace(struct bpf_fuse_data *ctx)
{
	bpf_printk("Hello Paul: %s\n", ctx->name);
	return 2;
}
