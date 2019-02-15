/*
 * Copyright (C) 2019 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/trusty/smcall.h>
#include <linux/trusty/sm_err.h>
#include <linux/trusty/trusty.h>
#include <linux/trusty/trusty_custom_smc.h>

#define AF_HF AF_ECONET
#define PF_HF AF_HF

struct trusty_hafnium_req_msg {
	struct {
		u64 r0;
		u64 r1;
		u64 r2;
		u64 r3;
	} args;
};

struct trusty_hafnium_resp_msg {
	struct {
		u64 r0;
	} ret;
};

struct trusty_hafnium_state {
	struct trusty_custom_smc smc;
	struct device *dev;
	struct socket *sock;
};

static inline struct trusty_hafnium_state *
trusty_hafnium_smc_to_state(struct trusty_custom_smc *smc)
{
	BUG_ON(!smc);
	return container_of(smc, struct trusty_hafnium_state, smc);
}

static inline struct trusty_hafnium_state *
trusty_hafnium_dev_to_state(struct device *dev)
{
	return trusty_hafnium_smc_to_state(trusty_custom_smc_get_drvdata(dev));
}

static ulong trusty_hafnium_custom_smc(ulong r0, ulong r1, ulong r2, ulong r3,
				       struct trusty_custom_smc *data)
{
	struct trusty_hafnium_state *s = trusty_hafnium_smc_to_state(data);
	int ret;
	struct msghdr msg = {0};
	struct trusty_hafnium_req_msg req = {
		.args.r0 = r0,
		.args.r1 = r1,
		.args.r2 = r2,
		.args.r3 = r3,
	};
	struct kvec req_iov[] = {
		{
			.iov_base = &req,
			.iov_len = sizeof(req),
		},
	};
	struct trusty_hafnium_resp_msg resp;
	struct kvec resp_iov[] = {
		{
			.iov_base = &resp,
			.iov_len = sizeof(resp),
		},
	};

	if (WARN_ON(irqs_disabled())) {
		dev_err(s->dev, "%s: interrupts disabled, not supported by hafnium\n",
			__func__);
		return SM_ERR_PANIC;
	}

	ret = kernel_sendmsg(s->sock, &msg, req_iov, ARRAY_SIZE(req_iov),
			     sizeof(req));
	if (ret) {
		dev_err(s->dev, "kernel_sendmsg failed: %d\n", ret);
		return SM_ERR_INTERNAL_FAILURE;
	}

	ret = kernel_recvmsg(s->sock, &msg, resp_iov, ARRAY_SIZE(resp_iov),
			     sizeof(resp), 0);
	if (ret != sizeof(resp)) {
		dev_err(s->dev, "kernel_recvmsg failed: %d\n", ret);
		return SM_ERR_INTERNAL_FAILURE;
	}

	return resp.ret.r0;
}
static int trusty_hafnium_remove_child(struct device *dev, void *data)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int trusty_hafnium_probe(struct platform_device *pdev)
{
	int ret;
	struct socket *sock;
	struct trusty_hafnium_state *s;

	struct sockaddr_hf {
		sa_family_t family;
		uint32_t vm_id;
		uint64_t port;
	} saddr;

	ret = sock_create(PF_HF, SOCK_DGRAM, 0, &sock);
	if (ret == -EAFNOSUPPORT) {
		dev_warn(&pdev->dev, "no hafnium socket support, defer probe\n");
		return -EPROBE_DEFER;
	}
	if (ret) {
		dev_err(&pdev->dev, "sock_create failed: %d\n", ret);
		goto err_sock_create;
	}

	saddr.family = AF_HF;
	saddr.vm_id = 1; /* TODO: get from device tree */
	saddr.port = 0; /* ? */

	ret = kernel_connect(sock, (struct sockaddr *)&saddr, sizeof(saddr), 0);
	if (ret) {
		dev_err(&pdev->dev, "kernel_connect failed: %d\n", ret);
		goto err_connect;
	}

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		goto err_allocate_state;
	}

	s->sock = sock;
	s->smc.smc = trusty_hafnium_custom_smc;
	s->dev = &pdev->dev;
	trusty_custom_smc_set_drvdata(&pdev->dev, &s->smc);
	dev_info(s->dev, "hafnium socket ready\n");

	ret = of_platform_populate(s->dev->of_node, NULL, NULL, s->dev);
	if (ret < 0) {
		dev_err(s->dev, "Failed to add children: %d\n", ret);
		device_for_each_child(s->dev, NULL, trusty_hafnium_remove_child);
		goto err_of_platform_populate;
	}

	return 0;

err_of_platform_populate:
	kfree(s);
err_allocate_state:
err_connect:
	sock_release(sock);
err_sock_create:
	dev_err(&pdev->dev, "Probe failed: %d\n", ret);
	return ret;
}
static int trusty_hafnium_remove(struct platform_device *pdev)
{
	struct trusty_hafnium_state *s = trusty_hafnium_dev_to_state(&pdev->dev);

	device_for_each_child(&pdev->dev, NULL, trusty_hafnium_remove_child);
	sock_release(s->sock);
	kfree(s);

	return 0;
}

static const struct of_device_id trusty_hafnium_of_match[] = {
	{ .compatible = "android,trusty-hafnium-v1", },
	{},
};

static struct platform_driver trusty_hafnium_driver = {
	.probe = trusty_hafnium_probe,
	.remove = trusty_hafnium_remove,
	.driver	= {
		.name = "trusty-hafnium",
		.owner = THIS_MODULE,
		.of_match_table = trusty_hafnium_of_match,
	},
};

static int __init trusty_hafnium_driver_init(void)
{
	return platform_driver_register(&trusty_hafnium_driver);
}

static void __exit trusty_hafnium_driver_exit(void)
{
	platform_driver_unregister(&trusty_hafnium_driver);
}

subsys_initcall(trusty_hafnium_driver_init);
module_exit(trusty_hafnium_driver_exit);
