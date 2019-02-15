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

#include <hf/call.h>
#include <linux/delay.h>
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
#include <net/sock.h>

#define AF_HF AF_ECONET
#define PF_HF AF_HF

struct trusty_hafnium_req_msg {
	u64 id;
	struct {
		u64 r0;
		u64 r1;
		u64 r2;
		u64 r3;
	} args;
};

struct trusty_hafnium_resp_msg {
	u64 id;
	struct {
		u64 r0;
	} ret;
};

struct trusty_hafnium_state {
	struct trusty_custom_smc smc;
	struct device *dev;
	struct socket *sock;
	void (*sk_data_ready)(struct sock *sk);
	enum {
		IN_SMC_IDLE,
		IN_SMC_GOT_MSG,
		IN_SMC_WAIT_MSG,
	} in_smc;
	struct device *trusty_dev;
	uint32_t vm_id;
	u64 last_msg_id;
	struct mutex smc_lock;
	struct mutex state_lock;
	bool internal_failure;
};

static bool is_trusty_hafnium_dev(struct device *dev);

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

static void trusty_hafnium_data_ready(struct sock *sk)
{
	struct trusty_hafnium_state *s = sk->sk_user_data;

	mutex_lock(&s->state_lock);
	dev_dbg(s->dev, "%s: in_smc %d, trusty_dev %d\n", __func__,
		s->in_smc, !!s->trusty_dev);
	if (s->in_smc != IN_SMC_WAIT_MSG && s->trusty_dev) {
		trusty_enqueue_nop(s->trusty_dev, NULL);
	} else {
		/*
		 * Clear in_smc here since another message could come right
		 * after the reply to the smc call, and it would not be an smc
		 * reply.
		 */
		s->in_smc = IN_SMC_GOT_MSG;
	}
	mutex_unlock(&s->state_lock);

	s->sk_data_ready(sk);
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

	mutex_lock(&s->smc_lock);
	mutex_lock(&s->state_lock);
	s->in_smc = IN_SMC_WAIT_MSG;
	mutex_unlock(&s->state_lock);

	if (s->internal_failure) {
		dev_err(s->dev, "previous failure, wait\n");
		msleep(10000);
	}

	ret = kernel_recvmsg(s->sock, &msg, resp_iov,
		     ARRAY_SIZE(resp_iov), sizeof(resp), MSG_DONTWAIT);
	if (ret != -EAGAIN && ret != 0) {
		dev_err(s->dev, "no message expected, kernel_recvmsg returned: %d\n", ret);
		if (ret == sizeof(resp)) {
			dev_err(s->dev, "msg id %lld\n", resp.id);
		}
	}

	s->last_msg_id = req.id = s->last_msg_id + 1;

	dev_dbg(s->dev, "smc 0x%x 0x%x 0x%x 0x%x, id %lld\n",
		r0, r1, r2, r3, req.id);

	while (true) {
		ret = kernel_sendmsg(s->sock, &msg, req_iov,
				     ARRAY_SIZE(req_iov), sizeof(req));
		if (ret != -EAGAIN)
			break;
		dev_err(s->dev, "kernel_sendmsg failed: %d, retry\n", ret);
		msleep(1000);
	}
	if (ret) {
		dev_err(s->dev, "kernel_sendmsg failed: %d\n", ret);
		goto err;
	}

	do {
		ret = kernel_recvmsg(s->sock, &msg, resp_iov,
				     ARRAY_SIZE(resp_iov), sizeof(resp), 0);
		dev_dbg(s->dev, "kernel_recvmsg ret %d\n", ret);
	} while (ret == 0);
	if (ret != sizeof(resp)) {
		dev_err(s->dev, "kernel_recvmsg failed: %d\n", ret);
		goto err;
	}
	if (resp.id != req.id) {
		dev_info(s->dev,
			 "smc 0x%x 0x%x 0x%x 0x%x, resp id %lld != %lld\n",
			 r0, r1, r2, r3, resp.id, req.id);
	} else {
		dev_dbg(s->dev,
			"smc 0x%x 0x%x 0x%x 0x%x, resp id %lld == %lld\n",
			r0, r1, r2, r3, resp.id, req.id);
	}

	mutex_lock(&s->state_lock);
	s->in_smc = IN_SMC_IDLE;
	mutex_unlock(&s->state_lock);

	mutex_unlock(&s->smc_lock);

	return resp.ret.r0;

err:
	s->internal_failure = true;
	mutex_lock(&s->state_lock);
	s->in_smc = IN_SMC_IDLE;
	mutex_unlock(&s->state_lock);
	mutex_unlock(&s->smc_lock);
	return SM_ERR_INTERNAL_FAILURE;
}

static int trusty_hafnium_share_memory(struct trusty_custom_smc *data,
				       phys_addr_t paddr, size_t size,
				       uint32_t flags)
{
	struct trusty_hafnium_state *s = trusty_hafnium_smc_to_state(data);
	dev_dbg(s->dev,
		"trusty_hafnium_share_memory: vm %d paddr %pa, size %zd, flags 0x%x \n",
		s->vm_id, &paddr, size, flags);
	if (/*WARN_ON*/(!PAGE_ALIGNED(size)))
		size = PAGE_ALIGN(size);
	return hf_share_memory(s->vm_id, paddr, size, HF_MEMORY_SHARE);
}

static int trusty_hafnium_remove_child(struct device *dev, void *data)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int trusty_hafnium_irq_probe(struct platform_device *pdev)
{
	struct device *trusty_dev = pdev->dev.parent;
	struct device *thdev = trusty_dev->parent;
	struct trusty_hafnium_state *s = trusty_hafnium_dev_to_state(thdev);
	if (!is_trusty_hafnium_dev(thdev)) {
		return -EINVAL;
	}
	mutex_lock(&s->state_lock);
	s->trusty_dev = trusty_dev;
	mutex_unlock(&s->state_lock);
	return 0;
}

static int trusty_hafnium_irq_remove(struct platform_device *pdev)
{
	struct device *trusty_dev = pdev->dev.parent;
	struct device *thdev = trusty_dev->parent;
	struct trusty_hafnium_state *s = trusty_hafnium_dev_to_state(thdev);
	if (!is_trusty_hafnium_dev(thdev)) {
		return -EINVAL;
	}
	mutex_lock(&s->state_lock);
	s->trusty_dev = NULL;
	mutex_unlock(&s->state_lock);
	return 0;
}

static int trusty_hafnium_probe(struct platform_device *pdev)
{
	int ret;
	struct socket *sock;
	struct trusty_hafnium_state *s;
	uint32_t vm_id;
	struct sockaddr_hf {
		sa_family_t family;
		uint32_t vm_id;
		uint64_t port;
	} saddr;

	ret = of_property_read_u32(pdev->dev.of_node, "vm-id", &vm_id);
	if (ret) {
		dev_err(&pdev->dev, "missing vm-id in device tree node\n");
		return ret;
	}

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
	saddr.vm_id = vm_id;
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
	s->sk_data_ready = sock->sk->sk_data_ready;
	s->vm_id = vm_id;
	s->smc.smc = trusty_hafnium_custom_smc;
	s->smc.share_memory = trusty_hafnium_share_memory;
	s->dev = &pdev->dev;
	mutex_init(&s->smc_lock);
	mutex_init(&s->state_lock);
	trusty_custom_smc_set_drvdata(&pdev->dev, &s->smc);

	sock->sk->sk_data_ready = trusty_hafnium_data_ready;
	sock->sk->sk_user_data = s;

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

static const struct of_device_id trusty_hafnium_irq_of_match[] = {
	{ .compatible = "android,trusty-hafnium-irq-v1", },
	{},
};

static struct platform_driver trusty_hafnium_irq_driver = {
	.probe = trusty_hafnium_irq_probe,
	.remove = trusty_hafnium_irq_remove,
	.driver = {
		.name = "trusty-hafnium-irq",
		.owner = THIS_MODULE,
		.of_match_table = trusty_hafnium_irq_of_match,
	},
};

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

static bool is_trusty_hafnium_dev(struct device *dev) {
	return !WARN_ON(dev->driver != &trusty_hafnium_driver.driver);
}

static int __init trusty_hafnium_driver_init(void)
{
	int ret;

	ret = platform_driver_register(&trusty_hafnium_driver);
	if (ret) {
		goto err_register_trusty_hafnium_driver;
	}

	ret = platform_driver_register(&trusty_hafnium_irq_driver);
	if (ret) {
		goto err_register_trusty_hafnium_irq_driver;
	}

	return 0;

err_register_trusty_hafnium_irq_driver:
	platform_driver_unregister(&trusty_hafnium_driver);
err_register_trusty_hafnium_driver:
	return ret;
}

static void __exit trusty_hafnium_driver_exit(void)
{
	platform_driver_unregister(&trusty_hafnium_irq_driver);
	platform_driver_unregister(&trusty_hafnium_driver);
}

subsys_initcall(trusty_hafnium_driver_init);
module_exit(trusty_hafnium_driver_exit);
