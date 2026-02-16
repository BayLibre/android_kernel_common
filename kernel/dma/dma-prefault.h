#ifndef _LINUX_DMA_PREFAULT_H
#define _LINUX_DMA_PREFAULT_H

#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/compiler.h>

static inline void dma_prefault_page_range(struct page *page,
					   size_t offset, size_t size)
{

	size_t start = offset >> PAGE_SHIFT;
	size_t end   = (offset + size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	size_t i;

	for (i = start; i < end; i++) {
		struct page *p = page + i;

		void *vaddr = kmap_local_page(p);

        READ_ONCE(*(volatile char *)vaddr);

        kunmap_local(vaddr);
	}
}

#endif /* _LINUX_DMA_PREFAULT_H */