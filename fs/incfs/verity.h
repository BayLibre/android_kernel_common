/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC
 */

#ifndef _INCFS_VERITY_H
#define _INCFS_VERITY_H

int ioctl_enable_verity(struct file *filp, const void __user *uarg);

int incfs_fsverity_file_open(struct inode *inode, struct file *filp);

int incfs_verity_measure(struct file *filp, void __user *_uarg);
#endif
