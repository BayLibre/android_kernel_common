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
#define REG_FREQ_SIZE 0x8

struct kvm_cpufreq_info {
	void (*set_freq)(struct cpufreq_policy *policy, u32 freq);
	u32 (*get_freq)(struct cpufreq_policy *policy);
};

struct kvm_cpufreq_drv_data {
	void __iomem *base;
	const struct kvm_cpufreq_info *info;
};

static void kvm_cpufreq_set_freq(struct cpufreq_policy *policy, u32 freq)
{
	struct kvm_cpufreq_drv_data *data = policy->driver_data;

	writel_relaxed(freq, data->base + policy->cpu * REG_FREQ_SIZE
			+ REG_SET_FREQ_OFFSET);
}

static u32 kvm_cpufreq_get_freq(struct cpufreq_policy *policy)
{
	struct kvm_cpufreq_drv_data *data = policy->driver_data;

	return readl_relaxed(data->base + policy->cpu * REG_FREQ_SIZE
			+ REG_CUR_FREQ_OFFSET);
}

static const struct kvm_cpufreq_info kvm_freq_info = {
	.set_freq = kvm_cpufreq_set_freq,
	.get_freq = kvm_cpufreq_get_freq,
};

static void kvm_scale_freq_tick(void)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(smp_processor_id());
	struct kvm_cpufreq_drv_data *data = policy->driver_data;
	u64 max_freq = (u64)policy->cpuinfo.max_freq;
	u64 cur_freq;
	u64 scale;

	cpufreq_cpu_put(policy);

	cur_freq = (u64)data->info->get_freq(policy);
	cur_freq <<= SCHED_CAPACITY_SHIFT;
	scale = div64_u64(cur_freq, max_freq);

	this_cpu_write(arch_freq_scale, (unsigned long)scale);
}

static struct scale_freq_data kvm_sfd = {
	.source = SCALE_FREQ_SOURCE_KVM,
	.set_freq_scale = kvm_scale_freq_tick,
};

static unsigned int kvm_cpufreq_set_perf(struct cpufreq_policy *policy)
{
	struct kvm_cpufreq_drv_data *data = policy->driver_data;
	/*
	 * Use cached frequency to avoid rounding to freq table entries
	 * and undo 25% frequency boost from util.
	 */
	u32 freq = mult_frac(policy->cached_target_freq, 80, 100);

	data->info->set_freq(policy, freq);
	return 0;
}

static unsigned int kvm_cpufreq_fast_switch(struct cpufreq_policy *policy,
		unsigned int target_freq)
{
	kvm_cpufreq_set_perf(policy);
	return target_freq;
}

static int kvm_cpufreq_target_index(struct cpufreq_policy *policy,
		unsigned int index)
{
	return kvm_cpufreq_set_perf(policy);
}

static const struct of_device_id kvm_cpufreq_match[] = {
	{ .compatible = "virtual,kvm-cpufreq", .data = &kvm_freq_info},
	{}
};
MODULE_DEVICE_TABLE(of, kvm_cpufreq_match);

static int kvm_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct kvm_cpufreq_drv_data *drv_data = cpufreq_get_driver_data();
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
	policy->transition_delay_us = 1;
	policy->driver_data = drv_data;

	/*
	 * Only takes effect if another FIE source such as AMUs
	 * have not been registered.
	 */
	topology_set_scale_freq_source(&kvm_sfd, policy->cpus);

	return 0;

}

static int kvm_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	topology_clear_scale_freq_source(SCALE_FREQ_SOURCE_KVM, policy->related_cpus);
	kfree(policy->freq_table);
	policy->freq_table = NULL;
	return 0;
}

static int kvm_cpufreq_online(struct cpufreq_policy *policy)
{
	/* Nothing to restore. */
	return 0;
}

static int kvm_cpufreq_offline(struct cpufreq_policy *policy)
{
	/* Dummy offline() to avoid exit() being called and freeing resources. */
	return 0;
}

static struct cpufreq_driver cpufreq_kvm_driver = {
	.name		= "kvm-cpufreq",
	.init		= kvm_cpufreq_cpu_init,
	.exit		= kvm_cpufreq_cpu_exit,
	.online         = kvm_cpufreq_online,
	.offline        = kvm_cpufreq_offline,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= kvm_cpufreq_target_index,
	.fast_switch	= kvm_cpufreq_fast_switch,
	.attr		= cpufreq_generic_attr,
};

static int kvm_cpufreq_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct kvm_cpufreq_drv_data *drv_data;

	drv_data = devm_kzalloc(&pdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->info = of_device_get_match_data(&pdev->dev);
	if (!drv_data->info)
		return -EINVAL;

	drv_data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drv_data->base))
		return PTR_ERR(drv_data->base);

	cpufreq_kvm_driver.driver_data = drv_data;

	ret = cpufreq_register_driver(&cpufreq_kvm_driver);
	if (ret) {
		dev_err(&pdev->dev, "KVM CPUFreq driver failed to register: %d\n", ret);
		return ret;
	}

	dev_dbg(&pdev->dev, "KVM CPUFreq driver initialized\n");
	return 0;
}

static int kvm_cpufreq_driver_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&cpufreq_kvm_driver);
	return 0;
}

static struct platform_driver kvm_cpufreq_driver = {
	.probe = kvm_cpufreq_driver_probe,
	.remove = kvm_cpufreq_driver_remove,
	.driver = {
		.name = "kvm-cpufreq",
		.of_match_table = kvm_cpufreq_match,
	},
};

static int __init kvm_cpufreq_init(void)
{
	return platform_driver_register(&kvm_cpufreq_driver);
}
postcore_initcall(kvm_cpufreq_init);

static void __exit kvm_cpufreq_exit(void)
{
	platform_driver_unregister(&kvm_cpufreq_driver);
}
module_exit(kvm_cpufreq_exit);

MODULE_DESCRIPTION("KVM cpufreq driver");
MODULE_LICENSE("GPL");
