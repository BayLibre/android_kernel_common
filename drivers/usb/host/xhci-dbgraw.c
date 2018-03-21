/**
 * xhci-dbcraw.c - Raw DbC for xHCI debug capability
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author: Rajaram Regupathy <rajaram.regupathy@imtel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>

#include "xhci.h"
#include "xhci-dbgcap.h"
#include <linux/miscdevice.h>

#define DBC_RAW_BULK_BUFFER_SIZE           (64 *1024)


struct xhci_hcd *g_xhci ;

extern int dbc_start_tx(struct dbc_port *port)
	__releases(&port->port_lock)
	__acquires(&port->port_lock);

extern void dbc_start_rx(struct dbc_port *port)
	__releases(&port->port_lock)
	__acquires(&port->port_lock);

struct xhci_hcd *g_xhci ;
int rx_done;

extern unsigned int
dbc_buf_get(struct dbc_buf *db, char *buf, unsigned int count);

extern unsigned int
dbc_buf_put(struct dbc_buf *db, const char *buf, unsigned int count);

static const char dbc_shortname[] = "dbc_raw";

struct dbc_dev {

	spinlock_t lock;

	int error;

	atomic_t read_excl;
	atomic_t write_excl;
	atomic_t open_excl;

	struct list_head tx_idle;

	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;

	int rx_done;
};

static inline int dbc_lock(atomic_t *excl)
{
	if (atomic_inc_return(excl) == 1) {
		return 0;
	} else {
		atomic_dec(excl);
		return -1;
	}
}

static inline void dbc_unlock(atomic_t *excl)
{
	atomic_dec(excl);
}
static void dbc_buf_free(struct dbc_buf *db)
{
	kfree(db->buf_buf);
	db->buf_buf = NULL;
}
static struct dbc_dev *_dbc_dev;
static void xhci_dbc_free_req(struct dbc_ep *dep, struct dbc_request *req)
{
	kfree(req->buf);
	dep->free_request(dep, req);
}

static void
xhci_dbc_free_requests(struct dbc_ep *dep, struct list_head *head)
{
	struct dbc_request	*req;

	while (!list_empty(head)) {
		req = list_entry(head->next, struct dbc_request, list_pool);
		list_del(&req->list_pool);
		xhci_dbc_free_req(dep, req);
	}
}

static int dbc_buf_alloc(struct dbc_buf *db, unsigned int size)
{
	db->buf_buf = kzalloc(size, GFP_KERNEL);
	if (!db->buf_buf)
		return -ENOMEM;

	db->buf_size = size;
	db->buf_put = db->buf_buf;
	db->buf_get = db->buf_buf;

	return 0;
}


static int
xhci_dbc_alloc_requests(struct dbc_ep *dep, struct list_head *head,
			void (*fn)(struct xhci_hcd *, struct dbc_request *))
{
	int			i;
	struct dbc_request	*req;

	for (i = 0; i < QUEUE_SIZE; i++) {
		req = dep->alloc_request(dep, GFP_ATOMIC);
		if (!req)
			break;

		req->length = DBC_MAX_PACKET;
		req->buf = kmalloc(req->length, GFP_KERNEL);
		if (!req->buf) {
			xhci_dbc_free_req(dep, req);
			break;
		}

		req->complete = fn;
		list_add_tail(&req->list_pool, head);
	}

	return list_empty(head) ? -ENOMEM : 0;
}



static void dbc_complete_in(struct xhci_hcd *xhci, struct dbc_request *req)
{
	struct dbc_dev *dev = _dbc_dev;

	if (req->status != 0)
		dev->error = 1;

	wake_up(&dev->write_wq);

}
static void dbc_complete_out(struct xhci_hcd *xhci, struct dbc_request *req)
{
	struct dbc_dev *dev = _dbc_dev;

	if (req->status != 0)
		dev->error = 1;
	rx_done=1;
	wake_up(&dev->read_wq);
}


static ssize_t dbc_read(struct file *fp, char __user *buf,
				size_t count, loff_t *pos)
{

	int			status = 0;
	struct dbc_dev *dev = fp->private_data;
	struct xhci_hcd *xhci = g_xhci;
	struct dbc_request *req = 0;
	struct xhci_dbc		*dbc = xhci->dbc;
	struct dbc_port		*port = &dbc->port;
	int r = count, xfer;
	int ret;
	struct list_head	*pool = &port->read_pool;

	if (!_dbc_dev)
		return -ENODEV;

	if (dbc_lock(&dev->read_excl))
		return -EBUSY;

retry_read:
	/* get an idle tx request to use */
	req = 0;
	rx_done=0;

	req = list_entry(pool->next, struct dbc_request, list_pool);

	req->actual = 0;
	//DBG("<= dbc_read  %d, %d\n", count,req);
	list_del(&req->list_pool);

	if (req != 0) {
		if (count > DBC_RAW_BULK_BUFFER_SIZE)
			xfer = DBC_RAW_BULK_BUFFER_SIZE;
		else
			xfer = count;


		req->length = xfer;

		status = port->in->queue(port->in, req, GFP_ATOMIC);

		if (status < 0) {
			//DBG("dbc_read: xfer error %d\n", ret);
			dev->error = 1;
			r = -EIO;
			dbc_unlock(&dev->read_excl);
			return r;
		}
	
	}

	wait_event_interruptible(dev->read_wq,rx_done);

	xfer = (req->actual < count) ? req->actual : count;

	if(req->actual == 0)
	{
		list_add_tail(&req->list_pool, &port->read_pool);
		goto retry_read;
	}
	else
	{		
		if (copy_to_user(buf, req->buf, xfer))
		r = -EFAULT;
		list_add_tail(&req->list_pool, &port->read_pool);
	}
	
	dbc_unlock(&dev->read_excl);
	//DBG("dbc_read returning %d/%d , %d\n", req->actual,req->status,req);
	return r;

}

static ssize_t dbc_write(struct file *fp, const char __user *buf,
				 size_t count, loff_t *pos)
{

	int			status = 0;
	struct dbc_dev *dev = fp->private_data;
	struct xhci_hcd *xhci = g_xhci;
	struct dbc_request *req = 0;
	struct xhci_dbc		*dbc = xhci->dbc;
	struct dbc_port		*port = &dbc->port;
	int r = count, xfer;
	int ret;
	struct list_head	*pool = &port->write_pool;

	if (!_dbc_dev)
		return -ENODEV;



	if (dbc_lock(&dev->write_excl))
		return -EBUSY;



	/* get an idle tx request to use */
	req = 0;
	
	req = list_entry(pool->next, struct dbc_request, list_pool);
	req->actual = 0;
	//DBG("===> dbc_write  %d, %d\n", count,req);
	list_del(&req->list_pool);

	if (req != 0) {
		if (count > DBC_RAW_BULK_BUFFER_SIZE)
			xfer = DBC_RAW_BULK_BUFFER_SIZE;
		else
			xfer = count;
		if (copy_from_user(req->buf, buf, xfer)) {
			r = -EFAULT;
			dbc_unlock(&dev->write_excl);
			return r;
		}

		req->length = xfer;

		status = port->out->queue(port->out, req, GFP_ATOMIC);
		if (status < 0) {
			//DBG("dbc_write: xfer error %d\n", ret);
			dev->error = 1;
			dbc_unlock(&dev->write_excl);
			r = -EIO;
			return r;
		}
	
	}

	wait_event_interruptible(dev->write_wq,( (req->length - req->actual) == 0) );

	list_add(&req->list_pool, &port->write_pool);

	dbc_unlock(&dev->write_excl);
	//DBG("dbc_write returning %d,%d\n", req->actual,req);
	return r;

}
static int dbc_open(struct inode *ip, struct file *fp)
{

	if (!_dbc_dev)
		return -ENODEV;

	if (dbc_lock(&_dbc_dev->open_excl))
		return -EBUSY;

	fp->private_data = _dbc_dev;

	/* clear the error latch */
	_dbc_dev->error = 0;


	//DBG("dbc open called\n");
	return 0;
}
static int dbc_release(struct inode *ip, struct file *fp)
{
	dbc_unlock(&_dbc_dev->open_excl);
	return 0;

}
static const struct file_operations dbc_fops = {
	.owner = THIS_MODULE,
	.read = dbc_read,
	.write = dbc_write,
	.open = dbc_open,
	.release = dbc_release,
};

static struct miscdevice dbc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = dbc_shortname,
	.fops = &dbc_fops,
};


void xhci_dbc_raw_init_port(struct xhci_hcd *xhci, struct dbc_port *port)
{
	spin_lock_init(&port->port_lock);
	INIT_LIST_HEAD(&port->read_pool);
	INIT_LIST_HEAD(&port->read_queue);
	INIT_LIST_HEAD(&port->write_pool);

	port->in =		get_in_ep(xhci);
	port->out =		get_out_ep(xhci);
	port->n_read =		0;
}

int dbc_raw_register_device(struct xhci_hcd *xhci)
{
	struct xhci_dbc		*dbc = xhci->dbc;
	struct dbc_port		*port = &dbc->port;
	struct dbc_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->lock);

	init_waitqueue_head(&dev->read_wq);
	init_waitqueue_head(&dev->write_wq);

	atomic_set(&dev->open_excl, 0);
	atomic_set(&dev->read_excl, 0);
	atomic_set(&dev->write_excl, 0);

	INIT_LIST_HEAD(&dev->tx_idle);
	_dbc_dev = dev;
	 g_xhci = xhci;

	xhci_dbc_raw_init_port(xhci, port);

	ret = dbc_buf_alloc(&port->port_write_buf, WRITE_BUF_SIZE);
	if (ret)
		goto request_fail;

	ret = xhci_dbc_alloc_requests(port->in, &port->read_pool,
				      dbc_complete_out);
	if (ret)
		goto request_fail;

	ret = xhci_dbc_alloc_requests(port->out, &port->write_pool,
				      dbc_complete_in);
	if (ret)
		goto request_fail;


	ret = misc_register(&dbc_device);

	if (ret)
		goto register_fail;

	return ret;

request_fail:
	xhci_dbc_free_requests(port->in, &port->read_pool);
	xhci_dbc_free_requests(port->out, &port->write_pool);
	dbc_buf_free(&port->port_write_buf);

register_fail:

	kfree(_dbc_dev);
	_dbc_dev = NULL;
	xhci_err(xhci, "can't register raw port, err %d\n", ret);

	return ret;
}

void raw_unregister_device(struct xhci_hcd *xhci)
{
	misc_deregister(&dbc_device);

	kfree(_dbc_dev);
	_dbc_dev = NULL;
}
