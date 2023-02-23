#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/oom.h>
#include <linux/tracepoint.h>
#include <trace/events/oom.h>
#include <linux/gfp.h>
#include <asm/uaccess.h>

#define BUF_LEN 30            /* Max length of the message from the device */

static wait_queue_head_t atest_wq;
static DEFINE_SPINLOCK(atest_lock);
static uint32_t atest_level = 0;
static uint32_t atest_threshold = 0;
static int atest_event = 0;
static char msg[BUF_LEN];    /* The msg the device will give when asked    */
static char *msg_Ptr;

static void param_set_attr(uint32_t level, uint32_t threshold)
{
	bool trigger;

	spin_lock(&atest_lock);
	atest_level = level;
	atest_threshold = threshold;
	trigger = level > threshold && cmpxchg(&atest_event, 0, 1) == 0;
	spin_unlock(&atest_lock);

	if (trigger) {
		printk("Triggering Android test event\n");

		/**
		* Generate OOM kill to verify that we are calling mark_victim_probe()
		* out_of_memory() -> Required to temporarily export it from `mm/oom_kill.c`
		*/
		out_of_memory(&(struct oom_control) {
			.zonelist = node_zonelist(first_memory_node, GFP_KERNEL),
			.nodemask = NULL,
			.memcg = NULL,
			.gfp_mask = GFP_KERNEL,
			.order = -1,
		});
	}
}

static int param_set_level(const char *s, const struct kernel_param *kp)
{
	uint32_t val;
	int ret;

	if (!s)
		return 0;

	printk("New level received: %s\n", s);

	ret = kstrtouint(s, 10, &val);
	if (ret)
		return ret;

	if (val == atest_level)
		return 0;

	printk("New level %u accepted\n", val);

	param_set_attr(val, atest_threshold);

	return 0;
}

static const struct kernel_param_ops param_ops_level = {
	.set = param_set_level,
	.get = param_get_uint,
};

module_param_cb(level, &param_ops_level, &atest_level, 0644);

static int param_set_threshold(const char *s, const struct kernel_param *kp)
{
	uint32_t val;
	int ret;

	if (!s)
		return 0;

	printk("New threshold received: %s\n", s);

	ret = kstrtouint(s, 10, &val);
	if (ret)
		return ret;

	if (val == atest_threshold)
		return 0;

	printk("New threshold %u accepted\n", val);

	param_set_attr(atest_level, val);

	return 0;
}

static const struct kernel_param_ops param_ops_threshold = {
	.set = param_set_threshold,
	.get = param_get_uint,
};

module_param_cb(threshold, &param_ops_threshold, &atest_threshold, 0644);

static __poll_t atest_poll(struct file *file, poll_table *wait)
{
	__poll_t res = DEFAULT_POLLMASK;

	poll_wait(file, &atest_wq, wait);
	if (cmpxchg(&atest_event, 1, 0) == 1)
		res |= EPOLLPRI;

	return res;
}

static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
   /* Number of bytes actually written to the buffer */
   int bytes_read = 0;

   /* If we're at the end of the message, return 0 signifying end of file */
   if (*msg_Ptr == 0) return 0;

   /* Actually put the data into the buffer */
   while (length && *msg_Ptr)  {
         put_user(*(msg_Ptr++), buffer++);

         length--;
         bytes_read++;
   }

   return bytes_read;
}

static int device_open(struct inode *inode, struct file *file)
{
   msg_Ptr = msg;
   return 0;
}


static const struct file_operations atest_fops = {
	.owner = THIS_MODULE,
	.poll = atest_poll,
	.read = device_read,
	.open = device_open,
};

static struct miscdevice atest_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "atestmod",
	.mode		= 0666,
	.fops		= &atest_fops,
};

static void mark_victim_probe(void *data, pid_t pid)
{
	sprintf(msg, "oom_kill,%d", pid);	// Store message in buffer
	wake_up_interruptible(&atest_wq);
}

static int __init atestmod_start(void)
{
	int result;

	printk(KERN_INFO "Loading Android test module...\n");
	result = misc_register(&atest_misc);
	if (result < 0)
		return -ENODEV;

	init_waitqueue_head(&atest_wq);
	if (!register_trace_mark_victim(mark_victim_probe, NULL))
		printk(KERN_INFO "Hooked a probe to the mark_victim tracepoint\n");
	else
		printk(KERN_WARNING "Failed to hook a probe to the mark_victim tracepoint\n");

	printk(KERN_INFO "Android test module loaded\n");

	return 0;
}

static void __exit atestmod_end(void)
{
	printk(KERN_INFO "Unloading Android test module\n");

	if (unregister_trace_mark_victim(mark_victim_probe, NULL))
		printk(KERN_WARNING "Failed to unhook a probe from the mark_victim tracepoint\n");

	misc_deregister(&atest_misc);
}

module_init(atestmod_start);
module_exit(atestmod_end);
MODULE_LICENSE("GPL");