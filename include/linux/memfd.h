/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MEMFD_H
#define __LINUX_MEMFD_H

#include <linux/file.h>

#ifdef CONFIG_MEMFD_CREATE
extern long memfd_fcntl(struct file *file, unsigned int cmd, unsigned int arg);
extern int do_memfd_create(const char *uname, unsigned int flags, bool user);
extern unsigned int *memfd_file_seals_ptr(struct file *file);
extern int memfd_get_seals(struct file *file);
#else
static inline long memfd_fcntl(struct file *f, unsigned int c, unsigned int a)
{
	return -EINVAL;
}
static inline int do_memfd_create(const char *uname, unsigned int flags)
{
	return -EOPNOTSUPP;
}
static inline unsigned int *memfd_file_seals_ptr(struct file *file)
{
	return NULL;
}
static inline int memfd_get_seals(struct file *file)
{
	return -EINVAL;
}
#endif

#endif /* __LINUX_MEMFD_H */
