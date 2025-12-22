/*
 * fault_stats.c
 *
 * Measure handle_mm_fault() wall clock latency.
 *
 * Use kretprobes to hook function entry and exit.
 * Distinguish between "Minor" faults (CPU-bound) and "Major" faults
 * (I/O-bound) to identify storage stalls.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/atomic.h>
#include <linux/sched.h>

/*
 * Define bucket IDs for the histogram.
 * Cover range from nanoseconds (cache hits) to seconds (stalls).
 */
#define BUCKET_0_100NS		0
#define BUCKET_100_500NS	1
#define BUCKET_500NS_1US	2
#define BUCKET_1_10US		3
#define BUCKET_10_100US		4
#define BUCKET_100US_1MS	5
#define BUCKET_1_10MS		6
#define BUCKET_10_100MS		7
#define BUCKET_100MS_1S		8
#define BUCKET_OVER_1S		9
#define NUM_BUCKETS		10

static const char *bucket_labels[] = {
	"0-100ns",
	"100-500ns",
	"500ns-1us",
	"1-10us",
	"10-100us",
	"100us-1ms",
	"1ms-10ms",
	"10ms-100ms",
	"100ms-1s",
	">1s"
};

/*
 * Store fault counters.
 * Use atomic64_t to ensure SMP safety during concurrent page faults.
 */
struct fault_stats_data {
	atomic64_t minor[NUM_BUCKETS];
	atomic64_t major[NUM_BUCKETS];
};

static struct fault_stats_data stats;
static struct kobject *fault_kobj;

/*
 * Classify duration into histogram buckets.
 * Input duration is in nanoseconds.
 */
static int get_bucket(s64 duration_ns)
{
	/* Nanosecond range (L1/L2 Cache) */
	if (duration_ns < 100)  return BUCKET_0_100NS;
	if (duration_ns < 500)  return BUCKET_100_500NS;
	if (duration_ns < 1000) return BUCKET_500NS_1US;

	/* Microsecond range */
	if (duration_ns < 10000)   return BUCKET_1_10US;
	if (duration_ns < 100000)  return BUCKET_10_100US;
	if (duration_ns < 1000000) return BUCKET_100US_1MS;

	/* Millisecond range */
	if (duration_ns < 10000000)   return BUCKET_1_10MS;    /* < 10ms */
	if (duration_ns < 100000000)  return BUCKET_10_100MS;  /* < 100ms */
	if (duration_ns < 1000000000) return BUCKET_100MS_1S;  /* < 1s */

	/* Stalls > 1s */
	return BUCKET_OVER_1S;
}

/*
 * Entry Handler: Capture timestamp before function execution.
 * Save current ktime in the probe instance data.
 */
static int entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	ktime_t *start_time = (ktime_t *)ri->data;
	*start_time = ktime_get();
	return 0;
}

/*
 * Return Handler: Calculate duration and record stats.
 * Inspect return value to classify as Major or Minor fault.
 */
static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	ktime_t *start_time = (ktime_t *)ri->data;
	ktime_t end_time = ktime_get();
	s64 duration_ns = ktime_to_ns(ktime_sub(end_time, *start_time));
	int bucket = get_bucket(duration_ns);

	/*
	 * Retrieve return value (x0 on ARM64).
	 * Check VM_FAULT_MAJOR bit to detect I/O activity.
	 */
	int retval = regs_return_value(regs);

	if (retval & VM_FAULT_MAJOR) {
		atomic64_inc(&stats.major[bucket]);
	} else {
		atomic64_inc(&stats.minor[bucket]);
	}

	return 0;
}

/*
 * Configure kretprobe structure.
 * Set maxactive to 64 to handle concurrent faults on SMP systems.
 * Allocate sizeof(ktime_t) for per-instance timestamp storage.
 */
static struct kretprobe fault_kretprobe = {
	.handler	= ret_handler,
	.entry_handler	= entry_handler,
	.data_size	= sizeof(ktime_t),
	.maxactive	= 64,
	.kp.symbol_name	= "handle_mm_fault",
};

/*
 * Sysfs Show: Format stats as CSV.
 * Output range labels and counters for both fault types.
 */
static ssize_t histogram_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	int i;
	int len = 0;

	len += sprintf(buf + len, "Range,Minor_Count,Major_Count\n");

	for (i = 0; i < NUM_BUCKETS; i++) {
		len += sprintf(buf + len, "%s,%lld,%lld\n",
			       bucket_labels[i],
			       atomic64_read(&stats.minor[i]),
			       atomic64_read(&stats.major[i]));
	}

	return len;
}

/*
 * Sysfs Store: Reset stats on write.
 * Clear all atomic counters to zero.
 */
static ssize_t histogram_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	int i;
	for (i = 0; i < NUM_BUCKETS; i++) {
		atomic64_set(&stats.minor[i], 0);
		atomic64_set(&stats.major[i], 0);
	}
	pr_info("fault_stats: Histogram reset.\n");
	return count;
}

static struct kobj_attribute histogram_attribute =
	__ATTR(histogram, 0644, histogram_show, histogram_store);

static struct attribute *fault_attrs[] = {
	&histogram_attribute.attr,
	NULL,
};

static const struct attribute_group fault_attr_group = {
	.attrs = fault_attrs,
};

static int __init fault_stats_init(void)
{
	int ret;

	/* Register kretprobe */
	ret = register_kretprobe(&fault_kretprobe);
	if (ret < 0) {
		pr_err("fault_stats: register_kretprobe failed: %d\n", ret);
		return ret;
	}

	/* Create Sysfs directory */
	fault_kobj = kobject_create_and_add("fault_stats", kernel_kobj);
	if (!fault_kobj) {
		unregister_kretprobe(&fault_kretprobe);
		return -ENOMEM;
	}

	ret = sysfs_create_group(fault_kobj, &fault_attr_group);
	if (ret) {
		kobject_put(fault_kobj);
		unregister_kretprobe(&fault_kretprobe);
		return ret;
	}

	return 0;
}

static void __exit fault_stats_exit(void)
{
	sysfs_remove_group(fault_kobj, &fault_attr_group);
	kobject_put(fault_kobj);
	unregister_kretprobe(&fault_kretprobe);
}

/*
 * Use late_initcall for obj-y compilation.
 * Ensure Sysfs and Kprobes subsystems are ready before initialization.
 */
late_initcall(fault_stats_init);
module_exit(fault_stats_exit);
