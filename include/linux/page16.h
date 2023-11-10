/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGE16_H
#define __LINUX_PAGE16_H

/*
 * include/linux/page16.h
 *
 * Written by kaleshsingh
 *
 * Helper macros for x86 16K page size emulation.
 *
 * The macors for use with the emulated page size are all
 * namespaced by the prefix '__'.
 */

#include <linux/align.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>

#include <asm/page_types.h>

#ifdef  CONFIG_DEBUG_16K
#define LOG_16K(fmt, ...) \
	pr_debug("DEBUG 16K: [%i]: " fmt, task_pid_nr(current), ## __VA_ARGS__)

#define LOG_16K_IF(condition, fmt, ...) \
    do {                                \
        if (condition)                  \
            pr_debug("DEBUG 16K: [%i]: " fmt, task_pid_nr(current), ## __VA_ARGS__); \
    } while(0)

#else   /* !CONFIG_DEBUG_16K */
#define LOG_16K(fmt, ...) do {} while(0)
#define LOG_16K_IF(condition, fmt, ...) do {} while(0)
#endif  /* CONFIG_DEBUG_16K */

#ifdef CONFIG_EMULATE_16K_PAGE_SIZE
#define __PAGE_SHIFT		14
#else   /* !CONFIG_EMULATE_16K_PAGE_SIZE */
#define __PAGE_SHIFT		PAGE_SHIFT
#endif  /* CONFIG_EMULATE_16K_PAGE_SIZE */

#define __PAGE_SIZE		    (_AC(1,UL) << __PAGE_SHIFT)
#define __PAGE_MASK		    (~(__PAGE_SIZE-1))

#define __PAGE_ALIGN(addr)      ALIGN(addr, __PAGE_SIZE)
#define __PAGE_ALIGN_DOWN(addr) ALIGN_DOWN(addr, __PAGE_SIZE)
#define __PAGE_ALIGNED(addr)	IS_ALIGNED((unsigned long)(addr), __PAGE_SIZE)

#define __offset_in_page(p)	((unsigned long)(p) & ~__PAGE_MASK)

#define __VM_SPECIAL	0x00000800	/* VMA is exempt from emulated page align requirements */
#define __MAP_SPECIAL   0x8000		/* VMA is exempt from emulated page align requirements */

#define __MMAP_RND_BITS(x)      (x - (__PAGE_SHIFT - PAGE_SHIFT))

#ifdef CONFIG_EMULATE_16K_PAGE_SIZE
/*
 * Combine the mmap "flags" argument into "vm_flags" add translation
 * of the special flag.
 */
static inline unsigned long
__calc_vm_flag_bits(unsigned long flags)
{
    return calc_vm_flag_bits(flags) |
           _calc_vm_trans(flags, __MAP_SPECIAL,  __VM_SPECIAL );
}

/*
 * Updates len to avoid mapping off the end of the file.
 *
 * The length of the original mapping must be updated before
 * it's VMA is created to avoid an unaligned munmap in the
 * MAP_FIXED fixup mapping.
 */
static inline void __filemap_len(struct inode *inode,
				 unsigned long pgoff, unsigned long *len,
				 bool is_16k)
{
	unsigned long file_size = (unsigned long) i_size_read(inode);
    /*
     * Round up, so that this is a count (not an index). This simplifies
     * the following calculations.
     */
	pgoff_t max_idx = DIV_ROUND_UP(file_size, PAGE_SIZE);
	pgoff_t index = pgoff + (*len >> PAGE_SHIFT);
	unsigned long new_len = 0;

	if (!is_16k)
		return;

	if (unlikely(index >= max_idx)) {
		new_len = (max_idx - pgoff)  << PAGE_SHIFT;
		/* Careful of overflows in special files */
		if (new_len > 0 && new_len < *len)
			*len = new_len;
	}
}

/* Avoid include circular dependencies */
/*
unsigned long do_mmap(struct file *file, unsigned long addr,
			unsigned long len, unsigned long prot,
			unsigned long flags, unsigned long pgoff,
			unsigned long *populate, struct list_head *uf);
*/


/* Avoid circular dependencies from mm_inline.h */
extern struct anon_vma_name *anon_vma_name_alloc(const char *name);

/*
 * This is called to fill any holes created by __filemap_len()
 * with an anonymous mapping.
 */
static inline void __filemap_fixup(unsigned long addr, unsigned long prot,
				   unsigned long old_len, unsigned long new_len)
{
	unsigned long anon_len = old_len - new_len;
	unsigned long anon_addr = addr + new_len;
	struct mm_struct *mm = current->mm;
	unsigned long populate = 0;
	struct vm_area_struct *vma;

	if (!anon_len)
		return;

	BUG_ON(new_len > old_len);

	/* Not a filemap fault */
	if (IS_ERR_VALUE(addr))
		return;

	vma = find_vma(mm, addr);

	/*
	 * This should never happen, VMA was inserted and we still
	 * haven't released the write lock.
	 */
	BUG_ON(!vma);

	/* Only handle fixups for filemap faults */
	if (vma->vm_ops && vma->vm_ops->fault != filemap_fault)
		return;

	/*
	 * Override the the end of the file mapping that is off the file
	 * with an anonymous mapping.
	 */
	anon_addr = do_mmap(NULL, anon_addr, anon_len, prot,
					MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED|__MAP_SPECIAL,
					0, &populate, NULL);

	if (!IS_ERR_VALUE(anon_addr)) {
		struct anon_vma_name *anon_name = anon_vma_name_alloc("filemap_fixup");

		if (!anon_name)
			return;

		/* Label the fixup VMA */
		madvise_set_anon_name(mm, anon_addr, anon_len, anon_name);
	}
}
#else   /* !CONFIG_EMULATE_16K_PAGE_SIZE */
static inline unsigned long
__calc_vm_flag_bits(unsigned long flags)
{
    return calc_vm_flag_bits(flags);
}

static inline void __filemap_len(struct inode *inode,
				 unsigned long pgoff, unsigned long *len,
				 bool is_16k) { }

static inline void __filemap_fixup(unsigned long addr, unsigned long prot,
				   unsigned long old_len, unsigned long new_len) { }
#endif  /* CONFIG_EMULATE_16K_PAGE_SIZE */

#endif /* __LINUX_PAGE16_H */
