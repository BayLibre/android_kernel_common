// SPDX-License-Identifier: GPL-2.0
/*
 * Energy Model of devices
 *
 * Copyright (c) 2018-2021, Arm ltd.
 * Written by: Quentin Perret, Arm ltd.
 * Improvements provided by: Lukasz Luba, Arm ltd.
 */

#define pr_fmt(fmt) "energy_model: " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/energy_model.h>
#include <linux/sched/topology.h>
#include <linux/slab.h>

#define CREATE_TRACE_POINTS
#include <trace/events/energy_model.h>

/*
 * Mutex serializing the registrations of performance domains and letting
 * callbacks defined by drivers sleep.
 */
static DEFINE_MUTEX(em_pd_mutex);

static void
em_cpufreq_update_efficiencies(struct device *dev,
			       struct em_perf_state *table);

static bool _is_cpu_device(struct device *dev)
{
	return (dev->bus == &cpu_subsys);
}

#ifdef CONFIG_DEBUG_FS
static struct dentry *rootdir;

static void em_debug_create_ps(struct em_perf_state *ps, struct dentry *pd)
{
	struct dentry *d;
	char name[24];

	snprintf(name, sizeof(name), "ps:%lu", ps->frequency);

	/* Create per-ps directory */
	d = debugfs_create_dir(name, pd);
	debugfs_create_ulong("frequency", 0444, d, &ps->frequency);
	debugfs_create_ulong("power", 0444, d, &ps->power);
	debugfs_create_ulong("cost", 0444, d, &ps->cost);
	debugfs_create_ulong("inefficient", 0444, d, &ps->flags);
}

static int em_debug_cpus_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "%*pbl\n", cpumask_pr_args(to_cpumask(s->private)));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(em_debug_cpus);

static int em_debug_units_show(struct seq_file *s, void *unused)
{
	struct em_perf_domain *pd = s->private;
	char *units = (pd->flags & EM_PERF_DOMAIN_MICROWATTS) ?
		"microWatts" : "bogoWatts";

	seq_printf(s, "%s\n", units);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(em_debug_units);

static int em_debug_skip_inefficiencies_show(struct seq_file *s, void *unused)
{
	struct em_perf_domain *pd = s->private;
	int enabled = (pd->flags & EM_PERF_DOMAIN_SKIP_INEFFICIENCIES) ? 1 : 0;

	seq_printf(s, "%d\n", enabled);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(em_debug_skip_inefficiencies);

static void em_debug_create_pd(struct device *dev)
{
	struct dentry *d;
	int i;

	/* Create the directory of the performance domain */
	d = debugfs_create_dir(dev_name(dev), rootdir);

	if (_is_cpu_device(dev))
		debugfs_create_file("cpus", 0444, d, dev->em_pd->cpus,
				    &em_debug_cpus_fops);

	debugfs_create_file("units", 0444, d, dev->em_pd, &em_debug_units_fops);
	debugfs_create_file("skip-inefficiencies", 0444, d, dev->em_pd,
			    &em_debug_skip_inefficiencies_fops);

	/* Create a sub-directory for each performance state */
	for (i = 0; i < dev->em_pd->nr_perf_states; i++)
		em_debug_create_ps(&dev->em_pd->table[i], d);

}

static void em_debug_remove_pd(struct device *dev)
{
	struct dentry *debug_dir;

	debug_dir = debugfs_lookup(dev_name(dev), rootdir);
	debugfs_remove_recursive(debug_dir);
}

static int __init em_debug_init(void)
{
	/* Create /sys/kernel/debug/energy_model directory */
	rootdir = debugfs_create_dir("energy_model", NULL);

	return 0;
}
fs_initcall(em_debug_init);
#else /* CONFIG_DEBUG_FS */
static void em_debug_create_pd(struct device *dev) {}
static void em_debug_remove_pd(struct device *dev) {}
#endif

static void em_destroy_rt_table_rcu(struct rcu_head *rp)
{
	struct em_perf_rt_table *rt_table;

	rt_table = container_of(rp, struct em_perf_rt_table, rcu);
	kfree(rt_table->state);
	kfree(rt_table);
}

static void em_destroy_tmp_setup_rcu(struct rcu_head *rp)
{
	struct em_perf_rt_table *rt_table;

	rt_table = container_of(rp, struct em_perf_rt_table, rcu);
	/* Don't free the 'rt_table->state' since it points to 'pd->table'. */
	kfree(rt_table);
}

static int em_perf_rt_table_set(struct device *dev,
				struct em_perf_rt_table *rt_table)
{
	struct em_perf_domain *pd = dev->em_pd;
	struct em_perf_rt_table *tmp;

	mutex_lock(&pd->lock);

	tmp = pd->rt_table;

	/*
	 * Don't race with the unregister EM function, which might block
	 * for a while (even milliseconds). The EM unregistration set NULL,
	 * so don't try to overwrite it and simply fail this update call.
	 */
	if (!tmp) {
		mutex_unlock(&pd->lock);
		return -EINVAL;
	}

	rcu_assign_pointer(pd->rt_table, rt_table);

	if (rt_table)
		em_cpufreq_update_efficiencies(dev, rt_table->state);

	/*
	 * Make the tracing inside the critical section, to make sure the
	 * memory is not freed and order is of entries is correct.
	 */
	if (trace_em_perf_state_enabled() && rt_table) {
		unsigned long freq, power, cost, flags;
		int i;

		for (i = 0; i < pd->nr_perf_states; i++) {
			freq = rt_table->state[i].frequency;
			power = rt_table->state[i].power;
			cost = rt_table->state[i].cost;
			flags = rt_table->state[i].flags;

			trace_em_perf_state(dev_name(dev), pd->nr_perf_states,
					    i, freq, power, cost, flags);
		}
	}

	mutex_unlock(&pd->lock);

	if (tmp) {
		/*
		 * Check if the 'state' array is not actually the one from setup.
		 * If it is then don't free it.
		 */
		if (tmp->state == pd->table)
			call_rcu(&tmp->rcu, em_destroy_tmp_setup_rcu);
		else
			call_rcu(&tmp->rcu, em_destroy_rt_table_rcu);
	}

	return 0;
}

static int em_compute_costs(struct device *dev, struct em_perf_state *table,
			    int nr_states, int num_devs)
{
	unsigned long prev_cost = ULONG_MAX;
	unsigned long max_cost = 0;
	u64 fmax;
	int i;

	/* Compute the cost of each performance state. */
	fmax = (u64) table[nr_states - 1].frequency;
	for (i = nr_states - 1; i >= 0; i--) {
		/*
		 * This 'cost' calculation is sensitive to the power scale
		 * which is in use. The micro-Watts values are better than
		 * milli-Watts and avoids rounding errors which are propagated
		 * further causing issues in other mechanisms.
		 */
		table[i].cost = div64_u64(fmax * table[i].power,
					  table[i].frequency);
		if (table[i].cost >= prev_cost) {
			table[i].flags = EM_PERF_STATE_INEFFICIENT;
			dev_dbg(dev, "EM: OPP:%lu is inefficient\n",
				table[i].frequency);
		} else {
			prev_cost = table[i].cost;
		}

		if (max_cost < table[i].cost)
			max_cost = table[i].cost;
	}

	/* Check if it won't overflow during energy estimation. */
	if (em_validate_cost(max_cost, num_devs)) {
		dev_err(dev, "EM: too big 'cost' value: %lu\n",	max_cost);
		return -EINVAL;
	}

	return 0;
}

/**
 * em_dev_update_perf_domain() - Update run-time EM table for a device
 * @dev		: Device for which the EM is to be updated
 * @cb		: Callback function providing the power data for the EM
 * @priv	: Pointer to private data useful for passing context
 *		which might be required while calling @cb
 *
 * Update EM run-time modifiable table for a @dev using the callback
 * defined in @cb. The EM new power values are then used for calculating
 * the em_perf_state::cost for associated performance state.
 *
 * This function uses mutex to serialize writers, so it must not be called
 * from non-sleeping context.
 *
 * Return 0 on success or a proper error in case of failure.
 */
int em_dev_update_perf_domain(struct device *dev, struct em_data_callback *cb,
			      void *priv)
{
	struct em_perf_rt_table *rt_table;
	unsigned long power, freq;
	struct em_perf_domain *pd;
	int ret, i, num_devs = 1;

	/* This cannot be called from atomic context */
	might_sleep();

	if (!dev || !dev->em_pd || !cb)
		return -EINVAL;

	pd = dev->em_pd;

	if (_is_cpu_device(dev))
		num_devs = cpumask_weight(em_span_cpus(pd));

	rt_table = kzalloc(sizeof(*rt_table), GFP_KERNEL);
	if (!rt_table)
		return -ENOMEM;

	rt_table->state = kcalloc(pd->nr_perf_states,
				  sizeof(struct em_perf_state), GFP_KERNEL);
	if (!rt_table->state) {
		kfree(rt_table);
		return -ENOMEM;
	}

	/* Populate run-time table with updated values using driver callback */
	for (i = 0; i < pd->nr_perf_states; i++) {
		freq = pd->table[i].frequency;
		rt_table->state[i].frequency = freq;

		/*
		 * Call driver callback to get a new power value for
		 * a given frequency.
		 */
		ret = cb->update_power(dev, freq, &power, priv);
		if (ret) {
			dev_dbg(dev, "EM: run-time update error: %d\n", ret);
			goto free_rt_table;
		}

		rt_table->state[i].power = power;
	}

	ret = em_compute_costs(dev, rt_table->state, pd->nr_perf_states,
			       num_devs);
	if (ret)
		goto free_rt_table;

	ret = em_perf_rt_table_set(dev, rt_table);
	if (ret)
		goto free_rt_table;

	return 0;

free_rt_table:
	kfree(rt_table->state);
	kfree(rt_table);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(em_dev_update_perf_domain);

static int em_create_perf_table(struct device *dev, struct em_perf_domain *pd,
				int nr_states, struct em_data_callback *cb,
				int num_devs)
{
	unsigned long power, freq, prev_freq = 0;
	struct em_perf_state *table;
	int i, ret;

	table = kcalloc(nr_states, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	/* Build the list of performance states for this performance domain */
	for (i = 0, freq = 0; i < nr_states; i++, freq++) {
		/*
		 * active_power() is a driver callback which ceils 'freq' to
		 * lowest performance state of 'dev' above 'freq' and updates
		 * 'power' and 'freq' accordingly.
		 */
		ret = cb->active_power(&power, &freq, dev);
		if (ret) {
			dev_err(dev, "EM: invalid perf. state: %d\n",
				ret);
			goto free_ps_table;
		}

		/*
		 * We expect the driver callback to increase the frequency for
		 * higher performance states.
		 */
		if (freq <= prev_freq) {
			dev_err(dev, "EM: non-increasing freq: %lu\n",
				freq);
			goto free_ps_table;
		}

		/*
		 * The power returned by active_state() is expected to be
		 * positive and be in range.
		 */
		if (!power || power > EM_MAX_POWER) {
			dev_err(dev, "EM: invalid power: %lu\n",
				power);
			goto free_ps_table;
		}

		table[i].power = power;
		table[i].frequency = prev_freq = freq;
	}

	ret = em_compute_costs(dev, table, nr_states, num_devs);
	if (ret)
		goto free_ps_table;

	pd->table = table;
	pd->nr_perf_states = nr_states;

	return 0;

free_ps_table:
	kfree(table);
	return -EINVAL;
}

static int em_create_pd(struct device *dev, int nr_states,
			struct em_data_callback *cb, cpumask_t *cpus)
{
	struct em_perf_rt_table *rt_table;
	int cpu, ret, num_devs = 1;
	struct em_perf_domain *pd;
	struct device *cpu_dev;

	if (_is_cpu_device(dev)) {
		pd = kzalloc(sizeof(*pd) + cpumask_size(), GFP_KERNEL);
		if (!pd)
			return -ENOMEM;

		cpumask_copy(em_span_cpus(pd), cpus);
		num_devs = cpumask_weight(cpus);
	} else {
		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd)
			return -ENOMEM;
	}

	rt_table = kzalloc(sizeof(*rt_table), GFP_KERNEL);
	if (!rt_table) {
		kfree(pd);
		return -ENOMEM;
	}

	ret = em_create_perf_table(dev, pd, nr_states, cb, num_devs);
	if (ret) {
		kfree(pd);
		kfree(rt_table);
		return ret;
	}

	/* Re-use temporally (till 1st modification) the memory */
	rt_table->state = pd->table;
	rcu_assign_pointer(pd->rt_table, rt_table);

	if (_is_cpu_device(dev))
		for_each_cpu(cpu, cpus) {
			cpu_dev = get_cpu_device(cpu);
			cpu_dev->em_pd = pd;
		}

	dev->em_pd = pd;

	return 0;
}

static void
em_cpufreq_update_efficiencies(struct device *dev, struct em_perf_state *table)
{
	struct em_perf_domain *pd = dev->em_pd;
	struct cpufreq_policy *policy;
	int found = 0;
	int i;

	if (!_is_cpu_device(dev) || !pd)
		return;

	policy = cpufreq_cpu_get(cpumask_first(em_span_cpus(pd)));
	if (!policy) {
		dev_warn(dev, "EM: Access to CPUFreq policy failed");
		return;
	}

	for (i = 0; i < pd->nr_perf_states; i++) {
		if (!(table[i].flags & EM_PERF_STATE_INEFFICIENT))
			continue;

		if (!cpufreq_table_set_inefficient(policy, table[i].frequency))
			found++;
	}

	if (!found) {
		pd->flags &= ~EM_PERF_DOMAIN_SKIP_INEFFICIENCIES;
		return;
	}

	/*
	 * Efficiencies have been installed in CPUFreq, inefficient frequencies
	 * will be skipped. The EM can do the same.
	 */
	pd->flags |= EM_PERF_DOMAIN_SKIP_INEFFICIENCIES;
}

/**
 * em_pd_get() - Return the performance domain for a device
 * @dev : Device to find the performance domain for
 *
 * Returns the performance domain to which @dev belongs, or NULL if it doesn't
 * exist.
 */
struct em_perf_domain *em_pd_get(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev))
		return NULL;

	return dev->em_pd;
}
EXPORT_SYMBOL_GPL(em_pd_get);

/**
 * em_cpu_get() - Return the performance domain for a CPU
 * @cpu : CPU to find the performance domain for
 *
 * Returns the performance domain to which @cpu belongs, or NULL if it doesn't
 * exist.
 */
struct em_perf_domain *em_cpu_get(int cpu)
{
	struct device *cpu_dev;

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev)
		return NULL;

	return em_pd_get(cpu_dev);
}
EXPORT_SYMBOL_GPL(em_cpu_get);

/**
 * em_dev_register_perf_domain() - Register the Energy Model (EM) for a device
 * @dev		: Device for which the EM is to register
 * @nr_states	: Number of performance states to register
 * @cb		: Callback functions providing the data of the Energy Model
 * @cpus	: Pointer to cpumask_t, which in case of a CPU device is
 *		obligatory. It can be taken from i.e. 'policy->cpus'. For other
 *		type of devices this should be set to NULL.
 * @microwatts	: Flag indicating that the power values are in micro-Watts or
 *		in some other scale. It must be set properly.
 *
 * Create Energy Model tables for a performance domain using the callbacks
 * defined in cb.
 *
 * The @microwatts is important to set with correct value. Some kernel
 * sub-systems might rely on this flag and check if all devices in the EM are
 * using the same scale.
 *
 * If multiple clients register the same performance domain, all but the first
 * registration will be ignored.
 *
 * Return 0 on success
 */
int em_dev_register_perf_domain(struct device *dev, unsigned int nr_states,
				struct em_data_callback *cb, cpumask_t *cpus,
				bool microwatts)
{
	unsigned long cap, prev_cap = 0;
	int cpu, ret;

	if (!dev || !nr_states || !cb)
		return -EINVAL;

	/*
	 * Use a mutex to serialize the registration of performance domains and
	 * let the driver-defined callback functions sleep.
	 */
	mutex_lock(&em_pd_mutex);

	if (dev->em_pd) {
		ret = -EEXIST;
		goto unlock;
	}

	if (_is_cpu_device(dev)) {
		if (!cpus) {
			dev_err(dev, "EM: invalid CPU mask\n");
			ret = -EINVAL;
			goto unlock;
		}

		for_each_cpu(cpu, cpus) {
			if (em_cpu_get(cpu)) {
				dev_err(dev, "EM: exists for CPU%d\n", cpu);
				ret = -EEXIST;
				goto unlock;
			}
			/*
			 * All CPUs of a domain must have the same
			 * micro-architecture since they all share the same
			 * table.
			 */
			cap = arch_scale_cpu_capacity(cpu);
			if (prev_cap && prev_cap != cap) {
				dev_err(dev, "EM: CPUs of %*pbl must have the same capacity\n",
					cpumask_pr_args(cpus));

				ret = -EINVAL;
				goto unlock;
			}
			prev_cap = cap;
		}
	}

	ret = em_create_pd(dev, nr_states, cb, cpus);
	if (ret)
		goto unlock;

	if (microwatts)
		dev->em_pd->flags |= EM_PERF_DOMAIN_MICROWATTS;

	em_cpufreq_update_efficiencies(dev, dev->em_pd->table);

	mutex_init(&dev->em_pd->lock);

	em_debug_create_pd(dev);
	dev_info(dev, "EM: created perf domain\n");

unlock:
	mutex_unlock(&em_pd_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(em_dev_register_perf_domain);

/**
 * em_dev_unregister_perf_domain() - Unregister Energy Model (EM) for a device
 * @dev		: Device for which the EM is registered
 *
 * Unregister the EM for the specified @dev (but not a CPU device).
 */
void em_dev_unregister_perf_domain(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev) || !dev->em_pd)
		return;

	if (_is_cpu_device(dev))
		return;

	/*
	 * The mutex separates all register/unregister requests and protects
	 * from potential clean-up/setup issues in the debugfs directories.
	 * The debugfs directory name is the same as device's name.
	 */
	mutex_lock(&em_pd_mutex);
	em_debug_remove_pd(dev);

	/* Safely destroy runtime modifiable EM */
	em_perf_rt_table_set(dev, NULL);

	/* Make sure we don't progress till we free internal rt_table */
	synchronize_rcu();

	kfree(dev->em_pd->table);
	kfree(dev->em_pd);
	dev->em_pd = NULL;
	mutex_unlock(&em_pd_mutex);
}
EXPORT_SYMBOL_GPL(em_dev_unregister_perf_domain);
