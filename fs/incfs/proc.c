// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "stats.h"

/*
 * initialise the /proc/fs/incfs/ directory
 */
int incfs_proc_init(void)
{
	if (!proc_mkdir("fs/incfs", NULL))
		goto error_dir;

#ifdef CONFIG_INCFS_STATS
	if (!proc_create_single("fs/incfs/stats", S_IFREG | 0444, NULL,
			incfs_stats_show))
		goto error_stats;
#endif

	return 0;

#ifdef CONFIG_INCFS_STATS
error_stats:
#endif
	remove_proc_entry("fs/incfs", NULL);
error_dir:
	return -ENOMEM;
}

/*
 * clean up the /proc/fs/incfs/ directory
 */
void incfs_proc_cleanup(void)
{
#ifdef CONFIG_INCFS_STATS
	remove_proc_entry("fs/incfs/stats", NULL);
#endif
	remove_proc_entry("fs/incfs", NULL);
}
