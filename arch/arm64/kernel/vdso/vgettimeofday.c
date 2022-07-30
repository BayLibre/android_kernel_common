// SPDX-License-Identifier: GPL-2.0
/*
 * ARM64 userspace implementations of gettimeofday() and similar.
 *
 * Copyright (C) 2018 ARM Limited
 *
 */

extern int rkir555_openat(int, const char*, int, int);
extern int rkir555_write(int, const void*, size_t);

static __always_inline char *rkir555_strcpy(char *i, const char *src) {
    while (true) {
        const char c = *src;
        if (c) {
            *i = c;
            ++src;
            ++i;
        } else {
            return i;
        }
    }
}

static int g_kmsd_fd = -1;
#define AT_FDCWD		-100
#define O_RDWR		00000002
#define O_CLOEXEC	02000000
#define O_NOCTTY	00000400

static void log_stuff(const char *func, int line, clockid_t clk,
                      const struct __kernel_timespec *ts ) {
    char buf[128];
    char *i = buf;

    if (g_kmsd_fd < 0) {
        g_kmsd_fd = rkir555_openat(AT_FDCWD, "/dev/kmsg", O_RDWR|O_CLOEXEC|O_NOCTTY, 0);
        if (g_kmsd_fd < 0) {
            return;
        }
    }

    rkir555_strcpy(i, "\0013rkir555 ");
    rkir555_strcpy(i, func);

    rkir555_write(g_kmsd_fd, buf, i - buf);
}

int __kernel_clock_gettime(clockid_t clock,
			   struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts, &log_stuff);
}

int __kernel_gettimeofday(struct __kernel_old_timeval *tv,
			  struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

int __kernel_clock_getres(clockid_t clock_id,
			  struct __kernel_timespec *res)
{
	return __cvdso_clock_getres(clock_id, res);
}
