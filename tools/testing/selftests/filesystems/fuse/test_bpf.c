// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google

#define __EXPORTED_HEADERS__

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

SEC("dummy")

#define fake_name "fake"

inline int strcmp(const char *a, const char *b)
{
	int i;

	for (i = 0; i < __builtin_strlen(b) + 1; ++i)
		if (a[i] != b[i])
			return -1;

	return 0;
}

SEC("test_trace")

int trace(struct bpf_fuse_data *ctx)
{
	int fake = strcmp(ctx->name, "fake");

	bpf_printk("Hello Paul: %s %d\n", ctx->name, fake);
	return fake ? 1 : 0;
}
