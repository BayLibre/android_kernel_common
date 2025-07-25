/*
 * core.c - Core implementation of the nl_android_apf genetlink family
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright 2025 The Android Open Source Project
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <uapi/linux/nl_android_apf.h>

/*
 * Forward declarations for our command handlers.
 */
static int apf_get_caps(struct sk_buff *skb, struct genl_info *info);
static int apf_set_filter(struct sk_buff *skb, struct genl_info *info);
static int apf_get_filter(struct sk_buff *skb, struct genl_info *info);

/*
 * Attribute policy definitions
 */
static const struct nla_policy apf_policy[NL_ANDROID_APF_ATTR_MAX + 1] = {
	[NL_ANDROID_APF_ATTR_IFINDEX] = { .type = NLA_U32 },
	[NL_ANDROID_APF_ATTR_PROGRAM] = { .type = NLA_BINARY, .len = 4096 },
	[NL_ANDROID_APF_ATTR_FLAGS] = { .type = NLA_U32 },
};

/*
 * Command definitions
 */
static const struct genl_ops apf_ops[] = {
	{
		.cmd = NL_ANDROID_APF_CMD_GET_CAPABILITIES,
		.doit = apf_get_caps,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN */
	},
	{
		.cmd = NL_ANDROID_APF_CMD_SET_FILTER,
		.doit = apf_set_filter,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ANDROID_APF_CMD_GET_FILTER,
		.doit = apf_get_filter,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

/*
 * Genetlink family definition
 */
static struct genl_family apf_family = {
	.name = NL_ANDROID_APF_FAMILY_NAME,
	.version = NL_ANDROID_APF_VERSION,
	.maxattr = NL_ANDROID_APF_ATTR_MAX,
	.ops = apf_ops,
	.n_ops = ARRAY_SIZE(apf_ops),
	.module = THIS_MODULE,
};

/*
 * Command handler implementations
 */
static int apf_get_caps(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct sk_buff *msg;
	void *hdr;
	u32 version, max_program_size, flags;
	int ret;

	if (!info->attrs[NL_ANDROID_APF_ATTR_IFINDEX])
		return -EINVAL;

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NL_ANDROID_APF_ATTR_IFINDEX]));
	if (!dev)
		return -ENODEV;

	if (!dev->netdev_ops->ndo_apf_get_caps) {
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->ndo_apf_get_caps(dev, &version, &max_program_size, &flags);
	if (ret)
		goto out_put_dev;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  NL_ANDROID_APF_CMD_GET_CAPABILITIES);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	if (nla_put_u32(msg, NL_ANDROID_APF_ATTR_VERSION, version) ||
	    nla_put_u32(msg, NL_ANDROID_APF_ATTR_MAX_PROGRAM_SIZE, max_program_size) ||
	    nla_put_u32(msg, NL_ANDROID_APF_ATTR_FLAGS, flags)) {
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	genlmsg_end(msg, hdr);
	ret = genlmsg_reply(msg, info);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_set_filter(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	const u8 *program;
	u32 len, flags = 0;
	int ret;

	if (!info->attrs[NL_ANDROID_APF_ATTR_IFINDEX] ||
	    !info->attrs[NL_ANDROID_APF_ATTR_PROGRAM])
		return -EINVAL;

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NL_ANDROID_APF_ATTR_IFINDEX]));
	if (!dev)
		return -ENODEV;

	if (!dev->netdev_ops->ndo_apf_set_filter) {
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	program = nla_data(info->attrs[NL_ANDROID_APF_ATTR_PROGRAM]);
	len = nla_len(info->attrs[NL_ANDROID_APF_ATTR_PROGRAM]);
	if (info->attrs[NL_ANDROID_APF_ATTR_FLAGS])
		flags = nla_get_u32(info->attrs[NL_ANDROID_APF_ATTR_FLAGS]);

	ret = dev->netdev_ops->ndo_apf_set_filter(dev, program, len, flags);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_get_filter(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct sk_buff *msg;
	void *hdr;
	u8 *program = NULL;
	u32 len = 0;
	int ret;

	if (!info->attrs[NL_ANDROID_APF_ATTR_IFINDEX])
		return -EINVAL;

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NL_ANDROID_APF_ATTR_IFINDEX]));
	if (!dev)
		return -ENODEV;

	u32 version, max_len = 0, flags;

	if (!dev->netdev_ops->ndo_apf_get_caps ||
	    !dev->netdev_ops->ndo_apf_get_filter) {
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->ndo_apf_get_caps(dev, &version, &max_len, &flags);
	if (ret || !max_len)
		goto out_put_dev;

	program = kmalloc(max_len, GFP_KERNEL);
	if (!program) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	len = max_len;
	/* Second call gets the program */
	ret = dev->netdev_ops->ndo_apf_get_filter(dev, program, &len);
	if (ret)
		goto out_free;

	if (len > max_len) {
		ret = -EINVAL;
		goto out_free;
	}

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_free;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  NL_ANDROID_APF_CMD_GET_FILTER);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_free;
	}

	if (nla_put(msg, NL_ANDROID_APF_ATTR_PROGRAM, len, program) ||
	    nla_put_u32(msg, NL_ANDROID_APF_ATTR_PROGRAM_LEN, len)) {
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_free;
	}

	genlmsg_end(msg, hdr);
	ret = genlmsg_reply(msg, info);

out_free:
	kfree(program);
out_put_dev:
	dev_put(dev);
	return ret;
}

#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/apf_interpreter.h>

/*
 * This is called by the APF interpreter.
 * The 'ctx' is the net_device pointer passed from the driver's call to apf_run.
 */
uint8_t *apf_allocate_buffer(void *ctx, uint32_t size)
{
	/*
	 * For simplicity, just use kmalloc. A real implementation might
	 * want a more sophisticated buffer management strategy.
	 */
	return kmalloc(size, GFP_ATOMIC);
}
EXPORT_SYMBOL(apf_allocate_buffer);

/*
 * This is called by the APF interpreter.
 */
int apf_transmit_buffer(void *ctx, uint8_t *ptr, uint32_t len, uint8_t dscp)
{
	struct net_device *dev = (struct net_device *)ctx;
	struct sk_buff *skb;

	if (len == 0) {
		kfree(ptr);
		return 0;
	}

	/*
	 * Create an SKB around the buffer. We assume the interpreter has
	 * constructed a full L2 frame.
	 */
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		kfree(ptr);
		return -ENOMEM;
	}

	memcpy(skb_put(skb, len), ptr, len);
	kfree(ptr); /* Free the original buffer */

	skb_reset_mac_header(skb);
	skb->dev = dev;

	struct ethhdr *eth = (struct ethhdr *)skb->data;
	skb->protocol = eth->h_proto;

	if (dev_queue_xmit(skb) < 0) {
		netdev_warn(dev, "apf_transmit_buffer: failed to transmit skb\n");
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(apf_transmit_buffer);

/*
 * Module initialization and exit
 */
static int __init apf_init(void)
{
	return genl_register_family(&apf_family);
}


static void __exit apf_exit(void)
{
	genl_unregister_family(&apf_family);
}

module_init(apf_init);
module_exit(apf_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Gemini");
MODULE_DESCRIPTION("Android Packet Filter (APF) subsystem");
