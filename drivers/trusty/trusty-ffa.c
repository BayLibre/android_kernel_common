// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ARM Ltd.
 */

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/trusty/smcall.h>
#include <linux/arm_ffa.h>
#include <linux/trusty/trusty.h>

#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

#include "trusty-ffa.h"
#include "trusty-private.h"

static int __trusty_ffa_share_memory(struct device *dev, u64 *id,
				     struct scatterlist *sglist,
				     unsigned int nents, pgprot_t pgprot,
				     u64 tag, bool share)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));
	int ret;
	struct scatterlist *sg;
	size_t count;
	struct ffa_device *ffa_dev = to_ffa_dev(s->ffa->dev);
	const struct ffa_ops *ffa_ops = s->ffa->ops;
	struct ffa_mem_region_attributes ffa_mem_attr;
	struct ffa_mem_ops_args ffa_mem_args;

	if (nents < 1) {
		dev_warn(s->dev, "no sg entries to map!\n");
		return -EINVAL;
	}

	count = dma_map_sg(dev, sglist, nents, DMA_BIDIRECTIONAL);
	if (count != nents) {
		dev_err(s->dev, "failed to dma map sg_table\n");
		return -EINVAL;
	}

	sg = sglist;

	mutex_lock(&s->ffa->share_memory_msg_lock);

	ffa_mem_attr.receiver = ffa_dev->vm_id;
	ffa_mem_attr.attrs = FFA_MEM_RW;

	ffa_mem_args.use_txbuf = 1;
	ffa_mem_args.tag = tag;
	ffa_mem_args.attrs = &ffa_mem_attr;
	ffa_mem_args.nattrs = 1;
	ffa_mem_args.sg = sg;
	ffa_mem_args.flags = 0;

	if (share)
		ret = ffa_ops->mem_ops->memory_share(&ffa_mem_args);
	else
		ret = ffa_ops->mem_ops->memory_lend(&ffa_mem_args);

	mutex_unlock(&s->ffa->share_memory_msg_lock);

	if (ret) {
		dev_err(s->dev, "memory %s failed %d", share ? "share" : "lend", ret);

		dma_unmap_sg(dev, sglist, nents, DMA_BIDIRECTIONAL);
		return ret;
	}

	*id = ffa_mem_args.g_handle;
	return 0;
}

static int trusty_ffa_share_memory(struct device *dev, u64 *id,
				   struct scatterlist *sglist,
				   unsigned int nents, pgprot_t pgprot, u64 tag)
{
	return __trusty_ffa_share_memory(dev, id, sglist, nents, pgprot, tag,
					 true);
}

static int trusty_ffa_lend_memory(struct device *dev, u64 *id,
				  struct scatterlist *sglist,
				  unsigned int nents, pgprot_t pgprot, u64 tag)
{
	return __trusty_ffa_share_memory(dev, id, sglist, nents, pgprot, tag,
					 false);
}

static int trusty_ffa_reclaim_memory(struct device *dev, u64 id,
				     struct scatterlist *sglist,
				     unsigned int nents)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));
	int ret = 0;
	const struct ffa_ops *ffa_ops = s->ffa->ops;

	if (WARN_ON(nents < 1))
		return -EINVAL;

	mutex_lock(&s->ffa->share_memory_msg_lock);

	ret = ffa_ops->mem_ops->memory_reclaim(id, 0);

	mutex_unlock(&s->ffa->share_memory_msg_lock);

	if (ret != 0)
		return ret;

	dma_unmap_sg(dev, sglist, nents, DMA_BIDIRECTIONAL);

	return 0;
}

static const struct trusty_mem_ops trusty_ffa_mem_ops = {
	.desc = &trusty_ffa_transport,
	.trusty_share_memory = &trusty_ffa_share_memory,
	.trusty_lend_memory = &trusty_ffa_lend_memory,
	.trusty_reclaim_memory = &trusty_ffa_reclaim_memory,
};

static const struct ffa_device_id trusty_ffa_device_id[] = {
	/*
	 * Trusty UID: RFC-4122 compliant UUID version 4
	 * 40ee25f0-a2bc-304c-8c4ca173c57d8af1
	 */
	{ UUID_INIT(0x40ee25f0, 0xa2bc, 0x304c,
		    0x8c, 0x4c, 0xa1, 0x73, 0xc5, 0x7d, 0x8a, 0xf1) },
	{}
};

static int trusty_ffa_dev_match(struct device *dev, const void *uuid)
{
	struct ffa_device *ffa_dev;

	ffa_dev = to_ffa_dev(dev);
	if (uuid_equal(&ffa_dev->uuid, uuid))
		return 1;

	return 0;
}

static struct ffa_device *trusty_ffa_dev_find(void)
{
	const void *data;
	struct device *dev;

	/* currently only one trusty instance is probed */
	data = &trusty_ffa_device_id[0].uuid;

	dev = bus_find_device(&ffa_bus_type, NULL, data, trusty_ffa_dev_match);
	if (dev)
		return to_ffa_dev(dev);

	return NULL;
}

static int trusty_ffa_link_supplier(struct device *c_dev, struct device *s_dev)
{
	if (!c_dev || !s_dev)
		return -EINVAL;

	if (!device_link_add(c_dev, s_dev, DL_FLAG_AUTOREMOVE_CONSUMER))
		return -ENODEV;

	return 0;
}

/*
 * called from trusty probe
 */
static int trusty_ffa_transport_setup(struct device *dev)
{
	int rc;
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));
	struct trusty_ffa_state *ffa_state;
	struct ffa_device *ffa_dev;

	/* ffa transport not required for lower api versions */
	if (s->api_version != 0 && s->api_version < TRUSTY_API_VERSION_MEM_OBJ)
		return -EINVAL;

	ffa_dev = trusty_ffa_dev_find();
	if (!ffa_dev) {
		dev_dbg(dev, "FFA: Trusty device not found defer probe\n");
		return -EPROBE_DEFER;
	}

	ffa_state = ffa_dev_get_drvdata(ffa_dev);
	if (!ffa_state)
		return -EINVAL;

	rc = trusty_ffa_link_supplier(dev, &ffa_dev->dev);
	if (rc != 0)
		return rc;

	/* FFA used only for memory sharing operations */
	if (s->api_version == TRUSTY_API_VERSION_MEM_OBJ) {
		s->ffa = ffa_state;
		s->mem_ops = &trusty_ffa_mem_ops;
		return 0;
	}

	return -EINVAL;
}

static void trusty_ffa_transport_cleanup(struct device *dev)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	/* ffa transport not setup for lower api versions */
	if (s->api_version < TRUSTY_API_VERSION_MEM_OBJ)
		return;

	put_device(s->ffa->dev);

	s->ffa = NULL;
	s->mem_ops = NULL;
}

static int trusty_ffa_probe(struct ffa_device *ffa_dev)
{
	struct trusty_ffa_state *s;
	u32 ffa_drv_version;

	/* check ffa driver version compatibility */
	ffa_drv_version = ffa_dev->ops->info_ops->api_version_get();
	if (TO_TRUSTY_FFA_MAJOR(ffa_drv_version) != TRUSTY_FFA_VERSION_MAJOR ||
	    TO_TRUSTY_FFA_MINOR(ffa_drv_version) < TRUSTY_FFA_VERSION_MINOR)
		return -EINVAL;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->dev = &ffa_dev->dev;
	s->ops = ffa_dev->ops;
	mutex_init(&s->share_memory_msg_lock);
	ffa_dev_set_drvdata(ffa_dev, s);

	ffa_dev->ops->msg_ops->mode_32bit_set(ffa_dev);

	return 0;
}

static void trusty_ffa_remove(struct ffa_device *ffa_dev)
{
	struct trusty_ffa_state *s;

	s = ffa_dev_get_drvdata(ffa_dev);

	mutex_destroy(&s->share_memory_msg_lock);
	memzero_explicit(s, sizeof(struct trusty_ffa_state));
	kfree(s);
}

static struct ffa_driver trusty_ffa_driver = {
	.name = "trusty-ffa",
	.probe = trusty_ffa_probe,
	.remove = trusty_ffa_remove,
	.id_table = trusty_ffa_device_id,
};

static int __init trusty_ffa_transport_init(void)
{
	if (IS_REACHABLE(CONFIG_ARM_FFA_TRANSPORT))
		return ffa_register(&trusty_ffa_driver);
	else
		return -ENODEV;
}

static void __exit trusty_ffa_transport_exit(void)
{
	if (IS_REACHABLE(CONFIG_ARM_FFA_TRANSPORT))
		ffa_unregister(&trusty_ffa_driver);
}

const struct trusty_transport_desc trusty_ffa_transport = {
	.name = "ffa",
	.setup = trusty_ffa_transport_setup,
	.cleanup = trusty_ffa_transport_cleanup,
};

module_init(trusty_ffa_transport_init);
module_exit(trusty_ffa_transport_exit);
