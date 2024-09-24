// SPDX-License-Identifier: GPL-2.0+
/*
 * Virtio message transport - FFA based channel interface.
 *
 * Copyright (C) 2024 Google LLC and Linaro.
 *
 * This implements the channel interface for Virtio msg transport via FFA.
 */

#define pr_fmt(fmt) "virtio-msg-ffa: " fmt

#include <linux/arm_ffa.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/xarray.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>

#include "virtio_msg.h"

#define FFA_DIRECT_REQ_ARG_NUM 5
#define FFA_INVALID_MEM_HANDLE U64_MAX
#define FFA_CHANNEL_UUID       UUID_INIT(0xc5b82091, 0xd4fe, 0x48bb, \
		               0xb7, 0xe7, 0x4d, 0x24, 0x6e, 0xbb, 0x28, 0xbe)

struct virtio_msg_ffa_device {
	struct virtio_msg_device vmdev;
	struct ffa_device *ffa_dev;

	void __iomem *base;
	const char   *name;
};

#define to_virtio_msg_ffa_device(_vmdev) \
	container_of(_vmdev, struct virtio_msg_ffa_device, vmdev)

/*************************************************************************
static int tstee_shm_register(struct tee_context *ctx, struct tee_shm *shm,
			      struct page **pages, size_t num_pages,
			      unsigned long start __always_unused)
{
	struct tstee *tstee = tee_get_drvdata(ctx->teedev);
	struct ffa_device *ffa_dev = tstee->ffa_dev;
	struct ffa_mem_region_attributes mem_attr = {
		.receiver = tstee->ffa_dev->vm_id,
		.attrs = FFA_MEM_RW,
		.flag = 0,
	};
	struct ffa_mem_ops_args mem_args = {
		.attrs = &mem_attr,
		.use_txbuf = true,
		.nattrs = 1,
		.flags = 0,
	};
	struct ffa_send_direct_data ffa_data;
	struct sg_table sgt;
	u32 ffa_args[FFA_DIRECT_REQ_ARG_NUM] = {};
	int rc;

	rc = sg_alloc_table_from_pages(&sgt, pages, num_pages, 0,
				       num_pages * PAGE_SIZE, GFP_KERNEL);
	if (rc)
		return rc;

	mem_args.sg = sgt.sgl;
	rc = ffa_dev->ops->mem_ops->memory_share(&mem_args);
	sg_free_table(&sgt);
	if (rc)
		return rc;

	shm->sec_world_id = mem_args.g_handle;

	ffa_args[TS_RPC_CTRL_REG] =
			TS_RPC_CTRL_PACK_IFACE_OPCODE(TS_RPC_MGMT_IFACE_ID,
						      TS_RPC_OP_RETRIEVE_MEM);
	ffa_args[TS_RPC_RETRIEVE_MEM_HANDLE_LSW] =
			lower_32_bits(shm->sec_world_id);
	ffa_args[TS_RPC_RETRIEVE_MEM_HANDLE_MSW] =
			upper_32_bits(shm->sec_world_id);
	ffa_args[TS_RPC_RETRIEVE_MEM_TAG_LSW] = 0;
	ffa_args[TS_RPC_RETRIEVE_MEM_TAG_MSW] = 0;

	arg_list_to_ffa_data(ffa_args, &ffa_data);
	rc = ffa_dev->ops->msg_ops->sync_send_receive(ffa_dev, &ffa_data);
	if (rc) {
		(void)ffa_dev->ops->mem_ops->memory_reclaim(shm->sec_world_id,
							    0);
		return rc;
	}

	arg_list_from_ffa_data(&ffa_data, ffa_args);

	if (ffa_args[TS_RPC_RETRIEVE_MEM_RPC_STATUS] != TS_RPC_OK) {
		dev_err(&ffa_dev->dev, "shm_register rpc status: %d\n",
			ffa_args[TS_RPC_RETRIEVE_MEM_RPC_STATUS]);
		ffa_dev->ops->mem_ops->memory_reclaim(shm->sec_world_id, 0);
		return -EINVAL;
	}

	return 0;
}
**********************************************************************************/

static int virtio_msg_ffa_send(struct virtio_msg_device *vmdev,
				struct virtio_msg *request,
				struct virtio_msg *response)
{
	struct virtio_msg_ffa_device *vmfdev = to_virtio_msg_ffa_device(vmdev);
	struct ffa_device *ffa_dev = vmfdev->ffa_dev;
	int i, len = sizeof(struct virtio_msg) / sizeof(unsigned long);

        struct ffa_send_direct_data ffa_data = { };
        int rc;

	unsigned long *reqs = (unsigned long *) request;
	unsigned long *data = (unsigned long *) &ffa_data;
	for (i = 0; i < len; i++) {
		data[i] = reqs[i];
	}

	rc = ffa_dev->ops->msg_ops->sync_send_receive(ffa_dev, &ffa_data);
	if (rc) {
                pr_err("Unable to send direct FFA message rc %d\n", rc);
		return -ENOMEM;
	}

	if (!response)
		return -EINVAL;

	unsigned long *resps = (unsigned long *) response;
	for (i = 0; i < len; i++) {
		resps[i] = data[i];
	}

	return 0;
}

static const char *virtio_msg_ffa_bus_name(struct virtio_msg_device *vmdev)
{
	struct virtio_msg_ffa_device *vmfdev = to_virtio_msg_ffa_device(vmdev);

	return vmfdev->name;
}

static void virtio_msg_ffa_synchronize_cbs(struct virtio_msg_device *vmdev)
{
	struct virtio_msg_ffa_device *vmfdev = to_virtio_msg_ffa_device(vmdev);
	(void)vmfdev;
}

static void virtio_msg_ffa_release(struct virtio_msg_device *vmdev)
{
	struct virtio_msg_ffa_device *vmfdev = to_virtio_msg_ffa_device(vmdev);

	kfree(vmfdev);
}

static int virtio_msg_ffa_vqs_prepare(struct virtio_msg_device *vmdev)
{
	struct virtio_msg_ffa_device *vmfdev = to_virtio_msg_ffa_device(vmdev);

	(void)vmfdev;
	return 0;
}

static void virtio_msg_ffa_vqs_release(struct virtio_msg_device *vmdev)
{
	struct virtio_msg_ffa_device *vmfdev = to_virtio_msg_ffa_device(vmdev);

	(void)vmfdev;
}

static struct virtio_msg_ops vmf_ops = {
	.send = virtio_msg_ffa_send,
	.bus_name = virtio_msg_ffa_bus_name,
	.synchronize_cbs = virtio_msg_ffa_synchronize_cbs,
	.release = virtio_msg_ffa_release,
	.prepare_vqs = virtio_msg_ffa_vqs_prepare,
	.release_vqs = virtio_msg_ffa_vqs_release,
};

static int virtio_msg_ffa_probe(struct ffa_device *ffa_dev)
{
	struct virtio_msg_ffa_device *vmfdev;
	int ret;

	ffa_dev->ops->msg_ops->mode_32bit_set(ffa_dev);

	vmfdev = kzalloc(sizeof(*vmfdev), GFP_KERNEL);
	if (!vmfdev)
		return -ENOMEM;

	vmfdev->vmdev.vdev.dev.parent = &ffa_dev->dev;
	vmfdev->vmdev.ops = &vmf_ops;
	vmfdev->ffa_dev = ffa_dev;

	ret = dma_set_mask_and_coherent(&ffa_dev->dev, DMA_BIT_MASK(64));
	if (ret)
		ret = dma_set_mask_and_coherent(&ffa_dev->dev, DMA_BIT_MASK(32));
	if (ret)
		dev_warn(&ffa_dev->dev, "Failed to enable 64-bit or 32-bit DMA\n");

	ffa_dev_set_drvdata(ffa_dev, vmfdev);

	return virtio_msg_register(&vmfdev->vmdev);
}

static void virtio_msg_ffa_remove(struct ffa_device *ffa_dev)
{
	struct virtio_msg_ffa_device *vmfdev = ffa_dev->dev.driver_data;

	kfree(vmfdev);
}

static const struct ffa_device_id virtio_msg_ffa_device_ids[] = {
	{ FFA_CHANNEL_UUID },
	{}
};

static struct ffa_driver virtio_msg_ffa_driver = {
	.name = "virtio-msg-ffa",
	.probe = virtio_msg_ffa_probe,
	.remove = virtio_msg_ffa_remove,
	.id_table = virtio_msg_ffa_device_ids,
};
module_ffa_driver(virtio_msg_ffa_driver);

MODULE_DESCRIPTION("Virtio_MSG_FFA channel driver");
MODULE_LICENSE("GPL");
