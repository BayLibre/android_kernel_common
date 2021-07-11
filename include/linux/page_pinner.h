/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGE_PINNER_H
#define __LINUX_PAGE_PINNER_H

#include <linux/jump_label.h>

#ifdef CONFIG_PAGE_PINNER

/*
 * During the page migration, the page will go through below states.
 *
 * PP_FAILURE_DETECT	- when the page migration failure was detected.
 * PP_FAILURE_PUT	- when the put_page opeation is done against on
 * 			  the page migration failure detected.
 * PP_FAILURE_FREE	- when the migration failure detected page is
 * 			  finally freed.
 */
enum pp_failure_state {
	PP_FAILURE_DETECT,
	PP_FAILURE_PUT,
	PP_FAILURE_FREE,
};

extern struct static_key_false page_pinner_inited;
extern struct static_key_true failure_tracking;
extern struct page_ext_operations page_pinner_ops;

extern void __reset_page_pinner(struct page *page, unsigned int order, bool free);
extern void __set_page_pinner(struct page *page, unsigned int order);
extern void __dump_page_pinner(struct page *page);
void __page_pinner_migration_failed(struct page *page, enum pp_failure_state state);
void __page_pinner_mark_migration_failed_pages(struct list_head *page_list);

static inline void reset_page_pinner(struct page *page, unsigned int order)
{
	if (static_branch_unlikely(&page_pinner_inited))
		__reset_page_pinner(page, order, false);
}

static inline void free_page_pinner(struct page *page, unsigned int order)
{
	if (static_branch_unlikely(&page_pinner_inited))
		__reset_page_pinner(page, order, true);
}

static inline void set_page_pinner(struct page *page, unsigned int order)
{
	if (static_branch_unlikely(&page_pinner_inited))
		__set_page_pinner(page, order);
}

static inline void dump_page_pinner(struct page *page)
{
	if (static_branch_unlikely(&page_pinner_inited))
		__dump_page_pinner(page);
}

static inline void page_pinner_put_page(struct page *page)
{
	if (!static_branch_unlikely(&failure_tracking))
		return;

	__page_pinner_migration_failed(page, PP_FAILURE_PUT);
}

static inline void page_pinner_mark_migration_failed_pages(struct list_head *page_list)
{
	if (!static_branch_unlikely(&failure_tracking))
		return;

	__page_pinner_mark_migration_failed_pages(page_list);
}
#else
static inline void reset_page_pinner(struct page *page, unsigned int order)
{
}
static inline void free_page_pinner(struct page *page, unsigned int order)
{
}
static inline void set_page_pinner(struct page *page, unsigned int order)
{
}
static inline void dump_page_pinner(struct page *page)
{
}
static inline void page_pinner_put_page(struct page *page)
{
}
static inline void page_pinner_mark_migration_failed_pages(struct list_head *page_list)
{
}
#endif /* CONFIG_PAGE_PINNER */
#endif /* __LINUX_PAGE_PINNER_H */
