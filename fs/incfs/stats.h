/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC
 */

#ifndef _INCFS_STATS_H
#define _INCFS_STATS_H

#include <linux/seq_file.h>
#include <linux/atomic.h>

extern int incfs_stats_show(struct seq_file *m, void *v);

#ifdef CONFIG_INCFS_STATS

extern atomic64_t incfs_n_read; // Total number of reads (data)
extern atomic64_t incfs_n_read_lz4; // Total number of reads (compressed-data)
extern atomic_t incfs_n_op_read; // In-flight reads
extern atomic_t incfs_n_read_err; // Read errors
extern atomic_t incfs_n_op_read_wait;  // In-flight reads waiting for data block
extern atomic_t incfs_n_op_read_blocks; // In-flight reads getting block information

extern atomic_t incfs_n_op_write; // In-flight write
extern atomic64_t incfs_n_write; // Total number of write
extern atomic_t incfs_n_write_err; // Write errors

extern atomic_t incfs_active_mounts; // Number of active mount points

int incfs_debugfs_open(struct inode *inode, struct file *file);

static inline void incfs_stat(atomic_t *stat)
{
	atomic_inc(stat);
}

static inline void incfs_stat64(atomic64_t *stat)
{
	atomic64_inc(stat);
}

static inline void incfs_stat_d(atomic_t *stat)
{
	atomic_dec(stat);
}

static const struct file_operations incfs_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = incfs_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#else

#define incfs_stat(stat) do {} while (0)
#define incfs_stat64(stat) do {} while (0)
#define incfs_stat_d(stat) do {} while (0)

#endif

#endif
