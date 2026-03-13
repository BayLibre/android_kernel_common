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

static inline int create_migrate_type_subdirs(struct dentry *orderdir,
					      int nodeid, int zoneid, int order)
{
	struct dentry *migratedir;
	char dirname[24];

	for (int mtype = 0; mtype < MIGRATE_TYPES; mtype++) {
		snprintf(dirname, sizeof(dirname), "migrate-%s",
			 migratetype_names[mtype]);
		migratedir = debugfs_create_dir(dirname, orderdir);
		if (IS_ERR(migratedir))
			return PTR_ERR(migratedir);
	}

	return 0;
}

static inline int create_page_orders_subdirs(struct dentry *zonedir, int nodeid,
					     int zoneid, struct zone *zone)
{
	struct dentry *orderdir;
	struct free_area *free_area;
	char dirname[12];
	int ret;

	for (int order = 0; order < NR_PAGE_ORDERS; order++) {
		free_area = &(zone->free_area[order]);
		snprintf(dirname, sizeof(dirname), "order-%d", order);
		orderdir = debugfs_create_dir(dirname, zonedir);
		if (IS_ERR(orderdir))
			return PTR_ERR(orderdir);

		ret = create_migrate_type_subdirs(orderdir, nodeid, zoneid,
						  order);
		if (ret)
			return ret;
	}

	return 0;
}

static inline int create_zones_subdirs(struct dentry *nodedir, int nodeid,
				       struct pglist_data *pgdata)
{
	struct dentry *zonedir;
	struct zone *zone;
	struct zone *node_zones = pgdata->node_zones;
	int zoneid;
	char dirname[24];
	int ret;

	for (zone = node_zones, zoneid = 0; zone - node_zones < MAX_NR_ZONES;
	     ++zone, ++zoneid) {
		if (!populated_zone(zone))
			continue;

		snprintf(dirname, sizeof(dirname), "zone-%s", zone->name);
		zonedir = debugfs_create_dir(dirname, nodedir);
		if (IS_ERR(zonedir))
			return PTR_ERR(zonedir);

		ret = create_page_orders_subdirs(zonedir, nodeid, zoneid, zone);
		if (ret)
			return ret;
	}

	return 0;
}

static inline int create_nodes_subdirs(struct dentry *mmdir)
{
	struct dentry *nodedir;
	int nodeid;
	char dirname[12];
	int ret;

	for_each_online_node(nodeid) {
		struct pglist_data *pgdata = NODE_DATA(nodeid);

		snprintf(dirname, sizeof(dirname), "node-%d", nodeid);
		nodedir = debugfs_create_dir(dirname, mmdir);
		if (IS_ERR(nodedir))
			return PTR_ERR(nodedir);

		ret = create_zones_subdirs(nodedir, nodeid, pgdata);
		if (ret)
			return ret;
	}

	return 0;
}
static int __init page_alloc_debugfs_init(void)
{
	int ret;

	pr_info("Starting");
	mmdir = debugfs_create_dir("mm", NULL);
	if (IS_ERR(mmdir)) {
		pr_err("Unable to create mm directory");
		return PTR_ERR(mmdir);
	}

	ret = create_nodes_subdirs(mmdir);
	if (ret)
		goto clean_dir;

	return 0;

clean_dir:
	debugfs_remove_recursive(mmdir);

	return ret;
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

