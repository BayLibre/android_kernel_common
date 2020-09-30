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
#include <linux/mempool.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/wait.h>

#define DM_MSG_PREFIX "user"

#define MAX_OUTSTANDING_MESSAGES 128

/*
 * FIXME: Not quite what's in blk-map.c, but instead what I thought the
 * functions in blk-map did.  This one seems more generally useful and I think
 * we could write the blk-map version in terms of this one.  The differences
 * are that this has a return value that counts, and blk-map uses the BIO _all
 * iters.
 *
 * These advance the BIO iter but don't advance the IOV iter, which is a bit
 * odd.
 */
static ssize_t bio_copy_from_iter(struct bio *bio, struct iov_iter *iter)
{
	struct bio_vec bvec;
	struct bvec_iter biter;
	ssize_t out = 0;

	bio_for_each_segment(bvec, bio, biter) {
		ssize_t ret;

		ret = copy_page_from_iter(bvec.bv_page,
					  bvec.bv_offset,
					  bvec.bv_len,
					  iter);

		/*
		 * FIXME: I thought that IOV copies had a mechanism for
		 * terminating early, if for example a signal came in while
		 * sleeping waiting for a page to be mapped, but I don't see
		 * where would happen.
		 */
		WARN_ON(ret < 0);
		out += ret;

		if (!iov_iter_count(iter))
			break;

		if (ret < bvec.bv_len)
			return ret;
	}

	return out;
}

static ssize_t bio_copy_to_iter(struct bio *bio, struct iov_iter *iter)
{
	struct bio_vec bvec;
	struct bvec_iter biter;
	ssize_t out = 0;

	bio_for_each_segment(bvec, bio, biter) {
		ssize_t ret;

		ret = copy_page_to_iter(bvec.bv_page,
					bvec.bv_offset,
					bvec.bv_len,
					iter);

		WARN_ON(ret < 0);
		out += ret;

		if (!iov_iter_count(iter))
			break;

		if (ret < bvec.bv_len)
			return ret;
	}

	return out;
}

/*
 * There are currently two structures that represent a message, and while
 * there's nothing actually broken about this approach it's just
 * unnecessarily complicated.  My original idea was to split this into two
 * problems: tracking the message as a whole vs sending/receiving a message.
 * It turns out I just didn't end up saving any code by trying to unify
 * sending/receiving behind the same interface, as there are enough subtle
 * behavioral differences that it's really just two separate code paths.
 *
 * I'm planning on replacing this with a single structure, with the two
 * iteration contexts merged in.  This won't change the code much at all, but
 * will allow me to avoid the race condition in dev_write() as well as cut out
 * the alloc/free in dev_read().
 */
struct message_iter {
	size_t posn;
	size_t total_to_user;
	size_t total_from_user;
	struct dm_user_message msg;
	struct bio *bio;
};

struct message {
	struct list_head list;
	u64 seq;
	struct bio *bio;
};

/*
 * A single communication channel between the kernel and the user.
 */
struct channel {
	/*
	 * This entire structure is protected by a single lock.  It'd probably
	 * be better for performance to split it into into three locks.
	 */
	struct mutex lock;

	/*
	 * There is only one point at which anything blocks: userspace blocks
	 * reading a new message, which is woken up by device mapper providing
	 * a new BIO to process.
	 */
	struct wait_queue_head wq;

	unsigned long next_seq_to_user;
	struct list_head to_user_queue;
	struct list_head from_user_outstanding;
	struct message_iter to_user_iter;
	struct message_iter from_user_iter;
	ssize_t to_user_error;
	ssize_t from_user_error;
	mempool_t message_pool;
	mempool_t message_iter_pool;

	/*
	 * Right now there is just a single open file associated with each 
	 * channel.  This essentially limits userspace to a single operation
	 * queue, which isn't ideal.  I'd like to allow userspace to open the
	 * control device multiple times, which would correspond to multiple
	 * queues.
	 */
	struct miscdevice *miscdev;
	struct file *file;
};

/*
 * On the surface nothing is globally shared, but there's one race that I can't
 * figure out a better way to fix: device mapper destroying the control node at
 * the same time 
 *
 *
 * Another option would be to reference count the channel, cleaning it up when
 * everything's dereferenced it.  I'm not sure this is cleaner than the lock,
 * though.
 *
 * So, there's a global lock.  Nothing that takes it is on a hot path, but it's
 * still ugly.
 */
struct mutex open_lock;

/*
 * Operations can be initiated from multiple points but they all refer to the
 * same channel.  Since there's a bunch of void*s in the way I've created these
 * short helpers so I can forget what actually goes where.
 */
static inline struct channel *channel_from_target(struct dm_target *target)
{
	return target->private;
}

static inline struct channel *channel_from_file(struct file *file)
{
	struct miscdevice *miscdev;

	miscdev = file->private_data;
	return miscdev->private;
}

static inline struct channel *channel_from_miscdev(struct miscdevice *miscdev)
{
	return miscdev->private;
}

/*
 * BIO-related helpers.
 */
static inline size_t bio_size(struct bio *bio)
{
	size_t out;
	struct bio_vec bvec;
	struct bvec_iter iter;

	out = 0;
	bio_for_each_segment(bvec, bio, iter)
		out += bio_iter_len(bio, iter);
	return out;
}

static inline size_t bio_bytes_needed_to_user(struct bio *bio)
{
	switch (bio_op(bio)) {
	case REQ_OP_READ:
		return 0;
	case REQ_OP_WRITE:
		return bio_size(bio);
	}

	/*
	 * FIXME: There's a bunch of other types to handle.  I also need to
	 * sort out ABI compatibility here -- do I just have userspace fail the
	 * types it doesn't understand and then emulate them in the kernel?
	 * What about flags, are they 0-compatible in the kernel?
	 */
	BUG();
}

static inline size_t bio_bytes_needed_from_user(struct bio *bio)
{
	switch (bio_op(bio)) {
	case REQ_OP_READ:
		return bio_size(bio);
	case REQ_OP_WRITE:
		return 0;
	}

	/* FIXME: as above */
	BUG();
}

static inline unsigned long bio_type_to_user_type(struct bio *bio)
{
	switch (bio_op(bio)) {
	case REQ_OP_READ:
		return DM_USER_MAP_READ;
	case REQ_OP_WRITE:
		return DM_USER_MAP_WRITE;
	}

	BUG();
}

/*
 * Mechanisms to process the message iterators.  These convert between the
 * contiguous memory interface that is provided to the user and the BIO-backed
 * implementation in the kernel.  These are going to be refactored when the
 * message_iter/message merge happens, which is why they're a bit oddly named
 * right now.
 */
static void message_iter_init(struct message_iter *iter)
{
	memset(iter, '\0', sizeof(*iter));
}

static int message_iter_on_bio(struct message_iter *iter)
{
	return iter->posn >= sizeof(iter->msg);
}

static int message_iter_has_bio(struct message_iter *iter)
{
	return iter->bio != NULL;
}

static ssize_t message_iter_to_iov(struct message_iter *iter, struct iov_iter *to)
{
	ssize_t copied = 0;

	if (!iov_iter_count(to))
		return 0;

	if (iter->posn < sizeof(iter->msg)) {
		copied = copy_to_iter((char *)(&iter->msg) + iter->posn,
				      sizeof(iter->msg) - iter->posn,
				      to);
	} else {
		copied = bio_copy_to_iter(iter->bio, to);
		if (copied > 0)
			bio_advance(iter->bio, copied);
	} 

	if (copied < 0)
		return copied;

	iter->posn += copied;
	return copied;
}

static ssize_t message_iter_from_iov(struct message_iter *iter, struct iov_iter *from)
{
	ssize_t copied = 0;

	if (!iov_iter_count(from))
		return 0;

	if (iter->posn < sizeof(iter->msg)) {
		copied = copy_from_iter((char *)(&iter->msg) + iter->posn,
					sizeof(iter->msg) - iter->posn,
					from);
	} else {
		copied = bio_copy_from_iter(iter->bio, from);
		if (copied > 0)
			bio_advance(iter->bio, copied);
	}

	if (copied < 0)
		return copied;

	iter->posn += copied;
	return copied;
}

static void message_iter_adopt(struct message_iter *iter, struct message *msg)
{
	iter->msg.seq = msg->seq;
	iter->msg.type = bio_type_to_user_type(msg->bio);
	iter->msg.flags = 0;
	/*
	 * FIXME: This ABI assumes that BIOs contain a single set of contiguous
	 * on-disk sectors.  As far as I can tell that's true (bio_truncate()
	 * seems to assume that, for example) but I don't know if it will
	 * always be the case.
	 *
	 * Either way, it may be worth changing the message format to have
	 * scatter/gather for the data.  That quickly goes down a zero-copy
	 * rabbit hole, though, and I don't want to do that right now.
	 */
	iter->msg.sector = msg->bio->bi_iter.bi_sector;
	iter->msg.len = bio_size(msg->bio);

	iter->total_from_user = sizeof(iter->msg) + bio_bytes_needed_from_user(msg->bio);
	iter->total_to_user = sizeof(iter->msg) + bio_bytes_needed_to_user(msg->bio);
	iter->bio = msg->bio;
}

static struct message *
message_iter_lookup_and_move(struct message_iter *iter, struct list_head *search,
			     struct channel *c)
{
	struct message *msg;
	struct list_head *cur;

	list_for_each(cur, search) {
		msg = list_entry(cur, struct message, list);

		if (msg->seq == iter->msg.seq) {
			message_iter_adopt(iter, msg);
			list_del(cur);
			mempool_free(cur, &c->message_pool);
			return msg;
		}
	}

	return NULL;
}

static int message_iter_at_end_to_user(struct message_iter *iter)
{
	return iter->posn >= iter->total_to_user;
}

static int message_iter_at_end_from_user(struct message_iter *iter)
{
	return iter->posn >= iter->total_from_user;
}

static void message_iter_move_end(struct message_iter *iter, struct list_head *list,
				  struct message *msg)
{
	INIT_LIST_HEAD(&msg->list);
	msg->seq = iter->msg.seq;
	msg->bio = iter->bio;
	list_add_tail(&msg->list, list);

	memset(iter, '\0', sizeof(*iter));
}

static void message_iter_finish(struct message_iter *iter)
{
	bio_endio(iter->bio);
	bio_put(iter->bio);

	memset(iter, '\0', sizeof(*iter));
}

/*
 * The control node in /dev/dm-user/.
 */
static int dev_open(struct inode *inode, struct file *file)
{
	struct channel *c;

	mutex_lock(&open_lock);
	c = channel_from_file(file);
	c->from_user_error = 0;
	c->to_user_error = 0;
	mutex_unlock(&open_lock);

	return 0;
}

static ssize_t dev_read(struct kiocb *iocb, struct iov_iter *to)
{
	struct channel *c = channel_from_file(iocb->ki_filp);
	ssize_t total_processed = 0;
	ssize_t processed;

	BUG_ON(!c);
	mutex_lock(&c->lock);

	if (unlikely(c->to_user_error)) {
		total_processed = c->to_user_error;
		goto cleanup_unlock;
	}

	if (!message_iter_has_bio(&c->to_user_iter)) {
		struct message *msg;

		while (list_empty(&c->to_user_queue)) {
			int err;

			mutex_unlock(&c->lock);
			err = wait_event_interruptible(c->wq, !list_empty(&c->to_user_queue));
			mutex_lock(&c->lock);

			if (err != 0) {
				/*
				 * We haven't processed any bytes in either the
				 * BIO or the IOV, so we can just terminate
				 * right now.  Elsewhere in the kernel handles
				 * restarting the syscall when appropriate.
				 */
				total_processed = err;
				goto cleanup_unlock;
			}
		}

		msg = list_first_entry(&c->to_user_queue, struct message, list);
		message_iter_adopt(&c->to_user_iter, msg);
		list_del(&msg->list);
		mempool_free(msg, &c->message_pool);
	}

	processed = message_iter_to_iov(&c->to_user_iter, to);
	WARN_ON(processed < 0);
	total_processed += processed;
	WARN_ON(total_processed <= 0);

	if (message_iter_at_end_to_user(&c->to_user_iter))
		message_iter_move_end(&c->to_user_iter, &c->from_user_outstanding,
				      mempool_alloc(&c->message_pool, GFP_NOIO));

cleanup_unlock:
	mutex_unlock(&c->lock);
	return total_processed;

}

static ssize_t dev_splice_read(struct file *in, loff_t *ppos,
			       struct pipe_inode_info *pipe,
			       size_t len, unsigned int flags)
{
	return -1;
}

static ssize_t dev_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct channel *c = channel_from_file(iocb->ki_filp);
	ssize_t total_processed = 0;
	ssize_t processed;

	mutex_lock(&c->lock);

	if (unlikely(c->from_user_error)) {
		total_processed = c->from_user_error;
		goto cleanup_unlock;
	}

	if (!message_iter_has_bio(&c->from_user_iter)) {
		processed = message_iter_from_iov(&c->from_user_iter, from);
		if (processed <= 0) {
			c->from_user_error = -EINVAL;
			goto cleanup_unlock;
		}
		total_processed += processed;

		/*
		 * In the unlikely event the user has provided us a very short
		 * write, not even big enough to fill a message, just succeed.
		 * We'll eventually build up enough bytes to do something.
		 */
		if (!message_iter_on_bio(&c->from_user_iter))
			goto cleanup_unlock;

		/*
		 * FIXME: This doesn't block, which means userspace must have
		 * read the entire message before it can start replying to it.
		 * I don't think that's a problem, but with a more complicated
		 * message format it might become one.  Either way, that should
		 * go away when I merge the message iterators as I can start
		 * processing writes before finishing the read.
		 */
		if (message_iter_lookup_and_move(&c->from_user_iter,
						 &c->from_user_outstanding, c) == NULL) {
			c->from_user_error = -EINVAL;
			goto cleanup_unlock;
		}
	}

	processed = message_iter_from_iov(&c->from_user_iter, from);
	WARN_ON(processed < 0);
	total_processed += processed;
	WARN_ON(total_processed <= 0);

	if (message_iter_at_end_from_user(&c->from_user_iter))
		message_iter_finish(&c->from_user_iter);

	goto cleanup_unlock;

cleanup_unlock:
	mutex_unlock(&c->lock);
	return total_processed;

}

static ssize_t dev_splice_write(struct pipe_inode_info *pipe,
				struct file *out, loff_t *ppos,
				size_t len, unsigned int flags)
{
	return -ENOTSUPP;
}

static __poll_t dev_poll(struct file *file, poll_table *wait)
{
	return -ENOTSUPP;
}

static int dev_release(struct inode *inode, struct file *file)
{
	struct channel *c;

	mutex_lock(&open_lock);
	c = channel_from_file(file);
	c->file = NULL;
	mutex_unlock(&open_lock);

	return 0;
}

static int dev_fasync(int fd, struct file *file, int on)
{
	return -ENOTSUPP;
}

static long dev_ioctl(struct file *file, unsigned int cmd,
		      unsigned long arg)
{
	return -ENOTSUPP;
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

/*
 * Construct a dummy mapping that only returns users
 */
static int user_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct channel *c;
	int r;

	if (argc != 3) {
		ti->error = "Invalid argument count";
		r = -EINVAL;
		goto cleanup_none;
	}

	/*
	 *FIXME: for now, though presumably we'd like userspace to get those.
	 */
	ti->num_discard_bios = 1;

	c = kzalloc(sizeof(struct channel), GFP_KERNEL);
	ti->private = c;
	if (c == NULL) {
		r = -ENOSPC;
		goto cleanup_none;
	}
	mutex_init(&c->lock);
	init_waitqueue_head(&c->wq);
	c->next_seq_to_user = 0;
	INIT_LIST_HEAD(&c->to_user_queue);
	message_iter_init(&c->to_user_iter);
	c->to_user_error = 0;
	INIT_LIST_HEAD(&c->from_user_outstanding);
	message_iter_init(&c->from_user_iter);
	c->from_user_error = 0;

	c->miscdev = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
	if (c->miscdev == NULL) {
		r = -ENOSPC;
		goto cleanup_channel;
	}
	c->miscdev->private = c;
	c->miscdev->minor = MISC_DYNAMIC_MINOR;
	c->miscdev->fops = &file_operations;
	/*
	 * FIXME: Everyone else ends up with their own file system for this
	 * sort of stuff, should we just be starting with one?
	 */
	c->miscdev->name = kasprintf(GFP_KERNEL, "dm-user/%s", argv[2]);
	if (c->miscdev->name == NULL) {
		r = -ENOSPC;
		goto cleanup_misc;
	}

	r = mempool_init_kmalloc_pool(&c->message_pool, MAX_OUTSTANDING_MESSAGES,
				      sizeof(struct message));
	if (r < 0)
		goto cleanup_misc;

	r = mempool_init_kmalloc_pool(&c->message_iter_pool, MAX_OUTSTANDING_MESSAGES,
				      sizeof(struct message_iter));
	if (r < 0)
		goto cleanup_message_pool;

	/*
	 * Once the miscdev is registered it can be opened and therefor
	 * concurrent references to the channel can happen.  Everything that's
	 * not protected by the channel's lock is ready only from this point
	 * on.
	 */
	smp_mb();

	r = misc_register(c->miscdev);
	if (r) {
		DMERR("Unable to register miscdev %s for dm-user", c->miscdev->name);
		goto cleanup_misc_name;
	}

	return 0;

cleanup_message_pool:
	mempool_destroy(&c->message_pool);
cleanup_misc_name:
	kfree(c->miscdev->name);
cleanup_misc:
	kfree(c->miscdev);
cleanup_channel:
	kfree(c);
cleanup_none:
	return r;
}

static void user_dtr(struct dm_target *ti)
{
	struct channel *c;

	mutex_lock(&open_lock);

	c = channel_from_target(ti);
	misc_deregister(c->miscdev);
	kfree(c->miscdev);
	kfree(c);

	mutex_unlock(&open_lock);
}

/*
 * Return users only on reads
 */
static int user_map(struct dm_target *ti, struct bio *bio)
{
	struct channel *c;
	struct message *entry;

	c = channel_from_target(ti);
	BUG_ON(!c);

	entry = mempool_alloc(&c->message_pool, GFP_NOIO);
	if (unlikely(entry == NULL)) {
		pr_warn("dm-user map() could not allocate a buffer");
		/*
		 * FIXME: If I return this frequently then things go off the
		 * rails.  Not sure what's going on.
		 */
		return DM_MAPIO_REQUEUE;
	}

	bio_get(bio);
	mutex_lock(&c->lock);
	entry->bio = bio;
	entry->seq = c->next_seq_to_user++;
	INIT_LIST_HEAD(&entry->list);
	list_add_tail(&entry->list, &c->to_user_queue);
	wake_up_interruptible(&c->wq);
	mutex_unlock(&c->lock);
	return DM_MAPIO_SUBMITTED;
}

static struct target_type user_target = {
	.name		= "user",
	.version	= {1, 0, 0},
	.module		= THIS_MODULE,
	.ctr		= user_ctr,
	.dtr		= user_dtr,
	.map		= user_map,
};

static int __init dm_user_init(void)
{
	int r;

	mutex_init(&open_lock);

	r = dm_register_target(&user_target);
	if (r) {
		DMERR("register failed %d", r);
		goto error;
	}

	return 0;

error:
	return r;
}

static void __exit dm_user_exit(void)
{
	dm_unregister_target(&user_target);
}

module_init(dm_user_init)
module_exit(dm_user_exit)

MODULE_AUTHOR("Palmer Dabbelt <palmerdabbelt@google.com>");
MODULE_DESCRIPTION(DM_NAME " target returning blocks from userspace");
MODULE_LICENSE("GPL");
