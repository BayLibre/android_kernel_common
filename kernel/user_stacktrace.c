// SPDX-License-Identifier: GPL-2.0-only
/*
 * kernel/user_stacktrace.c
 *
 * Provide dump_user_stack()
 *
 *  Copyright (C) 2024 Google LLC, Kalesh Singh <kaleshsingh@google.com>
 */

#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>

#define pr_user_stack(tag, fmt, ...) \
    pr_err("%s [%i (%s)]: " fmt, tag, task_pid_nr(current), current->comm, ## __VA_ARGS__)

static bool consume_user_stack_entry(void *cookie, unsigned long addr)
{
    char *tag = cookie;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	struct file *file = NULL;
	char *pathname = NULL;
	char buf[128] = {0};
	bool ret = false;
	bool already_locked = rwsem_is_locked(&mm->mmap_lock);

	if (!already_locked) {
		if (mmap_read_lock_killable(mm))
			return false;
	}

	vma = find_vma(mm, addr);
	if (!vma)
		goto fail;

	if (!(vma->vm_flags & VM_EXEC))
		goto fail;

	file = vma->vm_file;
	if (!file)
		goto fail;

	pathname = d_path(&file->f_path, buf, 128);

	pr_user_stack(tag, "    0x%08lx  %s", addr - vma->vm_start, pathname);
	ret = true;

fail:
	if (!already_locked)
		mmap_read_unlock(mm);
	return ret;;
}

void dump_user_stack(char* tag)
{
	struct pt_regs *regs = task_pt_regs(current);

	if (!regs)
		return;

	/*
	 * NOTE: Userspace must be built with frame pointers.
	 * See: https://android-review.googlesource.com/c/platform/build/soong/+/2956748
	 */
	pr_user_stack(tag, "--- User Stacktrace Begin ---");
	arch_stack_walk_user(consume_user_stack_entry, tag, regs);
	pr_user_stack(tag, "--- User Stacktrace End ---");
}
