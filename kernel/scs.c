/*
 * Shadow Call Stack support.
 *
 * Copyright (C) 2018 Google LLC
 */

#include <linux/cpuhotplug.h>
#include <linux/memcontrol.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/scs.h>
#include <linux/vmalloc.h>
#include <linux/vmstat.h>
#include <asm/page.h>

#define SCS_END_MAGIC	0xaf0194819b1635f6UL

static inline unsigned long scs_page(struct task_struct *tsk)
{
	return task_thread_info(tsk)->shadow_call_stack_page;
}

static inline void scs_set_page(struct task_struct *tsk, unsigned long s)
{
	task_thread_info(tsk)->shadow_call_stack_page = s;
}

#ifdef CONFIG_VMAP_STACK

/* Keep a cache of shadow stacks */
#define SCS_CACHE_SIZE 2
static DEFINE_PER_CPU(unsigned long, scs_cache[SCS_CACHE_SIZE]);

static unsigned long scs_alloc(int node)
{
	int i;

	for (i = 0; i < SCS_CACHE_SIZE; i++) {
		unsigned long s;

		s = this_cpu_xchg(scs_cache[i], 0);
		if (s)
			return s;
	}

	return (unsigned long)__vmalloc_node_range(PAGE_SIZE, PAGE_SIZE,
				     VMALLOC_START, VMALLOC_END,
				     THREADINFO_GFP, PAGE_KERNEL, 0,
				     node, __builtin_return_address(0));
}

static void scs_free(unsigned long s)
{
	int i;

	for (i = 0; i < SCS_CACHE_SIZE; i++) {
		if (this_cpu_cmpxchg(scs_cache[i], 0, s) != 0)
			continue;

		return;
	}

	vfree_atomic((const void *)s);
}

static struct page* __scs_page(struct task_struct *tsk)
{
	return vmalloc_to_page(scs_page(tsk));
}

static int scs_cleanup(unsigned int cpu)
{
	int i;
	unsigned long *cache = per_cpu_ptr(scs_cache, cpu);

	for (i = 0; i < SCS_CACHE_SIZE; i++) {
		vfree((const void *)cache[i]);
		cache[i] = 0;
	}

	return 0;
}

void __init scs_init(void)
{
	cpuhp_setup_state(CPUHP_BP_PREPARE_DYN, "scs:scs_cleanup", NULL,
		scs_cleanup);
}

#else /* !CONFIG_VMAP_STACK */

static inline unsigned long scs_alloc(int node)
{
	return __get_free_page(GFP_KERNEL);
}

static inline void scs_free(unsigned long s)
{
	free_page(s);
}

static struct page* __scs_page(struct task_struct *tsk)
{
	return virt_to_page(scs_page(tsk));
}

void __init scs_init(void)
{
}

#endif /* CONFIG_VMAP_STACK */

static inline unsigned long* scs_magic(struct task_struct *tsk)
{
	return (unsigned long *)(scs_page(tsk) +
				 PAGE_SIZE - sizeof(unsigned long));
}

static inline void scs_set_magic(struct task_struct *tsk)
{
	*scs_magic(tsk) = SCS_END_MAGIC;
}

void scs_task_init(struct task_struct *tsk)
{
	task_set_scs(tsk, 0);
	scs_set_page(tsk, 0);
}

extern struct task_struct init_task;

void scs_set_init_magic(void)
{
	scs_set_page(&init_task, (unsigned long)init_shadow_call_stack);
	scs_set_magic(&init_task);
}

static void scs_account(struct task_struct *tsk, int account)
{
	struct page *page = __scs_page(tsk);

	account *= PAGE_SIZE / 1024;

	mod_zone_page_state(page_zone(page), NR_KERNEL_SCS_KB, account);
	mod_memcg_page_state(page, MEMCG_KERNEL_SCS_KB, account);
}

int scs_prepare(struct task_struct *tsk, int node)
{
	unsigned long s;

	BUG_ON(scs_page(tsk));

	s = scs_alloc(node);
	if (!s)
		return -ENOMEM;

	task_set_scs(tsk, s);
	scs_set_page(tsk, s);
	scs_set_magic(tsk);
	scs_account(tsk, 1);

	return 0;
}

void scs_release(struct task_struct *tsk)
{
	unsigned long s;

	s = scs_page(tsk);
	if (!s)
		return;

	BUG_ON(*scs_magic(tsk) != SCS_END_MAGIC);

	scs_account(tsk, -1);
	scs_task_init(tsk);
	scs_free(s);
}
