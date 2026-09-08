/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/pm.h>
#include <linux/pm_domain.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <linux/vmalloc.h>
#include <dt-bindings/iopmp/zh-iopmp.h>
#include <asm/zh-iopmp.h>

#include "a210-pd.h"

static struct dentry *pd_debugfs_root;
static struct dentry *pd_pde;

/*
 * NOT looked up via dev_get_drvdata()/dev_set_drvdata() across separate
 * probe() attempts: the driver core's really_probe() explicitly resets a
 * device's drvdata to NULL whenever .probe() returns an error, as part of
 * its own failure cleanup - so a retried probe() can never see a previous
 * attempt's state that way. dev_set_drvdata() is still called every attempt
 * purely so the *rest* of this file (a210_add_one_domain(),
 * a210_init_pm_domains(), a210_pd_parse_regulators(), a210_pd_remove()) can
 * fetch it via dev_get_drvdata() during that same, single, still-in-flight
 * probe() call - only the entry point in a210_pd_probe() that decides
 * "fresh probe or retry" needs something the driver core won't touch.
 * There's only ever one "soc:a210-power-domain" device on this SoC, so a
 * plain singleton is correct here, not a multi-instance driver hazard.
 */
static struct a210_pd_soc *a210_pd_soc_singleton;

static inline struct a210_pm_domain *to_a210_pd(struct generic_pm_domain *domain)
{
	return container_of(domain, struct a210_pm_domain, pd);
}

static void bpc_config(struct device *dev, const char *str, void __iomem *base_addr, u32 bpc_ctrl)
{
    if ((bpc_ctrl & (1 << 0)) != 0) {
        dev_dbg(dev, "Enter %s bpc_config sw model...\n", str);
        writel(0x1, base_addr + 0x000); // 0x1 bypass
        writel(0x18, base_addr + 0x13c); // bpc 9000 ocgen &rset
    } else {
        dev_dbg(dev, "Enter %s bpc_config hw model...\n", str);
        writel(0x0, base_addr + 0x000);
    }
    writel(0x10101, base_addr + 0x004); // pwr venc bpc 3000| fence
}

/* Backstop against a wedged/never-triggered PCU hanging forever. */
#define PCU_INTR_TIMEOUT_US 10000

static int pcu_intr(struct device *dev, const char *str, void __iomem *base_addr)
{
    u32 data;
    int timeout = PCU_INTR_TIMEOUT_US;

    udelay(1);
    data = readl(base_addr + 0x2c); // read pcu intr
    while (data == 0 && --timeout > 0) {
        udelay(1);
        data = readl(base_addr + 0x2c); // read pcu intr
    }
    if (data == 0) {
        dev_err(dev, "%s pcu_intr timed out after %dus waiting for ack\n",
            str, PCU_INTR_TIMEOUT_US);
        return -ETIMEDOUT;
    }
    if (((data & (1 << 0)) != 0) || ((data & (1 << 3)) != 0)) {
        dev_dbg(dev, "%s pcu_intr accept\n", str);
    }
    if (((data & (1 << 1)) != 0) || ((data & (1 << 4)) != 0)) {
        dev_err(dev, "%s pcu_intr deny\n", str);
    }
    if (((data & (1 << 2)) != 0) || ((data & (1 << 5)) != 0)) {
        dev_err(dev, "%s pcu_intr timeout\n", str);
    }
    writel(data, base_addr + 0x28); // clr cpu intr

    return 0;
}

static int pcu_config(struct device *dev, const char *str,
		       void __iomem * base_addr, u32 pcu_ctrl, u32 state)
{
    writel(0x3f, base_addr + 0x24); // interrupt enable
    if ((pcu_ctrl & (1 << 0)) != 0) {
        dev_dbg(dev, "Enter %s pcu_config: pcu reg trigger...state=0x%x\n", str, state);
        writel((state & 0x1f), base_addr + 0x0c); // lpstate = power on
        writel(0x1, base_addr + 0x08); // lqreq
        return pcu_intr(dev, str, base_addr); // wait for accept
    }

    dev_dbg(dev, "Enter %s pcu_config: wait r2p trigger...\n", str);
    return 0;
}

static int a210_pd_power_switch(struct generic_pm_domain *domain, power_mode mode)
{
	struct a210_pm_domain *a210_pd = to_a210_pd(domain);
	struct a210_pd_soc *soc = a210_pd->soc;

	struct device *dev = soc->dev;
	struct regulator *regulator = soc->regulators[a210_pd->index];
	const char *name = domain->name;
	int ret;

	if (mode == ON && regulator != NULL) {
		ret = regulator_enable(regulator);
		if (ret) {
			dev_err(dev, "failed to regulator_enable for %s", name);
			return ret;
		}
	}

	/* config pca if needed */
	if (mode == ON && !IS_ERR(a210_pd->pca_base))
		writel(0x0, a210_pd->pca_base + 0x20);

	if (!IS_ERR(a210_pd->bpc_base))
		bpc_config(dev, name, a210_pd->bpc_base, BPC_HW_MODEL);
	if (!IS_ERR(a210_pd->pcu_base)) {
		ret = pcu_config(dev, name, a210_pd->pcu_base, PCU_REG_TRIGGER, mode);
		if (ret)
			return ret;
	}

	if (mode == OFF && regulator != NULL) {
		ret = regulator_disable(regulator);
		if (ret) {
			dev_err(dev, "failed to regulator_disable for %s", name);
			return ret;
		}
	}

	return 0;
}

static int a210_pd_power_off(struct generic_pm_domain *domain)
{
	struct a210_pm_domain *a210_pd = to_a210_pd(domain);
	int ret;

#ifdef CONFIG_ZH_IOPMP
	if(a210_pd->device_ids_count > 0)
		iopmp_disable(a210_pd->device_ids, a210_pd->device_ids_count);
#endif

	if (a210_pd->num_clks)
		clk_bulk_disable_unprepare(a210_pd->num_clks, a210_pd->clks);

	ret = reset_control_assert(a210_pd->reset);
	if (ret)
		return ret;

	return a210_pd_power_switch(domain, OFF);
}

static int a210_pd_power_on(struct generic_pm_domain *domain)
{
	struct a210_pm_domain *a210_pd = to_a210_pd(domain);
	int ret;

	ret = a210_pd_power_switch(domain, ON);
	if (ret)
		return ret;

	if (a210_pd->num_clks) {
		ret = clk_bulk_prepare_enable(a210_pd->num_clks, a210_pd->clks);
		if (ret)
			return ret;
	}

	ret = reset_control_deassert(a210_pd->reset);
	if (ret)
		return ret;

#ifdef CONFIG_ZH_IOPMP
	if(a210_pd->device_ids_count > 0)
		iopmp_enable(a210_pd->device_ids, a210_pd->device_ids_count);
#endif

	return 0;
}

static char *a210_pd_get_user_string(const char __user *userbuf, size_t userlen)
{
	char *buffer;

	buffer = vmalloc(userlen + 1);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(buffer, userbuf, userlen) != 0) {
		vfree(buffer);
		return ERR_PTR(-EFAULT);
	}

	pr_debug("buffer before strip linefeed = %s\n", buffer);
	/* got the string, now strip linefeed. */
	if (buffer[userlen - 1] == '\n')
		buffer[userlen - 1] = '\0';
	else
		buffer[userlen] = '\0';

	pr_debug("buffer after strip linefeed = %s\n", buffer);

	return buffer;
}

static ssize_t a210_power_domain_write(struct file *file,
		const char __user *userbuf,
		size_t userlen, loff_t *ppos)
{
	char *buffer, *start, *end;
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct a210_pd_soc *soc = m->private;
	struct device *dev = soc->dev;
	struct generic_pm_domain *domain;
	char pd_name[A210_PD_NAME_SIZE];
	char pd_state[A210_PD_STATE_NAME_SIZE];
	int idx, ret;

	buffer = a210_pd_get_user_string(userbuf, userlen);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	start = skip_spaces(buffer); // skip leading space if any
	end = start;
	while (!isspace(*end) && *end != '\0') // eg: find "gpu" before a space from string "gpu on"
		end++;

	*end = '\0';
	strscpy(pd_name, start, sizeof(pd_name));

	/* find the target power domain */
	for (idx = 0; idx < soc->num_domains; idx++) {
		domain = &soc->domains[idx]->pd;
		if (strcmp(pd_name, domain->name))
			continue;
		break;
	}

	if (idx == soc->num_domains) {
		dev_err(dev, "no taget power domain-%s found, idx = %d, total pd numbers = %d\n",
				pd_name, idx, soc->num_domains);
		userlen = -EINVAL;
		goto out;
	}

	end = end + 1; // end is the new start
	start = skip_spaces(end); // skip leading space if any
	end = start;
	while (!isspace(*end) && *end != '\0')
		end++;

	*end = '\0';
	strscpy(pd_state, start, sizeof(pd_state));

	if (!strcmp(pd_state, "on")) {
		ret = domain->power_on(domain);
		if (ret) {
			userlen = ret;
			goto out;
		}
	} else if (!strcmp(pd_state, "off")) {
		ret = domain->power_off(domain);
		if (ret) {
			userlen = ret;
			goto out;
		}
	} else {
		dev_err(dev, "invalid power domain target state, not 'on' or 'off'\n");
		userlen = -EINVAL;
		goto out;
	}

out:
	vfree(buffer);

	return userlen;
}

static int a210_power_domain_show(struct seq_file *m, void *v)
{
	struct a210_pd_soc *soc = m->private;
	u32 count = soc->num_domains;
	int idx;

	seq_puts(m, "[Power domain name list]: ");
	for (idx = 0; idx < count; idx++)
		seq_printf(m, "%s ", soc->domains[idx]->pd.name);
	seq_puts(m, "\n");
	seq_puts(m, "[Power on  domain usage]: echo power_name on  > domain\n");
	seq_puts(m, "[Power off domain usage]: echo power_name off > domain\n");

	return 0;
}

static int a210_power_domain_open(struct inode *inode, struct file *file)
{
	struct a210_pd_soc *soc = inode->i_private;

	return single_open(file, a210_power_domain_show, soc);
}

static const struct file_operations a210_power_domain_fops = {
	.owner = THIS_MODULE,
	.write = a210_power_domain_write,
	.read = seq_read,
	.open = a210_power_domain_open,
	.llseek = generic_file_llseek,
};

static void pd_debugfs_init(struct a210_pd_soc *soc)
{
	/* Idempotent: a210_pd_probe() may run this again on a retried attempt. */
	if (pd_debugfs_root)
		return;

	pd_debugfs_root = debugfs_create_dir("power_domain", NULL);
	if (IS_ERR_OR_NULL(pd_debugfs_root))
		return;

	pd_pde = debugfs_create_file("domain", 0600, pd_debugfs_root,
			soc, &a210_power_domain_fops);
}

static int a210_domain_lookup(struct device_node *np)
{
	unsigned int id;
	int ret;

	ret = of_property_read_u32(np, "id", &id);
	if (ret)
		return -ENODEV;

	return id;
}

static void __iomem *a210_ioremap_resource_by_node(struct device *dev, struct device_node *np, const char *name)
{
	struct resource *res;
	int idx;
	int ret;
	void __iomem *addr;

	idx = of_property_match_string(np, "reg-names", name);
	if (idx < 0) {
		return ERR_PTR(-ENODEV);
	}

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	ret = of_address_to_resource(np, idx, res);
	if (ret) {
		kfree(res);
		dev_err(dev, "Failed to get resource from node %pOF at index %d\n", np, idx);
		return ERR_PTR(ret);
	}

	/*
	 * Not devm_ioremap_resource(): this mapping is owned by the
	 * a210_pm_domain struct below, which now outlives a single failed
	 * probe() attempt (see a210_pd_probe()) - a devm mapping would be
	 * torn down by devres_release_all() the moment probe() returns an
	 * error for a *different*, still-deferred domain, leaving this one's
	 * pca/bpc/pcu_base dangling even though its own registration is fine.
	 */
	addr = ioremap(res->start, resource_size(res));
	kfree(res);

	return addr ?: ERR_PTR(-ENOMEM);
}

static struct a210_pm_domain *a210_pd_find(struct a210_pd_soc *pd_soc,
					    struct device_node *np)
{
	u32 i;

	for (i = 0; i < pd_soc->num_domains; i++)
		if (pd_soc->domains[i]->np == np)
			return pd_soc->domains[i];

	return NULL;
}

/*
 * Swap-removes a domain from pd_soc->domains[] if present - a no-op if not
 * (the domain never got past pm_genpd_init()/of_genpd_add_provider_simple(),
 * so it was never added in the first place). Order doesn't matter: the array
 * is only ever searched by np, never iterated for ordering.
 */
static void a210_pd_forget(struct a210_pd_soc *pd_soc,
			   struct a210_pm_domain *a210_pd)
{
	u32 i;

	for (i = 0; i < pd_soc->num_domains; i++) {
		if (pd_soc->domains[i] != a210_pd)
			continue;
		pd_soc->domains[i] = pd_soc->domains[--pd_soc->num_domains];
		return;
	}
}

static int a210_add_one_domain(struct platform_device *pdev, struct device_node *np)
{
	struct device *dev = &pdev->dev;
	struct a210_pd_soc *pd_soc = dev_get_drvdata(dev);
	struct a210_pm_domain *a210_pd;
	int ret;

	/*
	 * A domain whose genpd + OF provider already registered on an
	 * earlier attempt, but whose reset/clk fetch then hit
	 * -EPROBE_DEFER, is looked up here instead of allocated fresh.
	 * Repeating pm_genpd_init()/of_genpd_add_provider_simple() for the
	 * same np on every retry corrupts genpd's own kobject/debugfs
	 * bookkeeping for that domain - confirmed via "debugfs: '<domain>'
	 * already exists in 'pm_genpd'" immediately followed by a
	 * refcount_t underflow in pm_genpd_remove()'s device_link teardown.
	 * A single occurrence just looks like a WARN()-and-continue, but
	 * enough retries (e.g. a slow console stretching out the
	 * deferred-probe window) reproduce it as a full boot hang instead.
	 */
	a210_pd = a210_pd_find(pd_soc, np);
	if (!a210_pd) {
		/*
		 * Plain kzalloc, not devm: this struct is recorded in
		 * pd_soc->domains[] and must survive a failed probe() return
		 * for a *different* domain (see a210_init_pm_domains()'s
		 * idempotent retry) instead of being freed by
		 * devres_release_all() out from under an already-registered
		 * genpd/provider.
		 */
		a210_pd = kzalloc(sizeof(*a210_pd), GFP_KERNEL);
		if (!a210_pd)
			return -ENOMEM;

		int id = a210_domain_lookup(np);
		if (id < 0) {
			kfree(a210_pd);
			return -ENODEV;
		}

		a210_pd->index = id;
		a210_pd->np = np;
		a210_pd->pd.name = np->name;
		a210_pd->pd.power_off = a210_pd_power_off;
		a210_pd->pd.power_on = a210_pd_power_on;
		a210_pd->soc = pd_soc;

		a210_pd->pca_base = a210_ioremap_resource_by_node(dev, np, "pca");
		a210_pd->bpc_base = a210_ioremap_resource_by_node(dev, np, "bpc");
		a210_pd->pcu_base = a210_ioremap_resource_by_node(dev, np, "pcu");

		ret = pm_genpd_init(&a210_pd->pd, NULL, true);
		if (ret) {
			dev_err(dev, "failed to init power domain %s index %d",
				a210_pd->pd.name, a210_pd->index);
			if (!IS_ERR(a210_pd->pca_base))
				iounmap(a210_pd->pca_base);
			if (!IS_ERR(a210_pd->bpc_base))
				iounmap(a210_pd->bpc_base);
			if (!IS_ERR(a210_pd->pcu_base))
				iounmap(a210_pd->pcu_base);
			kfree(a210_pd);
			return -ENODEV;
		}

		ret = of_genpd_add_provider_simple(np, &a210_pd->pd);
		if (ret) {
			dev_err(dev, "failed to add PM domain provider for %pOFn: %d\n",
				np, ret);
			goto remove_genpd;
		}

		/*
		 * genpd + OF provider are up now, regardless of whether
		 * reset/clk fetch below succeeds - record the domain
		 * immediately (not only after a full success, like before
		 * this fix) so a retry finds it via a210_pd_find() above
		 * instead of redoing the two calls above.
		 */
		pd_soc->domains[pd_soc->num_domains++] = a210_pd;
	}

	if (!a210_pd->reset_done) {
		struct reset_control *rst = of_reset_control_array_get_optional_shared(np);

		if (IS_ERR(rst)) {
			ret = PTR_ERR(rst);
			dev_err(dev, "failed to get device resets for domain:%s\n", np->name);
			if (ret != -EPROBE_DEFER)
				goto remove_genpd;
			return ret;
		}
		a210_pd->reset = rst;
		a210_pd->reset_done = true;
	}

	if (!a210_pd->num_clks)
		a210_pd->num_clks = of_clk_get_parent_count(np);

	if (a210_pd->num_clks && !a210_pd->clks) {
		/* Plain kcalloc: see the kzalloc(*a210_pd) comment above. */
		a210_pd->clks = kcalloc(a210_pd->num_clks,
					 sizeof(*a210_pd->clks), GFP_KERNEL);
		if (!a210_pd->clks) {
			ret = -ENOMEM;
			goto remove_genpd;
		}
	}

	for (; a210_pd->clks_fetched < a210_pd->num_clks; a210_pd->clks_fetched++) {
		struct clk *clk = of_clk_get(np, a210_pd->clks_fetched);

		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			dev_err(dev,
				"failed to get clk at index %d: err:%d for domain:%s\n",
				a210_pd->clks_fetched, ret, np->name);
			if (ret != -EPROBE_DEFER)
				goto remove_genpd;
			return ret;
		}
		a210_pd->clks[a210_pd->clks_fetched].clk = clk;
	}

	if (a210_pd->complete)
		return 0;

	dev_dbg(dev, "added PM domain %s\n", a210_pd->pd.name);

#ifdef CONFIG_ZH_IOPMP
	/* get iopmps config node */
	int device_id_count = 0;
	int count = of_count_phandle_with_args(np, "iopmps", NULL);
	for (int i = 0; i < count; i++) {
		struct device_node *iopmp_node = of_parse_phandle(np, "iopmps", i);
		if (!iopmp_node) {
			dev_err(dev, "failed to get iopmps at index %d: for domain:%s\n", i, np->name);
			ret = -EINVAL;
			goto remove_genpd;
		}
		else {
			u32 device_id;
			if (of_property_read_u32(iopmp_node, "device-id", &device_id) == 0) {
				a210_pd->device_ids[device_id_count] = device_id;
				device_id_count++;
				dev_dbg(dev, "domain %pOFn iopmp node %pOFn: device id: %d\n", np, iopmp_node, device_id);
			}
		}
	}
	a210_pd->device_ids_count = device_id_count;
#endif

	a210_pd->complete = true;
	return 0;

remove_genpd:
	/*
	 * Only reached for a genuinely permanent failure now - anything
	 * that could plausibly resolve on a later retry (-EPROBE_DEFER)
	 * returns early above instead, leaving the domain registered in
	 * pd_soc->domains[] for a210_pd_find() to pick back up. del the
	 * provider before pm_genpd_remove(): it refuses to remove a domain
	 * that still has one attached (of_genpd_add_provider_simple() above
	 * may have already succeeded on every failure path that reaches
	 * here) - harmless no-op if the provider was never added.
	 */
	a210_pd_forget(pd_soc, a210_pd);
	of_genpd_del_provider(np);
	pm_genpd_remove(&a210_pd->pd);
	reset_control_put(a210_pd->reset);
	kfree(a210_pd->clks);
	if (!IS_ERR(a210_pd->pca_base))
		iounmap(a210_pd->pca_base);
	if (!IS_ERR(a210_pd->bpc_base))
		iounmap(a210_pd->bpc_base);
	if (!IS_ERR(a210_pd->pcu_base))
		iounmap(a210_pd->pcu_base);
	kfree(a210_pd);
	return ret;
}

/*
 * Full teardown of a registered domain: mirrors a210_add_one_domain()'s own
 * remove_genpd unwind, plus the clk/reset/iomem state that only exists once
 * a domain has made at least some progress. Used by .remove() below - NOT by
 * the retry path in a210_init_pm_domains()/a210_add_one_domain() any more
 * (see a210_pd_domain_registered() and a210_pd_find()): a domain whose
 * genpd/provider already registered in an earlier probe() attempt now stays
 * registered across a later -EPROBE_DEFER instead of being torn down and
 * re-added on every retry - both when that -EPROBE_DEFER comes from a later,
 * unrelated domain still being handled in the same for_each_child_of_node()
 * pass, and (since a210_add_one_domain() itself no longer tears down on a
 * transient reset/clk failure) when it's the domain's *own* reset/clk fetch
 * that's still deferring. That churn (repeatedly removing/re-adding a
 * domain's provider, which other devices can be fw_devlink-linked to) was
 * itself the cause of a device_link double-release
 * ("refcount_t: underflow; use-after-free." in device_link_release_fn) and a
 * genpd/debugfs bookkeeping collision ("debugfs: '<domain>' already exists
 * in 'pm_genpd'") - confirmed reproducible even with a single hart online,
 * so it's a genuine software race in the teardown/rebuild cycle, not the
 * SMP/cache-coherency class of hardware issue this board also has (see
 * [[a210-vector-unaligned-erratum]]). Severe/frequent enough retries (e.g. a
 * slow console stretching out the deferred-probe window) reproduce it as a
 * full boot hang, not just the WARN()-and-continue it looks like in
 * isolation.
 */
static void a210_teardown_pm_domains(struct a210_pd_soc *pd_soc)
{
	while (pd_soc->num_domains > 0) {
		struct a210_pm_domain *a210_pd = pd_soc->domains[--pd_soc->num_domains];

		of_genpd_del_provider(a210_pd->np);
		pm_genpd_remove(&a210_pd->pd);
		reset_control_put(a210_pd->reset);
		kfree(a210_pd->clks);
		if (!IS_ERR(a210_pd->pca_base))
			iounmap(a210_pd->pca_base);
		if (!IS_ERR(a210_pd->bpc_base))
			iounmap(a210_pd->bpc_base);
		if (!IS_ERR(a210_pd->pcu_base))
			iounmap(a210_pd->pcu_base);
		kfree(a210_pd);
	}
}

/*
 * True only once a domain is *fully* done (genpd, provider, reset, clks,
 * iopmp - see a210_pd->complete). A domain that's registered but still
 * mid-retry (genpd/provider up, reset/clk fetch still deferring) must return
 * false here so a210_init_pm_domains() calls a210_add_one_domain() again for
 * it - that's what lets a210_pd_find() there resume the same struct instead
 * of redoing pm_genpd_init()/of_genpd_add_provider_simple().
 */
static bool a210_pd_domain_registered(struct a210_pd_soc *pd_soc,
				      struct device_node *np)
{
	struct a210_pm_domain *a210_pd = a210_pd_find(pd_soc, np);

	return a210_pd && a210_pd->complete;
}

static int a210_init_pm_domains(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a210_pd_soc *pd_soc = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node;
	struct device_node *child;
	struct of_phandle_args child_args, parent_args;
	int ret = 0;

	for_each_child_of_node(np, child) {
		if (!of_device_is_available(child))
			continue;

		/*
		 * Already fully registered (genpd + provider + subdomain
		 * link, all added together the one time this child was
		 * reached below) by an earlier, since-failed probe() attempt
		 * for some *other*, still-deferred domain - leave it alone.
		 */
		if (a210_pd_domain_registered(pd_soc, child))
			continue;

		ret = a210_add_one_domain(pdev, child);
		if (ret) {
			dev_err(dev, "failed to handle node %pOFn: %d\n",
				child, ret);
			of_node_put(child);
			return ret;
		}

		if (of_parse_phandle_with_args(child, "power-domains",
					       "#power-domain-cells",
					       0, &parent_args))
			continue;

		child_args.np = child;
		child_args.args_count = 0;

		ret = of_genpd_add_subdomain(&parent_args, &child_args);
		of_node_put(parent_args.np);
		if (ret) {
			dev_err(dev, "failed to handle subdomain node %pOFn: %d\n",
				child, ret);
			of_node_put(child);
			return ret;
		}
	}

	/*
	 * No of_node_put(np) here: np is dev->of_node, owned by the platform
	 * device itself - this function never took its own reference via
	 * of_node_get(), so it has no reference to give back. Putting it
	 * anyway silently over-decrements dev->of_node's (kobject-backed)
	 * refcount on every call - harmless the one time builtin_platform_driver
	 * probes in buildroot, but this function reruns on every -EPROBE_DEFER
	 * retry here, and a few retries were enough to corrupt it badly enough
	 * to produce "kobject ... is not initialized" / "refcount_t: underflow"
	 * crashes and spurious "already exists" collisions on domains that were
	 * never actually re-registered.
	 */
	return ret;
}

static int a210_pd_parse_regulators(struct device *dev)
{
	struct a210_pd_soc *pd_soc = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node;
	struct device_node *child, *child_regulator;

	for_each_child_of_node(np, child) {
		if (!of_device_is_available(child))
			continue;

		int id = a210_domain_lookup(child);
		if (id < 0) {
			return -ENODEV;
		}

		if (of_property_present(child, "pmic-supply")) {
			child_regulator = of_parse_phandle(child, "pmic-supply", 0);
			pd_soc->regulators[id] = regulator_get_optional(dev, child_regulator->name);
			if (IS_ERR(pd_soc->regulators[id])) {
				dev_dbg(dev, "Regulator for %s deferred %ld\n", child->name, PTR_ERR(pd_soc->regulators[id]));
				return -EPROBE_DEFER;
			} else {
				u32 max_uV = 0;
				if (of_property_read_u32(child_regulator, "regulator-max-microvolt", &max_uV) == 0) {
					regulator_set_voltage(pd_soc->regulators[id], max_uV, max_uV);
					dev_info(dev, "Set %s voltage target %duV\n",
						child->name, max_uV);
					regulator_put(pd_soc->regulators[id]);
					/*
					 * Not devm_regulator_get_optional(): pd_soc now
					 * outlives a single failed probe() attempt (see
					 * a210_pd_probe()), same reasoning as the
					 * kzalloc(*a210_pd) comment in
					 * a210_add_one_domain(). Currently dormant since
					 * none of this tree's enabled domains (top/usb/
					 * peri2/peri3) have a pmic-supply - only the
					 * still-disabled gpu/npu_wrapper/vp_wrapper
					 * domains do (see a210-android-unsupported.dtsi)
					 * - but would need a matching regulator_put() in
					 * a210_teardown_pm_domains() once those return.
					 */
					pd_soc->regulators[id] = regulator_get_optional(dev, child_regulator->name);
				}
			}
		}
	}

	/* No of_node_put(np) here - see the matching comment in a210_init_pm_domains(). */

	return 0;
}

static int a210_pd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a210_pd_soc *pd_soc = a210_pd_soc_singleton;
	int ret;

	/*
	 * Not devm_kzalloc(), and looked up above via the static singleton,
	 * not dev_get_drvdata(): pd_soc must survive a failed probe() return
	 * (a still-deferred domain's clk/reset not being ready yet is
	 * routine) so a retried probe() can skip re-registering domains that
	 * already succeeded - see a210_pd_domain_registered() in
	 * a210_init_pm_domains(). dev_get_drvdata() can't be used for this
	 * specific lookup: really_probe() resets a device's drvdata to NULL
	 * on any probe() failure, so it would always read back NULL here on
	 * a retry regardless of what a prior attempt stored. Freed by
	 * a210_pd_remove() below.
	 */
	if (!pd_soc) {
		pd_soc = kzalloc(sizeof(*pd_soc), GFP_KERNEL);
		if (!pd_soc)
			return -ENOMEM;
		pd_soc->dev = dev;
		a210_pd_soc_singleton = pd_soc;
	}
	dev_set_drvdata(dev, pd_soc);

	ret = a210_pd_parse_regulators(dev);
	if (ret)
		return ret;

	ret = a210_init_pm_domains(pdev);
	if (ret)
		return ret;

	pd_debugfs_init(pd_soc);

	pr_cont("Registered a210 power domain:");
	for (int i = 0; i < pd_soc->num_domains; i++) {
		if (pd_soc->domains[i]) {
			pr_cont(" %s", pd_soc->domains[i]->pd.name);
		}
	}
	pr_cont("\n");

	return ret;
}

static void a210_pd_remove(struct platform_device *pdev)
{
	struct a210_pd_soc *pd_soc = dev_get_drvdata(&pdev->dev);

	a210_teardown_pm_domains(pd_soc);
	kfree(pd_soc);
	a210_pd_soc_singleton = NULL;
}

static const struct of_device_id a210_pd_of_match[] = {
	{ .compatible = "zhihe,a210-power-domain"},
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, a210_pd_of_match);

static struct platform_driver a210_pd_driver = {
	.probe = a210_pd_probe,
	.remove = a210_pd_remove,
	.driver = {
		.name = "a210-power-domain",
		.of_match_table = of_match_ptr(a210_pd_of_match),
	},
};

module_platform_driver(a210_pd_driver);

MODULE_AUTHOR("dong.yan <yand@zhcomputing.com>");
MODULE_DESCRIPTION("Zhihe A210 power domain driver");
MODULE_LICENSE("GPL v2");
