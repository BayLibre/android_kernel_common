// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "stats.h"

static struct dentry *debugfs_root;

/*
 * initialise the /sys/kernel/debugfs/incfs/stats
 */
void incfs_debugfs_init(void)
{
	debugfs_root = debugfs_create_dir("incfs", NULL);

	debugfs_create_file("stats", S_IFREG | 0444, debugfs_root,
			NULL, &incfs_debugfs_fops);
}

/*
 * clean up the /sys/kernel/debugfs/ directory
 */
void incfs_debugfs_cleanup(void)
{
	if (debugfs_root)
		debugfs_remove_recursive(debugfs_root);
}
