// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024-2024 XRing Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>

#include <trace/hooks/mm.h>

#include "xring_lb_drv.h"
#include "xring_lb_def.h"
#include "xring_lb_collect.h"
#include "xring_lb_preread.h"
#include "xring_lb_clear.h"
#include "xring_lb_flush.h"
#include "xring_lb_dbg.h"

struct lb_data g_lb_data;
static bool lb_enabled;

static int xring_lb_register_hooks(void)
{
	int ret;

	ret = register_trace_android_vh_filemap_read(
			xring_lb_page_collect_on_readfile, NULL);
	if (ret) {
		XRING_LB_ERR("register hook xring_lb_page_collect_on_readfile failed");
		goto err;
	}

	ret = register_trace_android_vh_filemap_map_pages_range(
			xring_lb_page_collect_on_pagefault, NULL);
	if (ret) {
		XRING_LB_ERR("register hook xring_lb_page_collect_on_pagefault failed");
		goto err_unregister1;
	}

	return 0;

err_unregister1:
	unregister_trace_android_vh_filemap_read(
			xring_lb_page_collect_on_readfile, NULL);
err:
	return ret;
}

static long xring_lb_ioctl(struct file *filp, uint32_t cmd, unsigned long arg)
{
	int ret = 0;

	if (!g_lb_data.initialized)
		return -1;

	switch (cmd) {
	case LB_COLLECT:
		ret = xring_lb_dealwith_collect_cmd(arg);
		if (ret)
			XRING_LB_ERR("lb collect pagecache fail");
		break;
	case LB_PREREAD:
		ret = xring_lb_dealwith_preread_cmd(arg);
		if (ret)
			XRING_LB_ERR("lb preread fail");
		break;
	case LB_CLEAR:
		ret = xring_lb_dealwith_clear_cmd(arg);
		if (ret)
			XRING_LB_ERR("lb clear fail");
		break;
	case LB_FLUSH:
		ret = xring_lb_dealwith_flush_cmd(arg);
		if (ret)
			XRING_LB_ERR("lb flush fail");
		break;
	case LB_STOP:
		ret = xring_lb_dealwith_stop_cmd(arg);
		if (ret)
			XRING_LB_ERR("lb stop fail");
		break;
	case LB_RECOVERY:
		ret = xring_lb_dealwith_recovery_cmd(arg);
		if (ret)
			XRING_LB_ERR("lb recovery fail");
		break;
	default:
		XRING_LB_ERR("unknown cmd, cmd is %u", cmd);
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

static const struct file_operations lb_fops = {
	.owner = THIS_MODULE,
	.open = NULL,
	.release = NULL,
	.compat_ioctl = xring_lb_ioctl,
	.unlocked_ioctl = xring_lb_ioctl,
};

static int xring_lb_chrdev_register(struct lb_data *data)
{
	int ret = 0;

	data->chr_major = register_chrdev(0, LB_DEV_NAME, &lb_fops);
	if (data->chr_major < 0) {
		XRING_LB_ERR("register chrdev fail\n");
		return -ENXIO;
	}

	data->chr_class = class_create("lb");
	if (IS_ERR_OR_NULL(data->chr_class)) {
		XRING_LB_ERR("class create fail");
		ret = PTR_ERR(data->chr_class);
		unregister_chrdev(data->chr_major, LB_DEV_NAME);
		return ret;
	}

	data->chr_dev = device_create(
		data->chr_class, 0, MKDEV(data->chr_major, 0), NULL, LB_DEV_NAME);
	if (IS_ERR_OR_NULL(data->chr_class)) {
		XRING_LB_ERR("class create fail");
		ret = PTR_ERR(data->chr_class);
		unregister_chrdev(data->chr_major, LB_DEV_NAME);
		return ret;
	}

	dev_set_drvdata(data->chr_dev, data);

	return 0;
}

static int xring_lb_chrdev_unregister(struct lb_data *priv)
{
	if (priv->chr_major > 0) {
		device_destroy(priv->chr_class, MKDEV(priv->chr_major, 0));
		class_destroy(priv->chr_class);
		unregister_chrdev(priv->chr_major, LB_DEV_NAME);
	}

	return 0;
}

static int __init xring_lb_init(void)
{
	int ret;
	struct lb_data *data = &g_lb_data;

	if (!lb_enabled)
		return 0;

	memset(data, 0, sizeof(*data));
	data->initialized = false;

	ret = xring_lb_chrdev_register(data);
	if (ret)
		return ret;

	ret = xring_lb_init_collector();
	if (ret) {
		XRING_LB_ERR("init collector failed\n");
		goto unregister_chrdev;
	}

	ret = xring_lb_init_preread();
	if (ret) {
		XRING_LB_ERR("init preread failed\n");
		goto free_collector;
	}

	ret = xring_lb_dbg_init();
	if (ret) {
		XRING_LB_ERR("dbg init failed\n");
		goto free_preread;
	}

	ret = xring_lb_register_hooks();
	if (ret) {
		XRING_LB_ERR("register hooks failed\n");
		goto remove_proc;
	}

	data->initialized = true;
	return ret;

remove_proc:
	remove_proc_entry("collect", NULL);
	remove_proc_entry("launch_boost", NULL);
free_preread:
	kfree(data->preread);
free_collector:
	kfifo_free(&data->collector->fifo);
	kfree(data->collector);
unregister_chrdev:
	xring_lb_chrdev_unregister(data);

	return ret;
}

static void __exit xring_lb_exit(void)
{
	struct lb_data *priv = &g_lb_data;

	if (priv->initialized) {
		xring_lb_chrdev_unregister(priv);
		priv->initialized = false;
	}
}

MODULE_LICENSE("GPL");
module_init(xring_lb_init);
module_exit(xring_lb_exit);

static int __init launch_boost_setup(char *str)
{
	lb_enabled = true;
	return 0;
}

early_param("launch_boost_enabled", launch_boost_setup);
