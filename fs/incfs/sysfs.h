/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC
 */
#ifndef _INCFS_SYSFS_H
#define _INCFS_SYSFS_H

struct incfs_sysfs_node {
	struct kobject isn_sysfs_node;
	struct completion isn_kobj_unregister;
	int isn_reads_failed_timed_out;
	int isn_reads_failed_hash_verification;
	int isn_reads_failed_other;
	int isn_reads_delayed_per_uid;
	int isn_reads_delayed_other;
	atomic64_t isn_reads_total_delay_ns;
};

int incfs_init_sysfs(void);
void incfs_cleanup_sysfs(void);
struct incfs_sysfs_node *incfs_add_sysfs_node(const char *name);
void incfs_free_sysfs_node(struct incfs_sysfs_node *node);

#endif
