/*
 * Copyright (C) 2003 Jana Saout <jana@saout.de>
 * Copyright (C) 2020 Palmer Dabbelt <palmerdabbelt@google.com>
 *
 * Based on dm-zero.c
 *
 * This file is released under the GPL.
 */

#include <linux/device-mapper.h>
#include <uapi/linux/dm-user.h>

#include <linux/bio.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/wait.h>

#define DM_MSG_PREFIX "user"

struct channel {
	struct mutex lock;
	unsigned long seq;
	struct bio *to_user;
	struct bio *from_user;
	struct wait_queue_head wq;
};

static struct channel *ch_glob;

static inline struct channel *channel_from_target(struct dm_target *target)
{
	return target->private;
}

static inline struct channel *channel_from_file(struct file *file)
{
	return file->private_data;
}

static inline size_t bytes_needed_to_user(struct bio *bio)
{
	return 4096;
}

static inline size_t bytes_needed_from_user(struct bio *bio)
{
	return 4096;
}

static inline unsigned long bio_type_to_user_type(struct bio *bio)
{
	return DM_USER_MAP_READ;
}

/*
 * /dev/dm-user control node
 */
static int dev_open(struct inode *inode, struct file *file)
{
	struct channel *c = ch_glob;
	file->private_data = c;

	return 0;
}

static ssize_t dev_read(struct kiocb *iocb, struct iov_iter *to)
{
	struct channel * c;
	struct dm_user_message msg;
	ssize_t out;
	struct bio_vec bvec;
	struct bvec_iter iter;

	out = 0;
	c = channel_from_file(iocb->ki_filp);
	BUG_ON(!c);

	/*
	 * Requests are of variable size, but we must at least have enough
	 * space to fill out a single request.
	 */
	if (iov_iter_count(to) < sizeof(msg))
		return -EINVAL;

	/*
	 * Try to pass a message to userspace.  First we must get a message.
	 * The trick here is that we have to know what the message is in order
	 * to make sure there's enough space, but we can't consume it.
	 */
	mutex_lock(&c->lock);
	while (c->to_user == NULL) {
		mutex_unlock(&c->lock);
		wait_event_interruptible(c->wq, c->to_user != NULL && c->from_user == NULL);
		mutex_lock(&c->lock);
	}

	if (iov_iter_count(to) < sizeof(msg) + bytes_needed_to_user(c->to_user)) {
		out = -ENOSPC;
		goto unlock;
	}

	msg.seq = c->seq;
	msg.type = bio_type_to_user_type(c->to_user);
	msg.flags = 0;
	msg.len = bytes_needed_to_user(c->to_user);
	out += copy_to_iter(&msg, sizeof(msg), to);
	bio_for_each_segment(bvec, c->to_user, iter) {
		out += copy_page_to_iter(bio_iter_page(c->to_user, iter),
					 bio_iter_offset(c->to_user, iter),
					 bio_iter_len(c->to_user, iter),
					 to);
	}

	c->from_user = c->to_user;
	c->to_user = NULL;

	wake_up_interruptible(&c->wq);

unlock:
	mutex_unlock(&c->lock);
	return out;
}

static ssize_t dev_splice_read(struct file *in, loff_t *ppos,
			       struct pipe_inode_info *pipe,
			       size_t len, unsigned int flags)
{
	return -1;
}

static ssize_t dev_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct channel * c;
	struct dm_user_message msg;
	ssize_t out;
	struct bio_vec bvec;
	struct bvec_iter iter;

	out = 0;
	c = channel_from_file(iocb->ki_filp);
	BUG_ON(!c);

	/*
	 * We can copy the first bits from userspace now, as we know there must
	 * be at least a header in any write request.
	 */
	if (iov_iter_count(from) < sizeof(msg))
		return -EINVAL;
	copy_from_iter(&msg, sizeof(msg), from);

	/*
	 * There must be a BIO ready to handle this response.  There's only one
	 * right now, so we just go look for it.
	 */
	mutex_lock(&c->lock);
	while (c->from_user == NULL) {
		mutex_unlock(&c->lock);
		wait_event_interruptible(c->wq, c->from_user != NULL);
		mutex_lock(&c->lock);
	}

	if (iov_iter_count(from) < sizeof(msg) + bytes_needed_from_user(c->from_user)) {
		out = -ENOSPC;
		goto unlock;
	}

	bio_for_each_segment(bvec, c->from_user, iter) {
		out += copy_page_from_iter(bio_iter_page(c->from_user, iter),
					   bio_iter_offset(c->from_user, iter),
					   bio_iter_len(c->from_user, iter),
					   from);
	}

	bio_endio(c->from_user);
	c->from_user = NULL;
	wake_up_interruptible(&c->wq);

	out += iov_iter_count(from);
	iov_iter_advance(from, iov_iter_count(from));

	c->seq++;

unlock:
	mutex_unlock(&c->lock);
	return out;
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

static int dev_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
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

static const struct file_operations file_operations = {
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
	.minor = DM_USER_MINOR,
	.name  = "dm-user",
	.fops  = &file_operations,
};

/*
 * Construct a dummy mapping that only returns users
 */
static int user_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	/*
	 * We don't actually care about the arguments, but the rest of DM needs
	 * some sort of size/length pair.
	 */
	if (argc != 2) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	/*
	 * Silently drop discards, avoiding -EOPNOTSUPP.
	 */
	ti->num_discard_bios = 1;

	ti->private = ch_glob;

	return 0;
}

/*
 * Return users only on reads
 */
static int user_map(struct dm_target *ti, struct bio *bio)
{
	struct channel *c;

	pr_info("user_map() called\n");

	c = channel_from_target(ti);
	BUG_ON(!c);

	mutex_lock(&c->lock);
	/*
	 * Find a new buffer slot so we can talk to userspace.  These probably
	 * won't run out, but if they do we just request that everyone else
	 * slows down for a bit.  This could cause requeue spins forever, so
	 * we may want to track some sort of metric here for a warning.
	 */
	if (unlikely(c->to_user != NULL)) {
		mutex_unlock(&c->lock);
		return DM_MAPIO_REQUEUE;
	}

	c->to_user = bio;
	wake_up_interruptible(&c->wq);
	mutex_unlock(&c->lock);
	return DM_MAPIO_SUBMITTED;
}

static struct target_type user_target = {
	.name   = "user",
	.version = {1, 0, 0},
	.module = THIS_MODULE,
	.ctr    = user_ctr,
	.map    = user_map,
};

static int __init dm_user_init(void)
{
	int r;

	ch_glob = kzalloc(sizeof(struct channel), GFP_KERNEL);
	pr_info("ch_glob: %lx\n", ch_glob);

	mutex_init(&ch_glob->lock);
	ch_glob->seq = 0x976196;
	ch_glob->to_user = NULL;
	ch_glob->from_user = NULL;
	init_waitqueue_head(&ch_glob->wq);

	r = dm_register_target(&user_target);
	if (r) {
		DMERR("register failed %d", r);
		goto error;
	}

	r = misc_register(&miscdev);
	if (r) {
		DMERR("Unable to register a misc device for dm-user");
		goto unregister_target;
	}

	return 0;

unregister_target:
	dm_unregister_target(&user_target);
error:
	return r;
}

static void __exit dm_user_exit(void)
{
	dm_unregister_target(&user_target);
	misc_deregister(&miscdev);
	kfree(ch_glob);
}

module_init(dm_user_init)
module_exit(dm_user_exit)

MODULE_AUTHOR("Palmer Dabbelt <palmerdabbelt@google.com>");
MODULE_DESCRIPTION(DM_NAME " target returning blocks from userspace");
MODULE_LICENSE("GPL");
