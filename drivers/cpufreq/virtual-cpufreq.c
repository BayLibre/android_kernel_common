// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC
 */

#include <linux/arch_topology.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>

#define REG_CUR_FREQ_OFFSET 0x0
#define REG_SET_FREQ_OFFSET 0x4
#define PER_CPU_OFFSET 0x8

struct virt_cpufreq_ops {
	void (*set_freq)(struct cpufreq_policy *policy, u32 freq);
	u32 (*get_freq)(struct cpufreq_policy *policy);
};

struct virt_cpufreq_drv_data {
	void __iomem *base;
	const struct virt_cpufreq_ops *ops;
};

static void virt_cpufreq_set_freq(struct cpufreq_policy *policy, u32 freq)
{
	struct virt_cpufreq_drv_data *data = policy->driver_data;

	writel_relaxed(freq, data->base + policy->cpu * PER_CPU_OFFSET
			+ REG_SET_FREQ_OFFSET);
}

static u32 virt_cpufreq_get_freq(struct cpufreq_policy *policy)
{
	struct virt_cpufreq_drv_data *data = policy->driver_data;

	return readl_relaxed(data->base + policy->cpu * PER_CPU_OFFSET
			+ REG_CUR_FREQ_OFFSET);
}

static const struct virt_cpufreq_ops virt_freq_ops = {
	.set_freq = virt_cpufreq_set_freq,
	.get_freq = virt_cpufreq_get_freq,
};

static void virt_scale_freq_tick(void)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(smp_processor_id());
	struct virt_cpufreq_drv_data *data = policy->driver_data;
	u32 max_freq = (u32)policy->cpuinfo.max_freq;
	u64 cur_freq;
	u64 scale;

	cpufreq_cpu_put(policy);

	cur_freq = (u64)data->ops->get_freq(policy);
	cur_freq <<= SCHED_CAPACITY_SHIFT;
	scale = div_u64(cur_freq, max_freq);

	this_cpu_write(arch_freq_scale, (unsigned long)scale);
}

static struct scale_freq_data virt_sfd = {
	.source = SCALE_FREQ_SOURCE_VIRT,
	.set_freq_scale = virt_scale_freq_tick,
};

static unsigned int virt_cpufreq_set_perf(struct cpufreq_policy *policy)
{
	struct virt_cpufreq_drv_data *data = policy->driver_data;
	/*
	 * Use cached frequency to avoid rounding to freq table entries
	 * and undo 25% frequency boost applied by schedutil.
	 */
	u32 freq = mult_frac(policy->cached_target_freq, 80, 100);

	data->ops->set_freq(policy, freq);
	return 0;
}

static unsigned int virt_cpufreq_fast_switch(struct cpufreq_policy *policy,
		unsigned int target_freq)
{
	virt_cpufreq_set_perf(policy);
	return target_freq;
}

static int virt_cpufreq_target_index(struct cpufreq_policy *policy,
		unsigned int index)
{
	return virt_cpufreq_set_perf(policy);
}

static const struct of_device_id virt_cpufreq_match[] = {
	{ .compatible = "virtual,cpufreq", .data = &virt_freq_ops},
	{}
};
MODULE_DEVICE_TABLE(of, virt_cpufreq_match);

static int virt_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct virt_cpufreq_drv_data *drv_data = cpufreq_get_driver_data();
	struct cpufreq_frequency_table *table;
	struct device *cpu_dev;
	int ret;

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev)
		return -ENODEV;

	ret = dev_pm_opp_of_add_table(cpu_dev);
	if (ret)
		return ret;

	ret = dev_pm_opp_get_opp_count(cpu_dev);
	if (ret <= 0) {
		dev_err(cpu_dev, "OPP table can't be empty\n");
		return -ENODEV;
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		return ret;
	}

	policy->freq_table = table;
	policy->dvfs_possible_from_any_cpu = false;
	policy->fast_switch_possible = true;
	policy->driver_data = drv_data;

	/*
	 * Only takes effect if another FIE source such as AMUs
	 * have not been registered.
	 */
	topology_set_scale_freq_source(&virt_sfd, policy->cpus);

	return 0;

}

static int virt_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	topology_clear_scale_freq_source(SCALE_FREQ_SOURCE_VIRT, policy->related_cpus);
	kfree(policy->freq_table);
	policy->freq_table = NULL;
	return 0;
}

static int virt_cpufreq_online(struct cpufreq_policy *policy)
{
	/* Nothing to restore. */
	return 0;
}

static int virt_cpufreq_offline(struct cpufreq_policy *policy)
{
	/* Dummy offline() to avoid exit() being called and freeing resources. */
	return 0;
}

static struct cpufreq_driver cpufreq_virt_driver = {
	.name		= "virt-cpufreq",
	.init		= virt_cpufreq_cpu_init,
	.exit		= virt_cpufreq_cpu_exit,
	.online         = virt_cpufreq_online,
	.offline        = virt_cpufreq_offline,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= virt_cpufreq_target_index,
	.fast_switch	= virt_cpufreq_fast_switch,
	.attr		= cpufreq_generic_attr,
};

static int virt_cpufreq_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct virt_cpufreq_drv_data *drv_data;

	drv_data = devm_kzalloc(&pdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->ops = of_device_get_match_data(&pdev->dev);
	if (!drv_data->ops)
		return -EINVAL;

	drv_data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drv_data->base))
		return PTR_ERR(drv_data->base);

	cpufreq_virt_driver.driver_data = drv_data;

	ret = cpufreq_register_driver(&cpufreq_virt_driver);
	if (ret) {
		dev_err(&pdev->dev, "Virtual CPUFreq driver failed to register: %d\n", ret);
		return ret;
	}

	dev_dbg(&pdev->dev, "Virtual CPUFreq driver initialized\n");
	return 0;
}

static int virt_cpufreq_driver_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&cpufreq_virt_driver);
	return 0;
}

static struct platform_driver virt_cpufreq_driver = {
	.probe = virt_cpufreq_driver_probe,
	.remove = virt_cpufreq_driver_remove,
	.driver = {
		.name = "virt-cpufreq",
		.of_match_table = virt_cpufreq_match,
	},
};

static int __init virt_cpufreq_init(void)
{
	return platform_driver_register(&virt_cpufreq_driver);
}
postcore_initcall(virt_cpufreq_init);

static void __exit virt_cpufreq_exit(void)
{
	platform_driver_unregister(&virt_cpufreq_driver);
}
module_exit(virt_cpufreq_exit);

MODULE_DESCRIPTION("Virtual cpufreq driver");
MODULE_LICENSE("GPL");
