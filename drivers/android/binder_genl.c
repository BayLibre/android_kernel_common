// SPDX-License-Identifier: GPL-2.0-only
/* binder_genl.c
 *
 * Android IPC Subsystem
 *
 * Copyright (C) 2024 Google, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "binder_genl.h"

/**
 * Defines what kinds of binder transactions should be reported.
 */
static uint32_t binder_report_flags;

/**
 * Only the process that enables binder_report would receive binder reports.
 */
static pid_t binder_report_pid;

static const struct nla_policy binder_report_policy[BINDER_GENL_ATTR_MAX + 1] = {
	[BINDER_GENL_ATTR_FLAGS]	= { .type = NLA_U32 },
};

static struct genl_family binder_gnl_family;

static int binder_cmd_reply(struct genl_info *info, uint32_t flags)
{
	int len;
	struct sk_buff *skb;
	void *hdr;

	pr_info("binder_cmd_reply: %d\n", flags);

	len = sizeof(flags);
	skb = genlmsg_new(len, GFP_KERNEL);
	if (!skb) {
		pr_err("Failed to alloc binder genl message\n");
		return -ENOMEM;
	}

	hdr = genlmsg_put(skb, binder_report_pid, 0, &binder_gnl_family, 0,
			BINDER_GENL_CMD_REPLY);
	if (!hdr) {
		pr_err("Failed to set binder genl header\n");
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	if (nla_put(skb, BINDER_GENL_ATTR_FLAGS, len, &flags)) {
		genlmsg_cancel(skb, hdr);
		nlmsg_free(skb);
		return -EMSGSIZE;
	}

	genlmsg_end(skb, hdr);

	if (genlmsg_reply(skb, info)) {
		pr_err("Failed to send binder genl message\n");
		return -EFAULT;
	}

	pr_info("Binder genl cmd replied\n");
	return 0;
}

static int binder_genl_cmd_doit(struct sk_buff *skb, struct genl_info *info) {
	struct nlattr *nla;

	pr_info("binder_genl_cmd_doit\n");
	nla = info->attrs[BINDER_GENL_ATTR_FLAGS];
	if (!nla) {
		pr_err("Invalid binder genl message\n");
		return EINVAL;
	}
	binder_report_flags = nla_get_u32(nla);
	binder_report_pid = nlmsg_hdr(skb)->nlmsg_pid;
	pr_info("Binder report enabled with flags 0x%08x from pid %d\n",
			binder_report_flags, binder_report_pid);

	return binder_cmd_reply(info, binder_report_flags);
}

static struct genl_small_ops binder_genl_ops[] = {
	{
		.cmd = BINDER_GENL_CMD_ENABLE,
		.doit = binder_genl_cmd_doit,
	}
};

static struct genl_family binder_gnl_family = {
	.name = BINDER_GENL_FAMILY_NAME,
	.version = BINDER_GENL_VERSION,
	.maxattr = BINDER_GENL_ATTR_MAX,
	.policy	= binder_report_policy,
	.small_ops = binder_genl_ops,
	.n_small_ops = ARRAY_SIZE(binder_genl_ops),
};

int init_binder_netlink(void)
{
	int ret = genl_register_family(&binder_gnl_family);
	if (ret) {
		pr_err("Failed to register binder genl\n");
		return ret;
	}

	pr_info("#8: Binder genl v%d inited\n", BINDER_GENL_VERSION);

	return 0;
}

inline bool binder_report_enabled(uint32_t mask)
{
	return (binder_report_flags & mask) != 0;
}

void binder_send_report(struct binder_context *context, int err,
		int from_pid, int from_tid, int to_pid, int to_tid,
		int reply, int flags, int code, int size)
{
	int len, minor;
	struct binder_device *device;
	struct binder_report report;
	struct sk_buff *skb;
	void *hdr;

	device = container_of(context, struct binder_device, context);
	minor = device->miscdev.minor;
	pr_info("binder_send_report: %d %d 0x%08x %d:%d -> %d:%d %d 0x08%x %d %d\n",
	minor, err, err, from_pid, from_tid, to_pid, to_pid, reply, flags, code, size);

	len = sizeof(struct binder_report);
	skb = genlmsg_new(len, GFP_KERNEL);
	if (!skb) {
		pr_err("Failed to alloc binder genl message\n");
		return;
	}

	hdr = genlmsg_put(skb, binder_report_pid, 0, &binder_gnl_family, 0,
			BINDER_GENL_CMD_REPORT);
	if (!hdr) {
		pr_err("Failed to set binder genl header\n");
		kfree_skb(skb);
		return;
	}

	report.minor = minor;
	report.err = err;
	report.from_pid = from_pid;
	report.from_tid = from_tid;
	report.to_pid = to_pid;
	report.to_tid = to_tid;
	report.reply = reply;
	report.flags = flags;
	report.code = code;
	report.size = size;

	if (nla_put(skb, BINDER_GENL_ATTR_REPORT, len, &report)) {
		genlmsg_cancel(skb, hdr);
		nlmsg_free(skb);
		return;
	}

	genlmsg_end(skb, hdr);

	if (genlmsg_unicast(&init_net, skb, binder_report_pid))
		pr_err("Failed to send binder genl message\n");

	pr_info("Binder genl report sent\n");
}
