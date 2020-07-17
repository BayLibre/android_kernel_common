/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC
 */


#include "stats.h"

atomic64_t incfs_n_read; // Total number of reads (data)
atomic64_t incfs_n_read_lz4; // Total number of reads (compressed-data)
atomic_t incfs_n_op_read; // In-flight reads
atomic_t incfs_n_read_err; // Read errors
atomic_t incfs_n_op_read_wait;  // In-flight reads waiting for data block
atomic_t incfs_n_op_read_blocks; // In-flight reads getting block information

atomic_t incfs_n_op_write; // In-flight write
atomic64_t incfs_n_write; // Total number of write
atomic_t incfs_n_write_err; // Write errors

atomic_t incfs_active_mounts; // Number of active mount points

int incfs_stats_show(struct seq_file *m, void *v)
{
        seq_puts(m, "Incremental fs statistics\n\n");

        seq_printf(m, "Read:      n=%llu n-compression=%llu err=%u\n",
                atomic64_read(&incfs_n_read),
                atomic64_read(&incfs_n_read_lz4),
                atomic_read(&incfs_n_read_err));

        seq_printf(m, "Write:     n=%llu err=%u\n",
                atomic64_read(&incfs_n_write),
                atomic_read(&incfs_n_write_err));


        seq_printf(m, "Ops:       Read=%u Read-Wait=%u Write=%u Read-Blockmap=%u Active-mounts=%u\n",
                atomic_read(&incfs_n_op_read),
                atomic_read(&incfs_n_op_read_wait),
                atomic_read(&incfs_n_op_write),
                atomic_read(&incfs_n_op_read_blocks),
                atomic_read(&incfs_active_mounts));

        return 0;
}
