// SPDX-License-Identifier: GPL-2.0
/*
 * Page allocator DebugFS Interface
 *
 * This interface allows to make page allocations per node, zone, migrate type
 * and order using the DebugFS filesystem.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/nodemask.h>
#include <linux/printk.h>
#include <linux/spinlock.h>

struct dentry *mmdir;

static inline void create_migrate_type_subdirs(struct dentry *orderdir)
{
	struct dentry *migratedir;
	char dirname[24];

	for (int mtype = 0; mtype < MIGRATE_TYPES; mtype++) {
		snprintf(dirname, sizeof(dirname), "migrate-%s", migratetype_names[mtype]);
		migratedir = debugfs_create_dir(dirname, orderdir);
	}
}

static inline void create_page_orders_subdirs(struct zone *zone, struct dentry *zonedir)
{
	struct dentry *orderdir;
	struct free_area *free_area;
	char dirname[12];

	for (int order = 0; order < NR_PAGE_ORDERS; order++) {
		free_area = &(zone->free_area[order]);
		snprintf(dirname, sizeof(dirname), "order-%d", order);
		orderdir = debugfs_create_dir(dirname, zonedir);
		create_migrate_type_subdirs(orderdir);
	}
}

static inline void create_zones_subdirs(struct pglist_data *pgdata, struct dentry *nodedir)
{
	struct dentry *zonedir;
	struct zone *zone;
	struct zone *node_zones = pgdata->node_zones;
	unsigned long flags;
	char dirname[24];

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		if (!populated_zone(zone))
			continue;

		snprintf(dirname, sizeof(dirname), "zone-%s", zone->name);
		spin_lock_irqsave(&zone->lock, flags);
		zonedir = debugfs_create_dir(dirname, nodedir);
		create_page_orders_subdirs(zone, zonedir);
		spin_unlock_irqrestore(&zone->lock, flags);
	}
}

static inline void create_nodes_subdirs(struct dentry *mmdir)
{
	struct dentry *nodedir;
	int nodeid;
	char dirname[12];

	for_each_online_node(nodeid) {
		struct pglist_data *pgdata = NODE_DATA(nodeid);

		snprintf(dirname, sizeof(dirname), "node-%d", nodeid);
		nodedir = debugfs_create_dir(dirname, mmdir);
		create_zones_subdirs(pgdata, nodedir);
	}
}

static int __init page_alloc_debugfs_init(void)
{
	pr_info("Starting");
	mmdir = debugfs_create_dir("mm", NULL);
	if (IS_ERR(mmdir))
		return PTR_ERR(mmdir);

	create_nodes_subdirs(mmdir);

	return 0;
}

static void __exit page_alloc_debugfs_exit(void)
{
	debugfs_remove_recursive(mmdir);
}

module_init(page_alloc_debugfs_init);
module_exit(page_alloc_debugfs_exit);

MODULE_AUTHOR("Juan Yescas");
MODULE_DESCRIPTION("Module to alloc pages using the debugfs filesystem");
MODULE_LICENSE("GPL v2");

