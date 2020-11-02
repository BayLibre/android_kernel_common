// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-buf.h>
#include <linux/mm_types.h>
#include <linux/ptrace.h>
#include <linux/sched/mm.h>
#include <linux/seq_file.h>

#include "internal.h"

// ---------------------- /proc/<pid>/dma_buf_maps --------------------
struct dma_buf_maps_private {
	struct inode *inode;
	struct mm_struct *mm;
	struct file *dma_buf_file;
};

/* mmap_lock must be held before calling this function */
static loff_t *next_mapped_dma_buf(struct dma_buf_maps_private *priv,
		loff_t *pos)
{
	struct vm_area_struct *vma = find_vma(priv->mm, (unsigned long) *pos);

	for (; vma; vma = vma->vm_next)
		if (vma->vm_file && is_dma_buf_file(vma->vm_file)) {
			priv->dma_buf_file = vma->vm_file;
			*pos = vma->vm_end - 1;
			break;
		}

	if (!vma)
		pos = NULL;

	return pos;
};


static void *dma_buf_maps_seq_start(struct seq_file *s, loff_t *pos)
{
	struct dma_buf_maps_private *priv = s->private;
	struct task_struct *task = get_proc_task(priv->inode);

	if (!task)
		return ERR_PTR(-ESRCH);

	priv->mm = get_task_mm(task);
	put_task_struct(task);

	if (!priv->mm)
		return NULL;

	if (mmap_read_lock_killable(priv->mm)) {
		mmput(priv->mm);
		priv->mm = NULL;
		return ERR_PTR(-EINTR);
	}

	return next_mapped_dma_buf(priv, pos);
}

static void *dma_buf_maps_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	++*pos;
	return next_mapped_dma_buf(s->private, pos);
}

static void dma_buf_maps_seq_stop(struct seq_file *s, void *v)
{
	struct dma_buf_maps_private *priv = s->private;

	if (priv->mm) {
		mmap_read_unlock(priv->mm);
		mmput(priv->mm);
	}
}

static int dma_buf_maps_seq_show(struct seq_file *s, void *v)
{
	struct dma_buf_maps_private *priv = s->private;
	struct file *file = priv->dma_buf_file;
	struct dma_buf *dma_buf = file->private_data;

	if (!dma_buf)
		return -ESRCH;

	seq_printf(s, "%8lu\t%8ld\t%#8x\t%#8x\t%8ld\t%-8s\n",
		   file_inode(file)->i_ino, dma_buf->size / SZ_1K,
		   file->f_flags, file->f_mode, file_count(file),
		   dma_buf->exp_name ? dma_buf->exp_name : "");

	return 0;

}

static const struct seq_operations proc_pid_dma_buf_maps_seq_ops = {
	.start = dma_buf_maps_seq_start,
	.next  = dma_buf_maps_seq_next,
	.stop  = dma_buf_maps_seq_stop,
	.show  = dma_buf_maps_seq_show
};

static int proc_dma_buf_maps_open(struct inode *inode, struct file *file,
		     const struct seq_operations *ops)
{
	struct dma_buf_maps_private *priv;
	struct task_struct *task;
	bool allowed = false;

	task = get_proc_task(inode);
	if (!task)
		return -ESRCH;

	allowed = ptrace_may_access(task, PTRACE_MODE_READ_FSCREDS);
	put_task_struct(task);

	if (!allowed)
		return -EACCES;

	priv = __seq_open_private(file, ops, sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	priv->inode = inode;
	priv->mm = NULL;
	priv->dma_buf_file = NULL;

	return 0;
}

static int proc_dma_buf_maps_release(struct inode *inode, struct file *file)
{
	return seq_release_private(inode, file);
}


static int pid_dma_buf_maps_open(struct inode *inode, struct file *file)
{
	return proc_dma_buf_maps_open(inode, file,
			&proc_pid_dma_buf_maps_seq_ops);
}

const struct file_operations proc_pid_dma_buf_maps_operations = {
	.open		= pid_dma_buf_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_dma_buf_maps_release,
};

