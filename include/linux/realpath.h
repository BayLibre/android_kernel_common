/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_REALPATH_H
#define _LINUX_REALPATH_H

char *tomoyo_get_absolute_path(const struct path *path, char * const buffer,
                                      const int buflen);

#define PR_INFO_FILE(f, fmt,...) \
	do { \
		char *buf = NULL; \
		char *str = NULL; \
		unsigned int buf_len = PAGE_SIZE / 2; \
		buf = kmalloc(buf_len, GFP_NOFS); \
		if (buf) { \
			str = tomoyo_get_absolute_path(&(f->f_path), buf, buf_len - 1); \
			pr_info("pid=%d, file=%s" fmt, current->pid, str, ##__VA_ARGS__); \
			kfree(buf); \
		} \
	} while(false); \


#define PR_INFO_FILE_DEBUG(f, fmt,...) \
	do { \
		if (current->debug) { \
			PR_INFO_FILE(f, fmt, ##__VA_ARGS__); \
		} \
	} while(false); \


#endif /* _LINUX_REALPATH_H */
