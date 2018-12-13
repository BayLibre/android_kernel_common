/*
 * VDSO implementation for AArch64 and for AArch32:
 * AArch64: vDSO implementation contains pages setup and data page update.
 * AArch32: vDSO implementation contains sigreturn and kuser pages setup.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/cache.h>
#include <linux/clocksource.h>
#include <linux/elf.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/timekeeper_internal.h>
#include <linux/vmalloc.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>

#include <asm/cacheflush.h>
#include <asm/signal32.h>
#include <asm/vdso.h>

extern char vdso_start[], vdso_end[];

/* vdso_lookup arch_index */
enum arch_vdso_type {
	ARM64_VDSO = 0,
};

struct __vdso_lookup_t {
	const char *name;
	const char *vdso_code_start;
	const char *vdso_code_end;
	unsigned long vdso_pages;
	/* Data Mapping */
	struct vm_special_mapping *dm;
	/* Code Mapping */
	struct vm_special_mapping *cm;
};

static struct __vdso_lookup_t vdso_lookup[2] __ro_after_init = {
	{
		.name = "vdso",
		.vdso_code_start = vdso_start,
		.vdso_code_end = vdso_end,
	},
};

/*
 * The vDSO data page.
 */
static union {
	struct vdso_data	data;
	u8			page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;
struct vdso_data *vdso_data = &vdso_data_store.data;

static int __vdso_remap(enum arch_vdso_type arch_index,
			const struct vm_special_mapping *sm,
			struct vm_area_struct *new_vma)
{
	unsigned long new_size = new_vma->vm_end - new_vma->vm_start;
	unsigned long vdso_size = vdso_lookup[arch_index].vdso_code_end -
				  vdso_lookup[arch_index].vdso_code_start;

	if (vdso_size != new_size)
		return -EINVAL;

	current->mm->context.vdso = (void *)new_vma->vm_start;

	return 0;
}

static int __vdso_init(enum arch_vdso_type arch_index)
{
	int i;
	struct page **vdso_pagelist;
	unsigned long pfn;

	if (memcmp(vdso_lookup[arch_index].vdso_code_start, "\177ELF", 4)) {
		pr_err("vDSO is not a valid ELF object!\n");
		return -EINVAL;
	}

	vdso_lookup[arch_index].vdso_pages = (
			vdso_lookup[arch_index].vdso_code_end -
			vdso_lookup[arch_index].vdso_code_start) >>
			PAGE_SHIFT;
	pr_info("%s: %ld pages (%ld code @ %p, %ld data @ %p)\n",
		vdso_lookup[arch_index].name,
		vdso_lookup[arch_index].vdso_pages + 1,
		vdso_lookup[arch_index].vdso_pages,
		vdso_lookup[arch_index].vdso_code_start, 1L, vdso_data);

	/* Allocate the vDSO pagelist, plus a page for the data. */
	vdso_pagelist = kcalloc(vdso_lookup[arch_index].vdso_pages + 1,
				sizeof(struct page *),
				GFP_KERNEL);
	if (vdso_pagelist == NULL)
		return -ENOMEM;

	/* Grab the vDSO data page. */
	vdso_pagelist[0] = phys_to_page(__pa_symbol(vdso_data));


	/* Grab the vDSO code pages. */
	pfn = sym_to_pfn(vdso_lookup[arch_index].vdso_code_start);

	for (i = 0; i < vdso_lookup[arch_index].vdso_pages; i++)
		vdso_pagelist[i + 1] = pfn_to_page(pfn + i);

	vdso_lookup[arch_index].dm->pages = &vdso_pagelist[0];
	vdso_lookup[arch_index].cm->pages = &vdso_pagelist[1];

	return 0;
}

static int __setup_additional_pages(enum arch_vdso_type arch_index,
				    struct mm_struct *mm,
				    struct linux_binprm *bprm,
				    int uses_interp)
{
	unsigned long vdso_base, vdso_text_len, vdso_mapping_len;
	void *ret;

	vdso_text_len = vdso_lookup[arch_index].vdso_pages << PAGE_SHIFT;
	/* Be sure to map the data page */
	vdso_mapping_len = vdso_text_len + PAGE_SIZE;

	vdso_base = get_unmapped_area(NULL, 0, vdso_mapping_len, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		ret = ERR_PTR(vdso_base);
		goto up_fail;
	}

	ret = _install_special_mapping(mm, vdso_base, PAGE_SIZE,
				       VM_READ|VM_MAYREAD,
				       vdso_lookup[arch_index].dm);
	if (IS_ERR(ret))
		goto up_fail;

	vdso_base += PAGE_SIZE;
	mm->context.vdso = (void *)vdso_base;
	ret = _install_special_mapping(mm, vdso_base, vdso_text_len,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       vdso_lookup[arch_index].cm);
	if (IS_ERR(ret))
		goto up_fail;

	return 0;

up_fail:
	mm->context.vdso = NULL;
	return PTR_ERR(ret);
}

#ifdef CONFIG_COMPAT
/*
 * Create and map the vectors page for AArch32 tasks.
 */
/*
 * aarch32_vdso_pages:
 * 0 - kuser helpers
 * 1 - sigreturn code
 */
static struct page *aarch32_vdso_pages[2] __ro_after_init;
static struct vm_special_mapping aarch32_vdso_spec[2] __ro_after_init = {
	{
		/* Must be named [vectors] for compatibility with arm. */
		.name	= "[vectors]",
		.pages	= &aarch32_vdso_pages[0],
	},
	{
		/* Must be named [sigpage] for compatibility with arm. */
		.name	= "[sigpage]",
		.pages	= &aarch32_vdso_pages[1],
	},
};

#ifdef CONFIG_KUSER_HELPERS
static int aarch32_alloc_kuser_vdso_page(void)
{
	extern char __kuser_helper_start[], __kuser_helper_end[];
	int kuser_sz = __kuser_helper_end - __kuser_helper_start;
	unsigned long vdso_page;

	vdso_page = get_zeroed_page(GFP_ATOMIC);
	if (!vdso_page)
		return -ENOMEM;

	/* kuser helpers */
	memcpy((void *)(vdso_page + 0x1000 - kuser_sz),
	       __kuser_helper_start,
	       kuser_sz);

	flush_icache_range(vdso_page, vdso_page + PAGE_SIZE);

	aarch32_vdso_pages[0] = virt_to_page(vdso_page);

	return 0;
}
#else
static int aarch32_alloc_kuser_vdso_page(void)
{
	return 0;
}
#endif /* CONFIG_KUSER_HELPER */

static int aarch32_alloc_sigreturn_vdso_page(void)
{
	extern char __aarch32_sigret_code_start[], __aarch32_sigret_code_end[];
	int sigret_sz = __aarch32_sigret_code_end - __aarch32_sigret_code_start;
	unsigned long vdso_page;

	vdso_page = get_zeroed_page(GFP_ATOMIC);
	if (!vdso_page)
		return -ENOMEM;

	/* sigreturn code */
	memcpy((void *)vdso_page,
	       __aarch32_sigret_code_start,
	       sigret_sz);

	flush_icache_range(vdso_page, vdso_page + PAGE_SIZE);

	aarch32_vdso_pages[1] = virt_to_page(vdso_page);

	return 0;

}

static int __init aarch32_alloc_vdso_pages(void)
{
	return aarch32_alloc_kuser_vdso_page() &
	       aarch32_alloc_sigreturn_vdso_page();
}
arch_initcall(aarch32_alloc_vdso_pages);

#ifdef CONFIG_KUSER_HELPERS
static int aarch32_kuser_helpers_setup(struct mm_struct *mm)
{
	void *ret;

	/* The kuser helpers must be mapped at the ABI-defined high address */
	ret = _install_special_mapping(mm, AARCH32_KUSER_BASE, PAGE_SIZE,
				       VM_READ | VM_EXEC |
				       VM_MAYREAD | VM_MAYEXEC,
				       &aarch32_vdso_spec[0]);

	return PTR_ERR_OR_ZERO(ret);
}
#else
static int aarch32_kuser_helpers_setup(struct mm_struct *mm)
{
	/* kuser helpers not enabled */
	return 0;
}
#endif /* CONFIG_KUSER_HELPERS */

static int aarch32_sigreturn_setup(struct mm_struct *mm)
{
	unsigned long addr;
	void *ret;

	addr = get_unmapped_area(NULL, 0, PAGE_SIZE, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = ERR_PTR(addr);
		goto out;
	}

	ret = _install_special_mapping(mm, addr, PAGE_SIZE,
				       VM_READ | VM_EXEC | VM_MAYREAD |
				       VM_MAYWRITE | VM_MAYEXEC,
				       &aarch32_vdso_spec[1]);
	if (IS_ERR(ret))
		goto out;

	mm->context.vdso = (void *)addr;

out:
	return PTR_ERR_OR_ZERO(ret);
}

int aarch32_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int ret;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

	ret = aarch32_kuser_helpers_setup(mm);
	if (ret)
		goto out;

	ret = aarch32_sigreturn_setup(mm);

out:
	up_write(&mm->mmap_sem);
	return ret;
}
#endif /* CONFIG_COMPAT */

static int vdso_mremap(const struct vm_special_mapping *sm,
		struct vm_area_struct *new_vma)
{
	return __vdso_remap(ARM64_VDSO, sm, new_vma);;
}

static struct vm_special_mapping vdso_spec[2] __ro_after_init = {
	{
		.name	= "[vvar]",
	},
	{
		.name	= "[vdso]",
		.mremap	= vdso_mremap,
	},
};

static int __init vdso_init(void)
{
	vdso_lookup[ARM64_VDSO].dm = &vdso_spec[0];
	vdso_lookup[ARM64_VDSO].cm = &vdso_spec[1];

	return __vdso_init(ARM64_VDSO);
}
arch_initcall(vdso_init);

int arch_setup_additional_pages(struct linux_binprm *bprm,
				int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int ret;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

	ret = __setup_additional_pages(ARM64_VDSO,
				       mm,
				       bprm,
				       uses_interp);

	up_write(&mm->mmap_sem);

	return ret;
}

#define VDSO_PRECISION_MASK	~(0xFF00ULL<<48)

/*
 * Update the vDSO data page to keep in sync with kernel timekeeping.
 */
void update_vsyscall(struct timekeeper *tk)
{
	struct vdso_timestamp *vdso_ts;
	u32 use_syscall = !tk->tkr_mono.clock->archdata.vdso_direct;
	u64 nsec;

	vdso_write_begin(vdso_data);

	vdso_data->use_syscall	= use_syscall;
	/* CLOCK_REALTIME_COARSE */
	vdso_ts			= &vdso_data->basetime[CLOCK_REALTIME_COARSE];
	vdso_ts->sec		= tk->xtime_sec;
	vdso_ts->nsec		= tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift;
	/* CLOCK_MONOTONIC_COARSE */
	vdso_ts			= &vdso_data->basetime[CLOCK_MONOTONIC_COARSE];
	vdso_ts->sec		= tk->xtime_sec + tk->wall_to_monotonic.tv_sec;
	nsec			= tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift;
	nsec			= nsec + tk->wall_to_monotonic.tv_nsec;
	while (nsec >= NSEC_PER_SEC) {
		nsec = nsec - NSEC_PER_SEC;
		vdso_ts->sec++;
	}
	vdso_ts->nsec		= nsec;

	if (!use_syscall) {
		vdso_data->clock_mode	= 0;
		vdso_data->cycle_last	= tk->tkr_mono.cycle_last;
		vdso_data->mask		= VDSO_PRECISION_MASK;
		vdso_data->mult		= tk->tkr_mono.mult;
		vdso_data->shift	= tk->tkr_mono.shift;
		/* CLOCK_REALTIME */
		vdso_ts			= &vdso_data->basetime[CLOCK_REALTIME];
		vdso_ts->sec		= tk->xtime_sec;
		vdso_ts->nsec		= tk->tkr_mono.xtime_nsec;
		/* CLOCK_MONOTONIC */
		vdso_ts			= &vdso_data->basetime[CLOCK_MONOTONIC];
		vdso_ts->sec		= tk->xtime_sec +
						tk->wall_to_monotonic.tv_sec;
		nsec			= tk->tkr_mono.xtime_nsec;
		nsec			= nsec +
					  ((u64)tk->wall_to_monotonic.tv_nsec <<
					   tk->tkr_mono.shift);
		while (nsec >= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift)) {
			nsec = nsec -
				(((u64)NSEC_PER_SEC) << tk->tkr_mono.shift);
			vdso_ts->sec++;
		}
		vdso_ts->nsec		= nsec;
		/* CLOCK_MONOTONIC_RAW */
		vdso_ts			= &vdso_data->basetime[CLOCK_MONOTONIC_RAW];
		vdso_ts->sec		= tk->raw_sec;
		vdso_ts->nsec		= tk->tkr_raw.xtime_nsec;
		/* CLOCK_BOOTTIME */
		vdso_ts			= &vdso_data->basetime[CLOCK_BOOTTIME];
		vdso_ts->sec		= tk->xtime_sec +
						tk->wall_to_monotonic.tv_sec;
		nsec			= tk->tkr_mono.xtime_nsec;
		nsec			= nsec +
					  ((u64)(tk->wall_to_monotonic.tv_nsec +
					   ktime_to_ns(tk->offs_boot)) <<
					   tk->tkr_mono.shift);
		while (nsec >= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift)) {
			nsec = nsec -
				(((u64)NSEC_PER_SEC) << tk->tkr_mono.shift);
			vdso_ts->sec++;
		}
		vdso_ts->nsec		= nsec;
		/* CLOCK_TAI */
		vdso_ts			= &vdso_data->basetime[CLOCK_TAI];
		vdso_ts->sec		= tk->xtime_sec + (s64)tk->tai_offset;
		vdso_ts->nsec		= tk->tkr_mono.xtime_nsec;
	}

	vdso_write_end(vdso_data);
}

void update_vsyscall_tz(void)
{
	vdso_data->tz_minuteswest	= sys_tz.tz_minuteswest;
	vdso_data->tz_dsttime		= sys_tz.tz_dsttime;
}
