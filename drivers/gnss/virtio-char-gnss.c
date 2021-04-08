// SPDX-License-Identifier: GPL-2.0
/*
 * virtio serial driver for GNSS. This driver requires the serdev binding
 *
 * Copyright 2021 Google LLC
 */

#include <linux/device.h>
#include <linux/gnss.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/virtio.h>
#include <linux/virtio_console.h>

#include "serial.h"

/*
1, create a char dev, and the char dev can communicate
directly with host(crosvm/qemu), bidirectional.
host side should provide the backend (TODO)
2, register the char dev with gnss core system

*/
struct virtio_char_gnss_dev {
    struct virtio_device *vdev;
	struct device *dev;
    struct gnss_device * gdev;
	struct virtqueue *in_vq, *out_vq;

    // to synchronize read
    struct completion have_data;
    bool busy;
    unsigned int data_avail;

    int chr_major;
    u32 id;
};

#define GNSS_VQ_RX 0
#define GNSS_VQ_TX 1
#define GNSS_NUM_VQ 2

static struct virtqueue *gnss_vqs[GNSS_NUM_VQ];
static struct class *myclass;
static struct virtio_char_gnss_dev* mydev;
static const unsigned int features[] = {
};

struct gnss_buf {
	char *buf;

	/* size of the buffer in *buf above */
	size_t size;

	/* used length of the buffer */
	size_t len;
	/* offset in the buf from which to consume data */
	size_t offset;

};

static struct gnss_buf *alloc_buf(size_t buf_size) {
    struct gnss_buf *gbuf;

    gbuf = kmalloc(sizeof(*gbuf), GFP_KERNEL);
    gbuf->buf = kmalloc(buf_size, GFP_KERNEL);
    gbuf->size = buf_size;
    return gbuf;
}

static void free_buf(struct gnss_buf* gbuf) {
    kfree(gbuf->buf);
    kfree(gbuf);
}


static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GNSS, VIRTIO_DEV_ANY_ID },
	{ 0 },
};
MODULE_DEVICE_TABLE(virtio, id_table);

static int gnss_tx_virtio(struct gnss_buf *gbuf)
{
	struct scatterlist sg[1];
	int err;

	sg_init_one(sg, gbuf->buf, gbuf->size);
	err = virtqueue_add_outbuf(gnss_vqs[GNSS_VQ_TX], sg, 1, gbuf, GFP_ATOMIC);
	if (err) {
		return err;
    }
	virtqueue_kick(gnss_vqs[GNSS_VQ_TX]);
	return 0;
}

static void kick() {
	struct scatterlist sg;

    char *buf;

    buf = kmalloc(1024, GFP_KERNEL);
    size_t size = 1024;

	sg_init_one(&sg, buf, size);

	/* There should always be room for one buffer. */
	virtqueue_add_inbuf(gnss_vqs[GNSS_VQ_RX], &sg, 1, buf, GFP_KERNEL);

	virtqueue_kick(gnss_vqs[GNSS_VQ_RX]);
}

static void gnss_virtio_rx_done(struct virtqueue *vq) {
	/* We can get spurious callbacks, e.g. shared IRQs + virtio_pci. */
    char *buf;
	if (!(buf=virtqueue_get_buf(vq, &mydev->data_avail)))
		return;

    gnss_insert_raw(mydev->gdev, buf, mydev->data_avail);
    kfree(buf);

    // ready for now data
    kick();
	//complete(&mydev->have_data);
}

static void gnss_virtio_tx_done(struct virtqueue *vq)
{
	unsigned int len;
	struct gnss_buf *gbuf;

	while ((gbuf = virtqueue_get_buf(vq, &len))) {
        free_buf(gbuf);
    }
}

static int init_vqs(struct virtio_device *vdev)
{
	vq_callback_t *callbacks[GNSS_NUM_VQ] = {
		[GNSS_VQ_RX] = gnss_virtio_rx_done,
		[GNSS_VQ_TX] = gnss_virtio_tx_done,
	};
	const char *names[GNSS_NUM_VQ] = {
		[GNSS_VQ_RX] = "input",
		[GNSS_VQ_TX] = "output",
	};

	return virtio_find_vqs(vdev, GNSS_NUM_VQ,
			       gnss_vqs, callbacks, names, NULL);
}

int gnss_char_fops_open(struct inode *inode, struct file * filp) {
    return 0;
}

int gnss_char_fops_release(struct inode *inode, struct file * filp) {
    return 0;
}

/* The host will fill any buffer we give it with data. */
static void register_buffer(char *buf, size_t size)
{
	struct scatterlist sg;

	sg_init_one(&sg, buf, size);

	/* There should always be room for one buffer. */
	virtqueue_add_inbuf(gnss_vqs[GNSS_VQ_RX], &sg, 1, buf, GFP_KERNEL);

	virtqueue_kick(gnss_vqs[GNSS_VQ_RX]);
}


static ssize_t gnss_char_fops_read(struct file *filp, char __user *ubuf,
			      size_t count, loff_t *offp)
{
    int ret;
    struct gnss_buf *gbuf;

    gbuf = alloc_buf(count);
	if (!mydev->busy) {
		mydev->busy = true;
		reinit_completion(&mydev->have_data);
		register_buffer(gbuf->buf, count);
	}

	ret = wait_for_completion_killable(&mydev->have_data);
	if (ret < 0) {
        free_buf(gbuf);
		return ret;
    }

	mydev->busy = false;

    copy_to_user(ubuf, gbuf->buf, mydev->data_avail);
    
    free_buf(gbuf);

	return mydev->data_avail;

}

static ssize_t gnss_char_fops_write(struct file *filp, const char __user *ubuf,
			       size_t count, loff_t *offp) {
    struct gnss_buf *gbuf;
    ssize_t ret;

    gbuf = alloc_buf(count);

    copy_from_user(gbuf->buf, ubuf, count);

    gnss_tx_virtio(gbuf);

    return count;
}

static const struct file_operations gnss_char_fops = {
	.owner = THIS_MODULE,
	.open  = gnss_char_fops_open,
	.release = gnss_char_fops_release,
	.read  = gnss_char_fops_read,
	.write = gnss_char_fops_write,
/*
	.poll  = gnss_char_fops_poll,
*/
};

static int virtio_gnss_probe(struct virtio_device *vdev)
{
    int ret;

    mydev->vdev = vdev;
    vdev->priv = mydev;

    ret = init_vqs(vdev);
    if (ret) {
    } else {
    }

    init_completion(&mydev->have_data);

    return 0;
}

static void virtio_gnss_remove(struct virtio_device *vdev)
{
    complete(&mydev->have_data);
}

#ifdef CONFIG_PM_SLEEP
static int virtio_gnss_freeze(struct virtio_device *vdev)
{
    return 0;
}
static int virtio_gnss_restore(struct virtio_device *vdev)
{
    return 0;
}
#endif

static struct virtio_driver virtio_char_gnss_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtio_gnss_probe,
	.remove =	virtio_gnss_remove,
#ifdef CONFIG_PM_SLEEP
	.freeze =	virtio_gnss_freeze,
	.restore =	virtio_gnss_restore,
#endif
};


static int char_open(struct gnss_device *gdev)
{
	struct virtio_char_gnss_dev *chardev = gnss_get_drvdata(gdev);

    kick();
	return 0;

}


static void char_close(struct gnss_device *gdev)
{
	struct virtio_char_gnss_dev *chardev = gnss_get_drvdata(gdev);

    /* TODO:
       maybe shutdown the thread that feeds the data */

}

static int char_write_raw(struct gnss_device *gdev,
		const unsigned char *buf, size_t count)
{
    struct gnss_buf *gbuf;
    ssize_t ret;

    gbuf = alloc_buf(count);

    memcpy(gbuf->buf, buf, count);

    gbuf->buf[count] = '\0';
    gnss_tx_virtio(gbuf);

	return count;
}



static const struct gnss_operations my_ops = {
	.open		= char_open,
	.close		= char_close,
	.write_raw	= char_write_raw,
};


static void register_gnss_device() {
    struct gnss_device* gdev;

    gdev = gnss_allocate_device(mydev->dev);

	gdev->ops = &my_ops;
	gnss_set_drvdata(gdev, mydev);

    mydev->gdev = gdev;

   gnss_register_device(gdev);

}

static int __init gnss_virtio_init(void)
{

    struct device* myvdev, *childdev, *granddev;
	dev_t devt;

    myclass = class_create(THIS_MODULE, "virtio-gnss-char-class");

    mydev = kmalloc(sizeof(*mydev), GFP_KERNEL);

    register_virtio_driver(&virtio_char_gnss_driver);

    register_gnss_device();
	return 0;
}

static void __exit gnss_virtio_exit(void)
{

    kfree(mydev);
}

module_init(gnss_virtio_init);
module_exit(gnss_virtio_exit);

MODULE_AUTHOR("Bo Hu <bohu@google.com>");
MODULE_DESCRIPTION("GNSS virtio serial driver");
MODULE_SOFTDEP("pre: gnss_serial");
MODULE_SOFTDEP("pre: virtio_console");
MODULE_SOFTDEP("pre: virtio_pci");
MODULE_LICENSE("GPL v2");
