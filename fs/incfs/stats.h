/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC
 */

#ifndef _INCFS_STATS_H
#define _INCFS_STATS_H

extern u64 incfs_n_read; // Total number of reads (data)
extern u64 incfs_n_read_lz4; // Total number of reads (compressed-data)
extern atomic_t incfs_n_op_read; // In-flight reads
extern u32 incfs_n_read_err; // Read errors
extern atomic_t incfs_n_op_read_wait;  // In-flight reads waiting for data block
extern atomic_t incfs_n_op_read_blocks; // In-flight reads getting block information

extern atomic_t incfs_n_op_write; // In-flight write
extern u64 incfs_n_write; // Total number of write
extern u32 incfs_n_write_err; // Write errors

extern atomic_t incfs_active_mounts; // Number of active mount points

#endif
