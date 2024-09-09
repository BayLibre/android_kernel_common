// SPDX-License-Identifier: GPL-2.0

/*
 * Ashmem compatability for memfd
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Carlos Galo <carlosgalo@google.com>
 */

#include <linux/bitfield.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/memfd.h>
#include <linux/mm_types.h>
#include <linux/mman.h>
#include <linux/shmem_fs.h>
#include <linux/spinlock.h>

#include "../drivers/staging/android/ashmem.h"
#include "ashmem_compat.h"

#define MFD_NAME_PREFIX "memfd:"
#define ASHMEM_COMPAT_PROT GENMASK(2, 0)
#define ASHMEM_COMPAT_MAPPED BIT(3)

static DEFINE_SPINLOCK(fops_lock);
static struct file_operations ashmem_compat_fops;
static int (*shmem_mmap)(struct file *, struct vm_area_struct *);

static unsigned long get_ashmem_compat_mask(struct file *file)
{
	return (unsigned long)file->private_data;
}

static void set_ashmem_compat_mask(struct file *file, unsigned long mask)
{
	file->private_data = (void *)mask;
}

static bool is_buffer_mapped(struct file *file)
{
	return get_ashmem_compat_mask(file) & ASHMEM_COMPAT_MAPPED;
}

static int ashmem_compat_set_name(struct file *file, void __user *name)
{
	struct inode *inode = file_inode(file);
	char local_name[ASHMEM_NAME_LEN];
	char *final_name;
	long len;
	int ret = 0;
	size_t buf_len;

	inode_lock(inode);

	if (is_buffer_mapped(file)) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * strncpy_from_user() either returns the amount of bytes copied, but not including the NUL
	 * terminating byte or ASHMEM_NAME_LEN if the string was too big.
	 */
	len = strncpy_from_user(local_name, name, ASHMEM_NAME_LEN);
	if (len <= 0) {
		ret = -EFAULT;
		goto out;
	} else if (len == ASHMEM_NAME_LEN) {
		/* Truncate the name to match the legacy behavior. */
		local_name[--len] = '\0';
	}

	buf_len = strlen(MFD_NAME_PREFIX) + len + 1;
	final_name = kmalloc(buf_len, GFP_KERNEL);
	if (!final_name) {
		ret = -ENOMEM;
		goto out;
	}
	scnprintf(final_name, buf_len, "%s%s", MFD_NAME_PREFIX, local_name);

	kfree(file->f_path.dentry->d_fsdata);
	file->f_path.dentry->d_fsdata = final_name;
out:
	inode_unlock(inode);
	return ret;
}

static int ashmem_compat_get_name(struct file *file, void __user *name)
{
	struct inode *inode = file_inode(file);
	char *filename;
	int ret;

	inode_lock_shared(inode);

	if (file->f_path.dentry->d_fsdata)
		filename = file->f_path.dentry->d_fsdata;
	else
		filename = (char *)file->f_path.dentry->d_name.name;
	/*
	 * memfd files have names of the following format: "memfd:name", where name
	 * is the name argument that is provided to memfd_create() or with ASHMEM_SET_NAME.
	 *
	 * To retain compatibility with ashmem's behavior, just return "name" instead of
	 * "memfd:name".
	 */
	filename += strlen(MFD_NAME_PREFIX);

	/*
	 * name is expected to be ASHMEM_NAME_LEN in size. If ASHMEM_SET_NAME was used, then the
	 * portion of the string containing the name should fit in the buffer.
	 *
	 * If ASHMEM_SET_NAME was not used, then the portion of the string containing the name
	 * will be at most MFD_NAME_MAX_LEN, which is less than ASHMEM_NAME_LEN, so that should
	 * still fit in the buffer.
	 *
	 * Add 1 to account for the NUL terminating byte.
	 */
	ret = copy_to_user(name, filename, strlen(filename) + 1) ? -EFAULT : 0;

	inode_unlock_shared(inode);
	return ret;
}

static int ashmem_compat_set_size(struct file* file, size_t size)
{
	struct inode *inode = file_inode(file);
	unsigned int *file_seals_ptr;
	unsigned int file_seals;
	int ret = 0;

	inode_lock(inode);

	if (is_buffer_mapped(file)) {
		ret = -EINVAL;
		goto out;
	}

	file_seals_ptr = memfd_file_seals_ptr(file);
	file_seals = *file_seals_ptr;

	if (file_seals & (F_SEAL_GROW | F_SEAL_SHRINK)) {
		ret = -EINVAL;
		goto out;
	}

	i_size_write(inode, size);
	/*
	 * While ashmem's legacy behavior was to allow the buffer size to be changed until the
	 * first time it was mapped, emulating that behavior would mean doing that for all memfds,
	 * (i.e. mmap implies file seals for the size). Overloading mmap as such may not be ideal
	 * for all memfds, so we seal the file size here.
	 */
	*file_seals_ptr |= F_SEAL_GROW | F_SEAL_SHRINK;

out:
	inode_unlock(inode);

	return ret;
}

static u64 ashmem_compat_get_size(struct file *file)
{
	struct inode *inode = file_inode(file);
	u64 size;

	inode_lock_shared(inode);
	size = i_size_read(inode);
	inode_unlock_shared(inode);
	return size;
}

/* Assumes that the file's inode lock is held */
static unsigned long __ashmem_compat_get_prot_mask(struct file *file)
{
	unsigned int file_seals = memfd_fcntl(file, F_GET_SEALS, 0);
	unsigned long prot_mask;

	 /* Clear PROT_WRITE so that we can use the file seals to determine its state. */
	prot_mask = FIELD_GET(ASHMEM_COMPAT_PROT, get_ashmem_compat_mask(file)) & ~PROT_WRITE;
	if (!(file_seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE)))
		prot_mask |= PROT_WRITE;

	return prot_mask;
}

static int ashmem_compat_set_prot_mask(struct file *file, unsigned long prot)
{
	struct inode *inode = file_inode(file);
	unsigned int *file_seals_ptr;
	unsigned long ashmem_compat_mask, current_prot_mask;
	int ret = 0;

	inode_lock(inode);

	/* Retain behavior of only allowing protections to be removed. */
	current_prot_mask = __ashmem_compat_get_prot_mask(file);
	if ((current_prot_mask & prot) != prot) {
		ret = -EINVAL;
		goto out;
	}

	if (current->personality & READ_IMPLIES_EXEC)
		prot |= PROT_EXEC;

	/*
	 * Ashmem buffers by default start with PROT_READ | PROT_WRITE | PROT_EXEC permissions.
	 * This ioctl command was used to remove permissions from the buffer's permission set.
	 *
	 * Removing PROT_READ and PROT_EXEC:
	 *
	 * Given that memfd buffers are always readable and executable, don't allow for read or exec
	 * permissions to be removed.
	 *
	 * Removing PROT_WRITE:
	 *
	 * We can prevent any other mappings from having write permissions by adding the
	 * F_SEAL_WRITE mapping. However, that would conflict with known usecases where it is
	 * desirable to maintain an existing writable mapping, but forbid future writable mappings.
	 *
	 * To support that usecase, we use F_SEAL_FUTURE_WRITE.
	 */
	file_seals_ptr = memfd_file_seals_ptr(file);

	if (!(prot & PROT_WRITE))
		*file_seals_ptr |= F_SEAL_FUTURE_WRITE;

	ashmem_compat_mask = get_ashmem_compat_mask(file);
	set_ashmem_compat_mask(file, ashmem_compat_mask | FIELD_PREP(ASHMEM_COMPAT_PROT, prot));
out:
	inode_unlock(inode);
	return ret;
}

static unsigned long ashmem_compat_get_prot_mask(struct file *file)
{
	struct inode *inode = file_inode(file);
	unsigned long prot_mask;

	inode_lock_shared(inode);
	prot_mask = __ashmem_compat_get_prot_mask(file);
	inode_unlock_shared(inode);
	return prot_mask;
}

static long ashmem_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOTTY;
	unsigned long inode_nr;

	switch (cmd) {
	case ASHMEM_SET_NAME:
		ret = ashmem_compat_set_name(file, (void __user *)arg);
		break;
	case ASHMEM_GET_NAME:
		ret = ashmem_compat_get_name(file, (void __user *)arg);
		break;
	case ASHMEM_SET_SIZE:
		ret = ashmem_compat_set_size(file, (size_t)arg);
		break;
	case ASHMEM_GET_SIZE:
		ret = ashmem_compat_get_size(file);
		break;
	case ASHMEM_SET_PROT_MASK:
		ret = ashmem_compat_set_prot_mask(file, arg);
		break;
	case ASHMEM_GET_PROT_MASK:
		ret = ashmem_compat_get_prot_mask(file);
		break;
	case ASHMEM_PIN:
		ret = ASHMEM_NOT_PURGED;
		break;
	case ASHMEM_UNPIN:
		ret = 0;
		break;
	case ASHMEM_GET_PIN_STATUS:
		ret = ASHMEM_IS_PINNED;
		break;
	case ASHMEM_PURGE_ALL_CACHES:
		ret = capable(CAP_SYS_ADMIN) ? 0 : -EPERM;
		break;
	case ASHMEM_GET_FILE_ID:
		inode_nr = file_inode(file)->i_ino;
		if (copy_to_user((void __user *)arg, &inode_nr , sizeof(inode_nr)))
			ret = -EFAULT;
		else
			ret = 0;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ashmem_compat_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (cmd == COMPAT_ASHMEM_SET_SIZE)
		cmd = ASHMEM_SET_SIZE;
	else if (cmd == COMPAT_ASHMEM_SET_PROT_MASK)
		cmd = ASHMEM_SET_PROT_MASK;

	return ashmem_compat_ioctl(file, cmd, arg);
}
#endif

static unsigned long ashmem_compat_get_unmapped_area(struct file *file, unsigned long addr,
						     unsigned long len, unsigned long pgoff,
						     unsigned long flags)
{
	return mm_get_unmapped_area(current->mm, file, addr, len, pgoff, flags);
}

static inline unsigned long calc_vm_may_flags(unsigned long prot)
{
	return _calc_vm_trans(prot, PROT_READ,  VM_MAYREAD) |
	       _calc_vm_trans(prot, PROT_WRITE, VM_MAYWRITE) |
	       _calc_vm_trans(prot, PROT_EXEC,  VM_MAYEXEC);
}

static int ashmem_compat_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(file);
	unsigned long ashmem_compat_mask;
	unsigned long blocked_prot_mask, blocked_vm_prot_mask;
	loff_t size;
	int ret;

	inode_lock(inode);

	/* Caller must set size before mapping. */
	size = i_size_read(inode);
	if (size <= 0) {
		ret = -EINVAL;
		goto out;
	}

	if (vma->vm_end - vma->vm_start > PAGE_ALIGN(size)) {
		ret = -EINVAL;
		goto out;
	}

	blocked_prot_mask = ~__ashmem_compat_get_prot_mask(file);
	blocked_vm_prot_mask = calc_vm_prot_bits(blocked_prot_mask, 0) & VM_ACCESS_FLAGS;
	if (vma->vm_flags & blocked_vm_prot_mask) {
		ret = -EPERM;
		goto out;
	}
	/*
	 * Clear matching VM_MAY* flags from the set of permissions that are blocked to prevent
	 * the region's permissions from changing with mprotect() later.
	 */
	vm_flags_clear(vma, calc_vm_may_flags(blocked_prot_mask));

	ret = shmem_mmap(file, vma);

	if (!ret) {
		ashmem_compat_mask = get_ashmem_compat_mask(file);
		set_ashmem_compat_mask(file, ashmem_compat_mask | ASHMEM_COMPAT_MAPPED);
	}

out:
	inode_unlock(inode);
	return ret;
}

static char *ashmem_compat_dname(struct dentry *dentry, char *buffer, int buflen)
{
	ssize_t ret = 0;
	struct inode *inode = d_inode(dentry);
	char local_name[ASHMEM_NAME_LEN];
	char *filename;

	inode_lock_shared(inode);
	filename = dentry->d_fsdata ? (char *)dentry->d_fsdata : (char *)dentry->d_name.name;
	ret = strscpy(local_name, filename, sizeof(local_name));
	inode_unlock_shared(inode);

	return dynamic_dname(buffer, buflen, "/%s (deleted)", ret > 0 ? local_name : "");
}

static void ashmem_compat_release(struct dentry *dentry)
{
	kfree(dentry->d_fsdata);
}

static const struct dentry_operations ashmem_compat_dentry_ops = {
	.d_dname = ashmem_compat_dname,
	.d_release = ashmem_compat_release,
};

/*
 * install_ashmem_compat_fops - Change the fops for a memfd to ashmem_compat_fops.
 * @file: The memfd file structure.
 */
void install_ashmem_compat_fops(struct file *file)
{
	if (!file || !shmem_file(file))
		return;

	spin_lock(&fops_lock);
	if (!shmem_mmap) {
		ashmem_compat_fops = *file->f_op;
		shmem_mmap = file->f_op->mmap;

		ashmem_compat_fops.unlocked_ioctl = ashmem_compat_ioctl;
#ifdef CONFIG_COMPAT
		ashmem_compat_fops.compat_ioctl = ashmem_compat_ioctl_compat;
#endif
		/* Overwrite mmap() to be able to do additional checks that ashmem requires. */
		ashmem_compat_fops.mmap = ashmem_compat_mmap;
		/*
		 * If the get_unmapped_area() function pointer is not overwritten, then
		 * shmem_get_unmapped_area() will be invoked in the future.
		 *
		 * This is not desirable as, shmem_get_unmapped_area() will trigger a VM_BUG_ON()
		 * since file->f_op != shmem_file_operations.
		 */
		ashmem_compat_fops.get_unmapped_area = ashmem_compat_get_unmapped_area;
	}
	spin_unlock(&fops_lock);

	/* All memfd/ashmem buffers start with PROT_READ | PROT_WRITE | PROT_EXEC permissions. */
	set_ashmem_compat_mask(file, FIELD_PREP(ASHMEM_COMPAT_PROT, PROT_READ | PROT_WRITE |
						PROT_EXEC));
	file->f_op = &ashmem_compat_fops;
	file->f_path.dentry->d_op = &ashmem_compat_dentry_ops;
}
