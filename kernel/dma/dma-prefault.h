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

static inline void dma_prefault_range(const void *addr, size_t size)
{
	unsigned long start = ALIGN_DOWN((unsigned long)addr, PAGE_SIZE);
	unsigned long end   = ALIGN_DOWN((unsigned long)addr + size - 1, PAGE_SIZE);

	for (unsigned long a = start; a <= end; a += PAGE_SIZE) {
		if (is_vmalloc_addr((void *)a)) {
			struct page *p = vmalloc_to_page((void *)a);
			if (!p)
				continue;

			void *vaddr = kmap_local_page(p);

			READ_ONCE(*(volatile char *)vaddr);

			kunmap_local(vaddr);
		} else if (virt_addr_valid((void *)a)) {
			unsigned long vaddr = a;

			READ_ONCE(*(volatile char *)vaddr);
		} else {
			continue;
		}
	}
}

#endif /* _LINUX_DMA_PREFAULT_H */