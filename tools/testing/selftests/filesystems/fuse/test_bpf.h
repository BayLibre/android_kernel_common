// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google LLC

#ifndef TEST_BPF_H_
#define TEST_BPF_H_

#include <linux/fuse.h>
#include <linux/errno.h>
#include <uapi/linux/bpf.h>
#include <linux/types.h>

#include <stdbool.h>

#define SEC(NAME) __attribute__((section(NAME), used))

/* TODO replace with new map style */
struct fuse_bpf_map {
	int map_type;
	size_t key_size;
	size_t value_size;
	int max_entries;
};

static long (*bpf_trace_printk)(const char *fmt, __u32 fmt_size, ...)
	= (void *) 6;

#define bpf_printk(fmt, ...)					\
	({			                                \
		char ____fmt[] = fmt;                           \
		bpf_trace_printk(____fmt, sizeof(____fmt),      \
		                 ##__VA_ARGS__);                \
	})

/* Per inode storate definitions */
#define __uint(name, val) int (*name)[val]
#define __type(name, val) typeof(val) *name

static void *(*bpf_inode_storage_get)(void *map, struct inode *inode,
				void *value, u64 flags) __attribute__((unused))
	= (void *) 145;

static int (*bpf_inode_storage_delete)(void *map, struct inode *inode)
				__attribute__((unused))
	= (void *) 146;

/* Buffer writeable definitions */
static long (*bpf_fuse_get_writeable_in)(struct __bpf_fuse_args *fa, u32 index, void *value,
					 u64 size, bool copy)
	= (void *) 176;
static long (*bpf_fuse_get_writeable_out)(struct __bpf_fuse_args *fa, u32 index, void *value,
					  u64 size, bool copy)
	= (void *) 177;

#define bpf_make_writable_in(fa, index, size, copy)\
	(void *)bpf_fuse_get_writeable_in(fa, index, size, copy)
#define bpf_make_writable_out(fa, index, value, size, copy) \
	(void *)bpf_fuse_get_writeable_out(fa, index, (void *)(long)value, size, copy)

/* This is a macro to enforce inlining. Without it, the compiler will do the wrong thing for bpf */
#define strcmp_check(a, b, end_b) \
		(((b) + __builtin_strlen(a) + 1 > (end_b)) ? -1 : strcmp((b), (a)))


static inline int strcmp(const char *a, const char *b)
{
	int i;

	for (i = 0; i < __builtin_strlen(b) + 1; ++i)
		if (a[i] != b[i])
			return -1;
	return 0;
}

#endif /* TEST_BPF_H_ */
