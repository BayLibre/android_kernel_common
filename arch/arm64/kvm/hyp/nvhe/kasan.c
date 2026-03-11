// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC
 * Author: Mostafa Saleh <smostafa@google.com>
 */

#include <linux/types.h>
#include <nvhe/kasan.h>
#include <nvhe/memory.h>

/*
 * Do nothing for reports, we are going to hit brk anyway, we
 * can use this in the future to have more information.
 */

#define DEFINE_ASAN_REPORT_LOAD(size)				\
void __asan_report_load##size##_noabort(void *addr)		\
{								\
}

#define DEFINE_ASAN_REPORT_STORE(size)				\
void __asan_report_store##size##_noabort(void *addr)		\
{								\
}

DEFINE_ASAN_REPORT_LOAD(1);
DEFINE_ASAN_REPORT_LOAD(2);
DEFINE_ASAN_REPORT_LOAD(4);
DEFINE_ASAN_REPORT_LOAD(8);
DEFINE_ASAN_REPORT_LOAD(16);
DEFINE_ASAN_REPORT_STORE(1);
DEFINE_ASAN_REPORT_STORE(2);
DEFINE_ASAN_REPORT_STORE(4);
DEFINE_ASAN_REPORT_STORE(8);
DEFINE_ASAN_REPORT_STORE(16);

void __asan_report_load_n_noabort(void *addr, ssize_t size)
{
	WARN_ON(1);
}

void __asan_report_store_n_noabort(void *addr, ssize_t size)
{
	WARN_ON(1);
}

void __asan_handle_no_return(void)
{
}

void __asan_register_globals(void *ptr, ssize_t size)
{
}

void __asan_unregister_globals(void *ptr, ssize_t size)
{
}

void *__asan_memcpy(void *dest, const void *src, ssize_t len)
{
	if (WARN_ON(!hyp_alloc_check_range(src, len) || !hyp_alloc_check_range(dest, len)))
	    return NULL;

	return memcpy(dest, src, len);
}

void *__asan_memset(void *addr, int c, ssize_t len)
{
	if (WARN_ON(!hyp_alloc_check_range(addr, len)))
		return NULL;
	return memset(addr, c, len);
}

#define DEFINE_ASAN_LOAD_STORE(size)					\
	void __asan_load##size(void *addr)				\
	{								\
		WARN_ON(!hyp_alloc_check_range(addr, size));		\
	}								\
	__alias(__asan_load##size)					\
	void __asan_load##size##_noabort(void *);			\
	void __asan_store##size(void *addr)				\
	{								\
		WARN_ON(!hyp_alloc_check_range(addr, size));		\
	}								\
	__alias(__asan_store##size)					\
	void __asan_store##size##_noabort(void *);			\

DEFINE_ASAN_LOAD_STORE(1);
DEFINE_ASAN_LOAD_STORE(2);
DEFINE_ASAN_LOAD_STORE(4);
DEFINE_ASAN_LOAD_STORE(8);
DEFINE_ASAN_LOAD_STORE(16);

bool __kasan_check_read(const volatile void *p, unsigned int size)
{
	return hyp_alloc_check_range(p, size);
}

bool __kasan_check_write(const volatile void *p, unsigned int size)
{
	return hyp_alloc_check_range(p, size);
}

void __asan_loadN(void *addr, ssize_t size)
{
	WARN_ON(!hyp_alloc_check_range(addr, size));
}

void __asan_storeN(void *addr, ssize_t size)
{
	WARN_ON(!hyp_alloc_check_range(addr, size));
}


__alias(__asan_loadN)
void __asan_loadN_noabort(void *, ssize_t);

__alias(__asan_storeN)
void __asan_storeN_noabort(void *, ssize_t);
