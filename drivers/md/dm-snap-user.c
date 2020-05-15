/*
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 * Copyright (C) 2006-2008 Red Hat GmbH
 * Copyright (C) 2020 Google, Inc
 * Based on drivers/md/dm-snap-persistent.c
 *
 * This file is released under the GPL.
 */

#include "dm-exception-store.h"
#include <linux/miscdevice.h>
#include <linux/poll.h>

#define DM_MSG_PREFIX "user snapshot"

/*-----------------------------------------------------------------
 * Userspace snapshots, which have chunk satisfied by a userspace daemon rather
 * than directly in kernel.  As such, this defines no on-disk format itself but
 * instead a userspace API (accessed via connector) that allows for arbitrary
 * snapshot formats to be defined by userspace.
 *---------------------------------------------------------------*/

/*-----------------------------------------------------------------
 * /dev/dm-snap-user control node
 *---------------------------------------------------------------*/
static int dev_open(struct inode *inode, struct file *file)
{
	pr_info("dev opened\n");
	return 0;
}

static ssize_t dev_read(struct kiocb *iocb, struct iov_iter *to)
{
	return -1;
}

static ssize_t dev_splice_read(struct file *in, loff_t *ppos,
				    struct pipe_inode_info *pipe,
				    size_t len, unsigned int flags)
{
	return -1;
}

static ssize_t dev_write(struct kiocb *iocb, struct iov_iter *from)
{
	return -1;
}

static ssize_t dev_splice_write(struct pipe_inode_info *pipe,
				     struct file *out, loff_t *ppos,
				     size_t len, unsigned int flags)
{
	return -1;
}

static __poll_t dev_poll(struct file *file, poll_table *wait)
{
	return -1;
}

int dev_release(struct inode *inode, struct file *file)
{
	return -1;
}

static int dev_fasync(int fd, struct file *file, int on)
{
	return -1;
}

static long dev_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	return -1;
}

const struct file_operations file_operations = {
	.owner		= THIS_MODULE,
	.open		= dev_open,
	.llseek		= no_llseek,
	.read_iter	= dev_read,
	.splice_read	= dev_splice_read,
	.write_iter	= dev_write,
	.splice_write	= dev_splice_write,
	.poll		= dev_poll,
	.release	= dev_release,
	.fasync		= dev_fasync,
	.unlocked_ioctl = dev_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

static struct miscdevice miscdev = {
	.minor = DM_SNAP_USER_MINOR,
	.name  = "dm-snap-user",
	.fops  = &file_operations,
};

/*-----------------------------------------------------------------
 * dm-snap interface functions
 *---------------------------------------------------------------*/
static int user_ctr(struct dm_exception_store *store, char *options)
{
	return 0;
}

static void user_dtr(struct dm_exception_store *store)
{
}

static void user_usage(struct dm_exception_store *store,
			     sector_t *total_sectors,
			     sector_t *sectors_allocated,
			     sector_t *metadata_sectors)
{
	pr_warn("user_usage() unimplemented");
	*sectors_allocated = -1;
	*total_sectors     = -1;
	*metadata_sectors  = -1;
}

static int user_read_metadata(struct dm_exception_store *store,
				    int (*callback)(void *callback_context,
						    chunk_t old, chunk_t new),
				    void *callback_context)
{
	pr_warn("user_read_metadata() unimplemented");
	return 0;
}

static int user_prepare_exception(struct dm_exception_store *store,
					struct dm_exception *e)
{
	pr_warn("user_prepare_exception() unimplemented");
	return 0;
}

static void user_commit_exception(struct dm_exception_store *store,
					struct dm_exception *e, int valid,
					void (*callback) (void *, int success),
					void *callback_context)
{
	pr_warn("user_commit_exception() unimplemented");
}

static int user_prepare_merge(struct dm_exception_store *store,
				    chunk_t *last_old_chunk,
				    chunk_t *last_new_chunk)
{
	pr_warn("user_prepare_merge() unimplemented");
	return 0;
}

static int user_commit_merge(struct dm_exception_store *store,
				   int nr_merged)
{
	pr_warn("user_commit_merge() unimplemented");
	return -1;
}

static void user_drop_snapshot(struct dm_exception_store *store)
{
	pr_warn("user_drop_snapshot() unimplemented");
}

static unsigned user_status(struct dm_exception_store *store,
				  status_type_t status, char *result,
				  unsigned maxlen)
{
	pr_warn("user_status() unimplemented");
	return 0;
}

static struct dm_exception_store_type _user_type = {
	.name = "user",
	.module = THIS_MODULE,
	.ctr = user_ctr,
	.dtr = user_dtr,
	.read_metadata = user_read_metadata,
	.prepare_exception = user_prepare_exception,
	.commit_exception = user_commit_exception,
	.prepare_merge = user_prepare_merge,
	.commit_merge = user_commit_merge,
	.drop_snapshot = user_drop_snapshot,
	.usage = user_usage,
	.status = user_status,
};

int dm_user_snapshot_init(void)
{
	int r;

	r = dm_exception_store_type_register(&_user_type);
	if (r) {
		DMERR("Unable to register persistent exception store type");
		return r;
	}

	r = misc_register(&miscdev);
	if (r) {
		DMERR("Unable to register dev node");
		goto unregister_store;
	}

	return 0;
unregister_store:
	dm_exception_store_type_unregister(&_user_type);
	return r;
}

void dm_user_snapshot_exit(void)
{
	misc_deregister(&miscdev);
	dm_exception_store_type_unregister(&_user_type);
}
