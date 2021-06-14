// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google

#include <stdbool.h>
#include <stdint.h>

#define SEC(NAME) __attribute__((section(NAME), used))

static long (*bpf_trace_printk)(const char *fmt, uint32_t fmt_size, ...)
	= (void *) 6;

#define bpf_printk(fmt, ...)					\
	({			                                \
		char ____fmt[] = fmt;                           \
	        bpf_trace_printk(____fmt, sizeof(____fmt),      \
		                 ##__VA_ARGS__);                \
	})

SEC("test_trace")

int trace(unsigned *ctx)
{
	if (ctx[2] > 1000)
		bpf_printk("Hello Paul: %u\n", ctx[2]);
	else
		bpf_printk("Goodbye: %u\n", ctx[2]);
	return 2;
}
