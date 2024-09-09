/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ASHMEM_COMPAT_H
#define __LINUX_ASHMEM_COMPAT_H

/*
 * mm/ashmem_compat.h
 *
 * Ashmem compatability for memfd in Android
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Carlos Galo <carlosgalo@google.com>
 *
 */

#include <linux/file.h>

#ifdef CONFIG_MEMFD_ASHMEM_COMPAT
void install_ashmem_compat_fops(struct file *file);
#else
static inline void install_ashmem_compat_fops(struct file *file)
{
}
#endif
#endif /* __LINUX_ASHMEM_COMPAT_H */
