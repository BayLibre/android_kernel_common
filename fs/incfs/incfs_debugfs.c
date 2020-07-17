// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

static struct dentry *debugfs_root;

u64 incfs_n_read; // Total number of reads (data)
u64 incfs_n_read_lz4; // Total number of reads (compressed-data)
atomic_t incfs_n_op_read; // In-flight reads
u32 incfs_n_read_err; // Read errors
atomic_t incfs_n_op_read_wait;  // In-flight reads waiting for data block
atomic_t incfs_n_op_read_blocks; // In-flight reads getting block information

atomic_t incfs_n_op_write; // In-flight write
u64 incfs_n_write; // Total number of write
u32 incfs_n_write_err; // Write errors

atomic_t incfs_active_mounts; // Number of active mount points

int incfs_stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Read:      n=%llu n-compression=%llu err=%u\n",
		incfs_n_read,
		incfs_n_read_lz4,
		incfs_n_read_err);

	seq_printf(m, "Write:     n=%llu err=%u\n",
		incfs_n_write,
		incfs_n_write_err);

	seq_printf(m, "Ops:       Read=%u Read-Wait=%u Write=%u Read-Blockmap=%u Active-mounts=%u\n",
		atomic_read(&incfs_n_op_read),
		atomic_read(&incfs_n_op_read_wait),
		atomic_read(&incfs_n_op_write),
		atomic_read(&incfs_n_op_read_blocks),
		atomic_read(&incfs_active_mounts));

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(incfs_stats);

/*
 * initialise the /sys/kernel/debug/incfs_stats/stats
 */
void incfs_debugfs_init(void)
{
	debugfs_root = debugfs_create_dir("incfs_stats", NULL);

	debugfs_create_file("stats", S_IFREG | 0444, debugfs_root,
			NULL, &incfs_stats_fops);
}

/*
 * clean up the /sys/kernel/debug/incfs_stats directory
 */
void incfs_debugfs_cleanup(void)
{
	debugfs_remove_recursive(debugfs_root);
}
