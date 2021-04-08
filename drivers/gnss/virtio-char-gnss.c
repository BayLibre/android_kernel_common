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
extern struct bus_type virtio_bus;

static char *vdevname;
module_param(vdevname, charp, 0644);
MODULE_PARM_DESC(vdevname, "virtio device to fine");

static char *childname;
module_param(childname, charp, 0644);
MODULE_PARM_DESC(childname, "virtio child device to fine");

/*
Over all plan

Alternative 1:
1, create a char dev, and the char dev can communicate
directly with host(crosvm/qemu), bidirectional.
host side should provide the backend (TODO)
2, register the char dev with gnss core system

Alternative 2:
similar to symlink gnss0 to virtio console port
1, create a char dev that simply proxy between
any read/write to virito console (hvcx)
2, register with gnss core system

*/
struct virtio_char_gnss_dev {
    struct virtio_device *vdev;
	struct cdev *cdev;
	struct device *dev;
    struct gnss_device * gdev;
	struct virtqueue *in_vq, *out_vq;
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
        dev_err(&mydev->vdev->dev, "fail to write calling %s %d here\n", __func__, __LINE__);
		return err;
    }
    dev_err(&mydev->vdev->dev, "success write vq %s %d here\n", __func__, __LINE__);
	virtqueue_kick(gnss_vqs[GNSS_VQ_TX]);
	return 0;
}

static void gnss_virtio_rx_done(struct virtqueue *vq) {
}

static void gnss_virtio_tx_done(struct virtqueue *vq)
{
	unsigned int len;
	struct gnss_buf *gbuf;

    dev_err(&mydev->vdev->dev, "calling %s %d here\n", __func__, __LINE__);
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

static ssize_t gnss_char_fops_read(struct file *filp, char __user *ubuf,
			      size_t count, loff_t *offp) {
    copy_to_user(ubuf, "hello", count > 5 ? 5 : count);
    return count;
}

static ssize_t gnss_char_fops_write(struct file *filp, const char __user *ubuf,
			       size_t count, loff_t *offp) {
    struct gnss_buf *gbuf;
    ssize_t ret;

    dev_err(&mydev->vdev->dev, "calling %s %d here\n", __func__, __LINE__);
    gbuf = alloc_buf(count);

    copy_from_user(gbuf->buf, ubuf, count);

    dev_err(&mydev->vdev->dev, "calling %s %d here with data %s\n", __func__, __LINE__,
            gbuf->buf);
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
        dev_err(&vdev->dev, "init vq failed with code %d calling %s %d here\n",ret, __func__, __LINE__);
    } else {
        dev_err(&vdev->dev, "init vq succeeded with code %d calling %s %d here\n",ret, __func__, __LINE__);
    }

    return 0;
}

static void virtio_gnss_remove(struct virtio_device *vdev)
{
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

static int name_match(struct device *dev, void *data)
{
	return strstr(dev_name(dev), data) != NULL;
}

static void lookup_virtio_devices() {
    struct device* myvdev, *childdev, *granddev;
    myvdev = vdevname ? bus_find_device_by_name(&virtio_bus, NULL, vdevname) : NULL;
    if (!myvdev) {
		dev_err(mydev->dev, "failed to fine device: %s\n", vdevname);
    } else {
        /* this can be found, e.g. virtio7 */
		dev_err(mydev->dev, "found device: %s\n", vdevname);

        /* find downwad from virtio7 to vport7p0 or vport7p1 */
        if (!childname) return;

	    childdev = device_find_child(myvdev, childname, name_match);
        if (childdev) {
		    dev_err(mydev->dev, "found child device: %s\n", childname);
        } else {
		    dev_err(mydev->dev, "cannot found child device: %s\n", childname);
        }
    }
}

static int char_open(struct gnss_device *gdev)
{
	struct virtio_char_gnss_dev *chardev = gnss_get_drvdata(gdev);

    /* TODO:
       maybe start a thread that feeds the data */

    char buf[32] = "hello world";

    gnss_insert_raw(gdev, buf, strlen(buf));
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
	struct virtio_char_gnss_dev *chardev = gnss_get_drvdata(gdev);
    /* TODO:
       maybe write something here*/
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
    mydev->chr_major = register_chrdev(0, "virtio-gnss-char-dev",
					     &gnss_char_fops);
    mydev->cdev = cdev_alloc();
    mydev->cdev->owner = THIS_MODULE;
    mydev->id = 0;
    devt = MKDEV(mydev->chr_major, mydev->id);
    mydev->cdev->ops = &gnss_char_fops;


    mydev->dev = device_create(myclass, NULL, devt, mydev, "virtio-gnss-char%u", mydev->id);

	cdev_add(mydev->cdev, devt, 1);
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
