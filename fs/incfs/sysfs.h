/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC
 */
#ifndef _INCFS_SYSFS_H
#define _INCFS_SYSFS_H

struct mount_info;

int incfs_init_sysfs(void);
void incfs_cleanup_sysfs(void);
int incfs_add_sysfs_node(struct mount_info *mi, const char* sysfs_name);
void incfs_free_sysfs_node(struct mount_info *mi);

#endif
