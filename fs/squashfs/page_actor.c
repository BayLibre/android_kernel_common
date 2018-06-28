/*
 * Copyright (c) 2013
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include "page_actor.h"

static struct kmem_cache *squashfs_page_actor_cachep;

int __init init_page_actor_cache(void)
{
	squashfs_page_actor_cachep = kmem_cache_create("squashfs_page_actor_cache",
		sizeof(struct squashfs_page_actor), 0,
		SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT, NULL);

	return squashfs_page_actor_cachep ? 0 : -ENOMEM;
}

void destroy_page_actor_cache(void)
{
	kmem_cache_destroy(squashfs_page_actor_cachep);
}

struct squashfs_page_actor *squashfs_page_actor_init(struct page **page,
	int pages, int length, void (*release_pages)(struct page **, int, int))
{
	struct squashfs_page_actor *actor = kmem_cache_alloc(squashfs_page_actor_cachep, GFP_NOFS);

	if (actor == NULL)
		return NULL;

	actor->length = length ? : pages * PAGE_SIZE;
	actor->page = page;
	actor->pages = pages;
	actor->next_page = 0;
	actor->pageaddr = NULL;
	actor->release_pages = release_pages;
	return actor;
}

void squashfs_page_actor_free(struct squashfs_page_actor *actor, int error)
{
	if (!actor)
		return;

	if (actor->release_pages)
		actor->release_pages(actor->page, actor->pages, error);
	kmem_cache_free(squashfs_page_actor_cachep, actor);
}

void squashfs_actor_to_buf(struct squashfs_page_actor *actor, void *buf,
	int length)
{
	void *pageaddr;
	int i, avail, pos = 0;

	for (i = 0; i < actor->pages && pos < length; ++i) {
		avail = min_t(int, length - pos, PAGE_SIZE);
		if (actor->page[i]) {
			pageaddr = kmap_atomic(actor->page[i]);
			memcpy(buf + pos, pageaddr, avail);
			kunmap_atomic(pageaddr);
		}
		pos += avail;
	}
}

void squashfs_buf_to_actor(void *buf, struct squashfs_page_actor *actor,
	int length)
{
	void *pageaddr;
	int i, avail, pos = 0;

	for (i = 0; i < actor->pages && pos < length; ++i) {
		avail = min_t(int, length - pos, PAGE_SIZE);
		if (actor->page[i]) {
			pageaddr = kmap_atomic(actor->page[i]);
			memcpy(pageaddr, buf + pos, avail);
			kunmap_atomic(pageaddr);
		}
		pos += avail;
	}
}

void squashfs_bh_to_actor(struct buffer_head **bh, int nr_buffers,
	struct squashfs_page_actor *actor, int offset, int length, int blksz)
{
	void *kaddr = NULL;
	int bytes = 0, pgoff = 0, b = 0, p = 0, i, avail;

	while (bytes < length) {
		if (actor->page[p]) {
			kaddr = kmap_atomic(actor->page[p]);
			while (pgoff < PAGE_SIZE && bytes < length) {
				avail = min_t(int, blksz - offset,
						PAGE_SIZE - pgoff);
				memcpy(kaddr + pgoff, bh[b]->b_data + offset,
				       avail);
				pgoff += avail;
				bytes += avail;
				offset = (offset + avail) % blksz;
				if (!offset) {
					put_bh(bh[b]);
					++b;
				}
			}
			kunmap_atomic(kaddr);
			pgoff = 0;
		} else {
			for (i = 0; i < PAGE_SIZE / blksz; ++i) {
				if (bh[b])
					put_bh(bh[b]);
				++b;
			}
			bytes += PAGE_SIZE;
		}
		++p;
	}
}

void squashfs_bh_to_buf(struct buffer_head **bh, int nr_buffers, void *buf,
	int offset, int length, int blksz)
{
	int i, avail, bytes = 0;

	for (i = 0; i < nr_buffers && bytes < length; ++i) {
		avail = min_t(int, length - bytes, blksz - offset);
		if (bh[i]) {
			memcpy(buf + bytes, bh[i]->b_data + offset, avail);
			put_bh(bh[i]);
		}
		bytes += avail;
		offset = 0;
	}
}

void free_page_array(struct page **page, int nr_pages)
{
	int i;

	for (i = 0; i < nr_pages; ++i)
		__free_page(page[i]);
	kfree(page);
}

struct page **alloc_page_array(int nr_pages, int gfp_mask)
{
	int i;
	struct page **page;

	page = kcalloc(nr_pages, sizeof(struct page *), gfp_mask);
	if (!page)
		return NULL;
	for (i = 0; i < nr_pages; ++i) {
		page[i] = alloc_page(gfp_mask);
		if (!page[i]) {
			free_page_array(page, i);
			return NULL;
		}
	}
	return page;
}
