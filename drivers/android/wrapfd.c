// SPDX-License-Identifier: GPL-2.0-only
/* wrapfd.c
 *
 * Wrapfd
 *
 * Copyright (C) 2025 Google, Inc.
 */

#include <linux/anon_inodes.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <uapi/linux/wrapfd.h>

#define FDINFO_BUF_SIZE	100

struct wrap_ctx;
struct wrap_content;
static const struct file_operations wrap_fops;

struct wrap_content_operations {
	int (*create_wrap)(struct wrap_content *content, struct wrap_ctx *ctx);
	int (*mmap_prepare)(struct wrap_content *content, struct vm_area_struct *vma);
	vm_fault_t (*fault)(struct wrap_content *content, struct vm_fault *vmf);
	void (*free)(struct wrap_content *content);
	struct wrap_content* (*make_writable)(struct wrap_content* content, bool writable);
	bool (*is_writable)(struct wrap_content* content);
	void (*show_fdinfo)(struct wrap_content *content, char *buf, size_t buf_size);
};

/* Abstract wrap content that should be embedded in a concrete content object. */
struct wrap_content {
	struct wrap_content_operations *ops;
};

/* Folios content */
struct wrap_content_folios {
	struct wrap_content content;
	struct folio **folios;
	size_t nr_pages;
	bool writable;
};

static int folios_content_create_wrap(struct wrap_content *content, struct wrap_ctx *ctx)
{
	return anon_inode_getfd("[wrapfd]", &wrap_fops, ctx, 0);
}

static int folios_content_mmap_prepare(struct wrap_content *content, struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_MAYWRITE) {
		struct wrap_content_folios *folios_content;

		folios_content = container_of(content, struct wrap_content_folios, content);
		if (!folios_content->writable)
			return -EINVAL;
	}

	vm_flags_set(vma, VM_SHARED | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);

	return 0;
}

static vm_fault_t folios_content_fault(struct wrap_content *content, struct vm_fault *vmf)
{
	struct wrap_content_folios *folios_content;
	struct folio *folio;
	size_t i, page_offs;

	folios_content = container_of(content, struct wrap_content_folios, content);

	if (vmf->pgoff >= folios_content->nr_pages)
		return VM_FAULT_SIGBUS;

	/* Find out the page number within the folio */
	page_offs = 0;
	i = vmf->pgoff;
	folio = folios_content->folios[i];
	while (i > 0) {
		i--;
		if (folio != folios_content->folios[i])
			break;
		page_offs++;
	}

	return vmf_insert_pfn(vmf->vma, vmf->address,
			      page_to_pfn(folio_page(folio, page_offs)));
}

static void folios_content_free(struct wrap_content *content)
{
	struct wrap_content_folios *folios_content;

	folios_content = container_of(content, struct wrap_content_folios, content);
	if (folios_content->folios) {
		for (size_t i = 0; i < folios_content->nr_pages; ) {
			struct folio *folio = folios_content->folios[i];

			i += folio_nr_pages(folio);
			folio_put(folio);
		}
		kfree(folios_content->folios);
	}
	kfree(folios_content);
}

static struct wrap_content* folios_content_make_writable(struct wrap_content* content, bool writable)
{
	struct wrap_content_folios *folios_content;

	folios_content = container_of(content, struct wrap_content_folios, content);
	folios_content->writable = writable;

	return content;
}

static bool folios_content_is_writable(struct wrap_content* content)
{
	struct wrap_content_folios *folios_content;

	folios_content = container_of(content, struct wrap_content_folios, content);

	return folios_content->writable;
}


static void folios_content_show_fdinfo(struct wrap_content *content,
				       char *buf, size_t buf_size)
{
	struct wrap_content_folios *folios_content;

	folios_content = container_of(content, struct wrap_content_folios, content);
	sprintf(buf, "type:\tanon\nsize:\t%lu",
		folios_content->nr_pages << PAGE_SHIFT);
}

static struct wrap_content_operations folios_content_ops = {
	.create_wrap		= folios_content_create_wrap,
	.mmap_prepare		= folios_content_mmap_prepare,
	.fault			= folios_content_fault,
	.make_writable		= folios_content_make_writable,
	.is_writable		= folios_content_is_writable,
	.free			= folios_content_free,
	.show_fdinfo		= folios_content_show_fdinfo,
};

static struct wrap_content *alloc_folios_content(struct file *file)
{
	struct address_space *mapping = file->f_mapping;
	struct wrap_content_folios *folios_content;
	unsigned long pg_count, nr_pages = 0;
	XA_STATE(xas, &mapping->i_pages, 0);
	unsigned long addr, size;
	struct folio **folios;
	struct folio *folio;

	if (mapping->a_ops->free_folio)
		return NULL;

	/* Fault-in and mlock the content of the file. */
	size = i_size_read(file->f_inode);
	addr = vm_mmap(file, 0, size, PROT_READ, MAP_PRIVATE | MAP_LOCKED, 0);
	if (IS_ERR_VALUE(addr))
		return NULL;

	folios_content = kmalloc(sizeof(*folios_content), GFP_KERNEL);
	if (!folios_content) {
		vm_munmap(addr, size);
		return NULL;
	}

	/* Copy file content into folios_content->folios. */
	pg_count = mapping->nrpages;
	folios = kmalloc(sizeof(struct folio*) * pg_count, GFP_KERNEL);
	if (!folios) {
		kfree(folios_content);
		vm_munmap(addr, size);
		return NULL;
	}

	rcu_read_lock();
	xas_for_each(&xas, folio, ULONG_MAX) {
		if (xas_retry(&xas, folio))
			continue;
		if (xa_is_value(folio))
			continue;

		folio_get(folio);
		for (size_t i = 0; i < folio_nr_pages(folio); i++)
			folios[nr_pages++] = folio;

		if (nr_pages >= pg_count)
			break;
	}
	rcu_read_unlock();

	BUG_ON(nr_pages < pg_count);

	folios_content->folios = folios;
	folios_content->nr_pages = nr_pages;
	folios_content->writable = true;
	folios_content->content.ops = &folios_content_ops;

	/* Now that we have folio references we can let go of the file mapping. */
	truncate_inode_pages_range(mapping, 0, ULONG_MAX);
	vm_munmap(addr, size);

	return &folios_content->content;
}

/* Read-only file content */
struct wrap_content_rd_file {
	struct wrap_content content;
	struct file *file;
	unsigned long addr;
	unsigned long size;
};

static int rd_file_content_create_wrap(struct wrap_content *content, struct wrap_ctx *ctx)
{
	struct wrap_content_rd_file *rd_file_content;
	struct file *new_file;
	struct file *file;
	int wrapfd;

	rd_file_content = container_of(content, struct wrap_content_rd_file, content);
	file = rd_file_content->file;

	new_file = alloc_file_clone(file, file->f_flags, &wrap_fops);
	if (IS_ERR(new_file))
		return PTR_ERR(new_file);

	wrapfd = get_unused_fd_flags(file->f_flags);
	if (wrapfd < 0) {
		fput(new_file);
		return wrapfd;
	}

	new_file->private_data = ctx;
	fd_install(wrapfd, new_file);

	return wrapfd;
}

static int rd_file_content_mmap_prepare(struct wrap_content *content, struct vm_area_struct *vma)
{
	struct wrap_content_rd_file *rd_file_content;

	/* Read-only mappings only */
	if (vma->vm_flags & VM_MAYWRITE)
		return -EINVAL;

	rd_file_content = container_of(content, struct wrap_content_rd_file, content);
	vma->vm_file = get_file(rd_file_content->file);

	return 0;
}

static vm_fault_t rd_file_content_fault(struct wrap_content *content, struct vm_fault *vmf)
{
	return filemap_fault(vmf);
}

static void rd_file_content_free(struct wrap_content *content)
{
	struct wrap_content_rd_file *rd_file_content;

	rd_file_content = container_of(content, struct wrap_content_rd_file, content);
	/* If exit_mm() already happened all the areas are already destroyed. */
	if (current->mm)
		vm_munmap(rd_file_content->addr, rd_file_content->size);
	fput(rd_file_content->file);
	kfree(rd_file_content);
}

static struct wrap_content* rd_file_content_make_writable(struct wrap_content* content, bool writable)
{
	struct wrap_content_rd_file *rd_file_content;

	if (!writable)
		return content; /* The content is already read-only. */

	rd_file_content = container_of(content, struct wrap_content_rd_file, content);

	return alloc_folios_content(rd_file_content->file);
}

static bool rd_file_content_is_writable(struct wrap_content* content)
{
	return false;
}

static void rd_file_content_show_fdinfo(struct wrap_content *content,
					char *buf, size_t buf_size)
{
	struct wrap_content_rd_file *rd_file_content;

	rd_file_content = container_of(content, struct wrap_content_rd_file, content);
	sprintf(buf, "type:\tfile\nsrc:\t%ld\nsize:\t%lu",
		rd_file_content->file->f_inode->i_ino, rd_file_content->size);
}

static struct wrap_content_operations rd_file_content_ops = {
	.create_wrap		= rd_file_content_create_wrap,
	.mmap_prepare		= rd_file_content_mmap_prepare,
	.fault			= rd_file_content_fault,
	.free			= rd_file_content_free,
	.make_writable		= rd_file_content_make_writable,
	.is_writable		= rd_file_content_is_writable,
	.show_fdinfo		= rd_file_content_show_fdinfo,
};

static struct wrap_content *alloc_rd_file_content(struct file *file)
{
	struct wrap_content_rd_file *rd_file_content;
	unsigned long addr;
	unsigned long size;

	rd_file_content = kmalloc(sizeof(*rd_file_content), GFP_KERNEL);
	if (!rd_file_content)
		return NULL;

	/* Fault in and mlock the content of the file */
	size = i_size_read(file->f_inode);
	addr = vm_mmap(file, 0, size, PROT_READ, MAP_PRIVATE | MAP_LOCKED, 0);
	if (IS_ERR_VALUE(addr)) {
		kfree(rd_file_content);
		return NULL;
	}

	rd_file_content->content.ops = &rd_file_content_ops;
	rd_file_content->file = get_file(file);
	rd_file_content->addr = addr;
	rd_file_content->size = size;

	return &rd_file_content->content;
}

/* Generic wrapfd */
struct wrap_ctx {
	struct wrap_content *content;
	spinlock_t lock; /* protects all fields below */
	struct task_struct *owner;
	bool allow_guests;
	int map_count;
};

static struct wrap_ctx *create_wrap_ctx(void)
{
	struct wrap_ctx *ctx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	spin_lock_init(&ctx->lock);
	ctx->content = NULL;
	ctx->owner = NULL;
	ctx->map_count = 0;
	ctx->allow_guests = false;

	return ctx;
}

static inline bool is_wrap_owner(struct wrap_ctx *ctx,
				 struct task_struct *task)
{
	assert_spin_locked(&ctx->lock);
	return ctx->owner && ctx->owner->mm == task->mm;
}

static inline int publish_wrap(struct wrap_ctx *ctx,
			       struct wrap_content *content)
{
	ctx->content = content;
	return content->ops->create_wrap(content, ctx);
}

static int wrap_accessible(struct wrap_ctx *ctx, struct task_struct *task,
			   bool check_content)
{
	assert_spin_locked(&ctx->lock);

	if (!is_wrap_owner(ctx, task))
		return -EBUSY;

	if (ctx->map_count > 0)
		return -EINVAL;

	if (check_content && !ctx->content)
		return -ENOENT;

	return 0;
}

static void wrap_vm_close(struct vm_area_struct *area)
{
	struct wrap_ctx *ctx = area->vm_private_data;

	spin_lock(&ctx->lock);
	ctx->map_count--;
	spin_unlock(&ctx->lock);
}

static vm_fault_t wrap_vm_fault(struct vm_fault *vmf)
{
	struct wrap_ctx *ctx = vmf->vma->vm_private_data;

	return ctx->content->ops->fault(ctx->content, vmf);
}

static const struct vm_operations_struct wrap_vm_ops = {
	.close		= wrap_vm_close,
	.fault		= wrap_vm_fault,
};

static int wrap_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct wrap_ctx *ctx = file->private_data;
	int ret = 0;

	spin_lock(&ctx->lock);

	if (!ctx->allow_guests && ctx->owner && !is_wrap_owner(ctx, current)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (!ctx->content) {
		ret = -ENOENT;
		goto unlock;
	}

	ret = ctx->content->ops->mmap_prepare(ctx->content, vma);
	if (!ret) {
		vma->vm_ops = &wrap_vm_ops;
		vma->vm_private_data = ctx;
		ctx->map_count++;
		if (vma->vm_file != file)
			fput(file);
	} else {
		if (vma->vm_file != file)
			fput(vma->vm_file);
	}
unlock:
	spin_unlock(&ctx->lock);

	return ret;
}

static int wrap_release(struct inode *ignored, struct file *file)
{
	struct wrap_ctx *ctx = file->private_data;

	if (ctx->content)
		ctx->content->ops->free(ctx->content);
	kfree(ctx);

	return 0;
}

static int wrap_file_get(struct wrap_ctx *ctx)
{
	int ret = 0;

	spin_lock(&ctx->lock);

	if (is_wrap_owner(ctx, current))
		goto unlock;

	if (ctx->owner) {
		ret = -EBUSY;
		goto unlock;
	}

	if (ctx->map_count > 0) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!ctx->content) {
		ret = -ENOENT;
		goto unlock;
	}

	ctx->owner = current;
unlock:
	spin_unlock(&ctx->lock);

	return ret;
}

static int wrap_file_put(struct wrap_ctx *ctx)
{
	int ret = 0;

	spin_lock(&ctx->lock);

	ret = wrap_accessible(ctx, current, false);
	if (ret)
		goto unlock;

	ctx->owner = NULL;
	ctx->allow_guests = false;
unlock:
	spin_unlock(&ctx->lock);

	return ret;
}

static int wrap_file_rewrap(struct wrap_ctx *ctx, unsigned long arg)
{
	struct wrapfd_rewrap __user *user_wrapfd_rewrap;
	struct wrapfd_rewrap wrapfd_rewrap;
	struct wrap_content *new_content;
	struct wrap_content *content;
	struct wrap_ctx *new_ctx;
	int ret = 0;

	user_wrapfd_rewrap = (struct wrapfd_rewrap __user *)arg;
	if (copy_from_user(&wrapfd_rewrap, user_wrapfd_rewrap, sizeof(wrapfd_rewrap)))
		return -EFAULT;

	spin_lock(&ctx->lock);
	ret = wrap_accessible(ctx, current, true);
	if (!ret) {
		content = ctx->content;
		ctx->content = NULL;
	}
	spin_unlock(&ctx->lock);

	if (ret)
		goto out;

	new_content = content->ops->make_writable(content,
				(wrapfd_rewrap.prot & PROT_WRITE) != 0);
	if (!new_content) {
		ret = -ENOMEM;
		goto restore_content;
	}

	new_ctx = create_wrap_ctx();
	if (!new_ctx) {
		ret = -ENOMEM;
		goto free_new_content;
	}

	ret = publish_wrap(new_ctx, new_content);
	if (ret < 0)
		goto free_new_ctx;

	if (new_content != content)
		content->ops->free(content);

	return ret;

free_new_ctx:
	kfree(new_ctx);
free_new_content:
	if (new_content != content)
		new_content->ops->free(new_content);
restore_content:
	/*
	 * Restore original wrap. We are the owner and the wrap
	 * is empty, so it could not have changed from under us.
	 */
	spin_lock(&ctx->lock);
	ctx->content = content;
	spin_unlock(&ctx->lock);
out:
	return ret;
}


static int wrap_file_empty(struct wrap_ctx *ctx)
{
	struct wrap_content *content;
	int ret = 0;

	spin_lock(&ctx->lock);

	ret = wrap_accessible(ctx, current, true);
	if (ret)
		goto unlock;

	content = ctx->content;
	ctx->content = NULL;
unlock:
	spin_unlock(&ctx->lock);

	if (!ret)
		content->ops->free(content);

	return ret;
}

static int wrap_file_allow_guests(struct wrap_ctx *ctx, bool allow)
{
	int ret = 0;

	spin_lock(&ctx->lock);

	ret = wrap_accessible(ctx, current, true);
	if (ret)
		goto unlock;

	ctx->allow_guests = allow;
unlock:
	spin_unlock(&ctx->lock);

	return ret;
}

static long wrap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct wrap_ctx *ctx = file->private_data;
	long ret;

	switch (cmd) {
	case WRAPFD_DEV_IOC_GET:
		ret = wrap_file_get(ctx);
		break;
	case WRAPFD_DEV_IOC_PUT:
		ret = wrap_file_put(ctx);
		break;
	case WRAPFD_DEV_IOC_REWRAP:
		ret = wrap_file_rewrap(ctx, arg);
		break;
	case WRAPFD_DEV_IOC_EMPTY:
		ret = wrap_file_empty(ctx);
		break;
	case WRAPFD_DEV_IOC_ALLOW_GUESTS:
		ret = wrap_file_allow_guests(ctx, true);
		break;
	case WRAPFD_DEV_IOC_PROHIBIT_GUESTS:
		ret = wrap_file_allow_guests(ctx, false);
		break;
	default:
		return -ENOTTY;
	}

	return ret;
}

#ifdef CONFIG_PROC_FS
static void wrap_show_fdinfo(struct seq_file *m, struct file *file)
{
	struct wrap_ctx *ctx = file->private_data;
	char buf[FDINFO_BUF_SIZE];
	char *pos = buf;

	spin_lock(&ctx->lock);
	if (ctx->owner)
		pos += sprintf(pos, "owner:\t%d\n", ctx->owner->pid);
	else
		pos += sprintf(pos, "owner:\t<none>\n");
	pos += sprintf(pos, "guests:\t%s\n", ctx->allow_guests ? "yes" : "no");
	pos += sprintf(pos, "maps:\t%d\n", ctx->map_count);
	pos += sprintf(pos, "empty:\t%s\n", ctx->content ? "no" : "yes");
	if (ctx->content) {
		struct wrap_content *content = ctx->content;

		pos += sprintf(pos, "rdonly:\t%s\n",
				content->ops->is_writable(content) ? "no" : "yes");
		content->ops->show_fdinfo(content, pos,
					  FDINFO_BUF_SIZE - (pos - buf));
	}
	spin_unlock(&ctx->lock);

	seq_printf(m, "%s\n", buf);
}
#endif

static const struct file_operations wrap_fops = {
	.owner		= THIS_MODULE,
	.mmap		= wrap_mmap,
	.release	= wrap_release,
	.unlocked_ioctl	= wrap_ioctl,
	.compat_ioctl	= wrap_ioctl,
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= wrap_show_fdinfo,
#endif
};

static int wrap_file(struct wrap_ctx *ctx, unsigned long arg)
{
	struct wrapfd_wrap __user *user_wrapfd_wrap;
	struct wrapfd_wrap wrapfd_wrap;
	struct wrap_content *content;
	struct file *file;
	int err, wrapfd;

	user_wrapfd_wrap = (struct wrapfd_wrap __user *)arg;
	if (copy_from_user(&wrapfd_wrap, user_wrapfd_wrap, sizeof(wrapfd_wrap)))
		return -EFAULT;

	file = fget(wrapfd_wrap.fd);
	if (!file)
		return -EBADF;

	/* File should be read-only */
	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		err = -EPERM;
		goto err_put_file;
	}

	if (wrapfd_wrap.prot & PROT_WRITE)
		content = alloc_folios_content(file);
	else
		content = alloc_rd_file_content(file);

	if (!content) {
		err = -ENOMEM;
		goto err_put_file;
	}
	wrapfd = publish_wrap(ctx, content);
	if (wrapfd < 0) {
		ctx->content = NULL;
		err = wrapfd;
		goto err_free_content;
	}
	fput(file);

	return wrapfd;

err_free_content:
	content->ops->free(content);
err_put_file:
	fput(file);

	return err;
}

static int get_wrap_state(unsigned long arg)
{
	struct wrapfd_get_state __user *user_wrapfd_get_state;
	struct wrapfd_get_state wrapfd_get_state;
	struct wrap_ctx *ctx;
	struct file *file;

	user_wrapfd_get_state = (struct wrapfd_get_state __user *)arg;
	if (copy_from_user(&wrapfd_get_state, user_wrapfd_get_state,
			   sizeof(wrapfd_get_state)))
		return -EFAULT;

	file = fget(wrapfd_get_state.fd);
	if (!file)
		return -EBADF;

	if (file->f_op != &wrap_fops) {
		fput(file);
		return -EINVAL;
	}

	ctx = file->private_data;
	if (ctx->content) {
		if (ctx->content->ops->is_writable(ctx->content))
			wrapfd_get_state.state = WRAPFD_CONTENT_RDWR;
		else
			wrapfd_get_state.state = WRAPFD_CONTENT_RDONLY;
	} else {
		wrapfd_get_state.state = WRAPFD_CONTENT_EMPTY;
	}

	fput(file);

	if (copy_to_user(user_wrapfd_get_state, &wrapfd_get_state,
			 sizeof(wrapfd_get_state)))
		return -EFAULT;

	return 0;
}

static long wrapfd_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct wrap_ctx *ctx;
	int ret;

	VM_WARN_ON_ONCE(!current->mm);

	switch (cmd) {
	case WRAPFD_DEV_IOC_WRAP:
		ctx = create_wrap_ctx();
		if (!ctx)
			return -ENOMEM;

		ret = wrap_file(ctx, arg);
		if (ret < 0)
			kfree(ctx);

		break;
	case WRAPFD_DEV_IOC_GET_STATE:
		ret = get_wrap_state(arg);
		break;
	default:
		return -ENOTTY;
	}

	return ret;
}

static const struct file_operations wrapfd_dev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = wrapfd_dev_ioctl,
	.compat_ioctl = wrapfd_dev_ioctl,
	.llseek = noop_llseek,
};

static struct miscdevice wrapfd_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "wrapfd",
	.fops = &wrapfd_dev_fops,
};

static int __init wrapfd_init(void)
{
	int ret;

	ret = misc_register(&wrapfd_misc);
	if (ret) {
		pr_err("failed to register misc device!\n");
		return ret;
	}

	pr_info("wrapfd initialized\n");

	return 0;
}
device_initcall(wrapfd_init);

