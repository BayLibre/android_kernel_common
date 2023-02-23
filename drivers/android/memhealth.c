// SPDX-License-Identifier: GPL-2.0
/* memhealth.c
 *
 * Copyright (C) 2023 Google, Inc.
 */

#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/oom.h>
#include <linux/sched.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <trace/events/oom.h>

#define MEMHEALTH_DIRECTORY "memhealth"
#define OOM_VICTIM_LIST_ENTRY "oom_victim_list"

static DEFINE_SPINLOCK(memhealth_lock);
static wait_queue_head_t memhealth_wq;
static struct list_head oom_victim_list;

struct oom_victim {
	pid_t pid;
	uid_t uid;
	char process_name[TASK_COMM_LEN];
	unsigned long timestamp_ms;
	struct list_head list;
};

#define OOM_VICTIM_LIST_MAX_SIZE PAGE_SIZE/sizeof(struct oom_victim)

static int oom_victim_count;
static int oom_victim_removed_count;

static int add_oom_victim_to_list(pid_t pid, unsigned long timestamp)
{
	struct oom_victim *new_node, *head, *new_head;
	struct task_struct *task;
	const struct cred *cred;
	struct pid *pid_struct;
	ssize_t bytes_written_process_name;

	/**
	* Function could be called while a spinlock is being held, we want to
	* prevent blocking while allocating for the new node
	*/
	new_node = kmalloc(sizeof(*new_node), GFP_ATOMIC);
	if (!new_node) {
		pr_err("memhealth failed to allocate memory for OOM victim node\n");
		return -ENOMEM;
	}

	pid_struct = find_get_pid(pid);
	if (!pid_struct) {
		pr_err("memhealth failed to find pid %d\n", pid);
		kfree(new_node);
		return -EINVAL;
	}

	task = get_pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);
	if (!task) {
		pr_err("memhealth failed to find task with pid %d \n", pid);
		kfree(new_node);
		return -EINVAL;
	}

	cred = get_task_cred(task);
	if (!cred) {
		pr_err("memhealth failed to find credentials\n");
		kfree(new_node);
		return -EINVAL;
	}

	bytes_written_process_name = strscpy_pad(new_node->process_name,
		task->comm, TASK_COMM_LEN);
	if (bytes_written_process_name < 0) {
		pr_err(
			"memhealth failed to copy process name to new oom victim node\n");
		put_task_struct(task);
		put_cred(cred);
		kfree(new_node);
		return bytes_written_process_name;
	}

	put_task_struct(task);

	new_node->pid = pid;
	new_node->timestamp_ms = timestamp;
	new_node->uid = cred->uid.val;

	put_cred(cred);

	spin_lock(&memhealth_lock);
	list_add_tail(&new_node->list, &oom_victim_list);
	oom_victim_count++;
	if (oom_victim_count - oom_victim_removed_count >=
			OOM_VICTIM_LIST_MAX_SIZE) {
		head = list_first_entry(&oom_victim_list, struct oom_victim, list);
		new_head = list_next_entry(head, list);

		list_del(&head->list);
		oom_victim_list.next = &new_head->list;
		new_head->list.prev = &oom_victim_list;

		kfree(head);
		oom_victim_removed_count++;
	}
	spin_unlock(&memhealth_lock);
	return 0;
}

static void mark_victim_probe(void *data, pid_t pid)
{
	unsigned long timestamp_ms = get_jiffies_64() * 1000 / HZ;
	if (add_oom_victim_to_list(pid, timestamp_ms) < 0) {
		pr_err("memhealth failed to add new OOM killer victim to list\n");
		return;
	}
	wake_up_interruptible(&memhealth_wq);
}

/* OOM Events */
static struct proc_dir_entry *proc_mem_health_dir;

static ssize_t oom_victim_list_read(struct file *file, char __user *buf,
			  size_t count, loff_t *offset)
{
	int bytes_copied = 0;
	char *kernelBuffer;
	struct oom_victim *entry;
	loff_t index = 0, lastIndexRead = 0;

	kernelBuffer = kmalloc(count, GFP_KERNEL);
	if (!kernelBuffer) {
		pr_err("memhealth failed to allocate memory for oom message\n");
		return -ENOMEM;
	}

	spin_lock(&memhealth_lock);
	lastIndexRead = *offset - oom_victim_removed_count;
	/* In the case we deleted/added more nodes than previously read */
	if (lastIndexRead < 0)
		lastIndexRead = 0;

	list_for_each_entry(entry, &oom_victim_list, list) {
		if (index >= lastIndexRead) {
			char nextMsgLine[100];
			int temp_size = snprintf(nextMsgLine, 100,
				"%d %lu %u %s\n",
				entry->pid, entry->timestamp_ms, entry->uid, entry->process_name
			);

			if (temp_size + bytes_copied >= count)
				break;

			bytes_copied += snprintf(kernelBuffer + bytes_copied,
				count - bytes_copied, nextMsgLine);
		}
		index++;
	}
	spin_unlock(&memhealth_lock);

	if (index > lastIndexRead)
		*offset = index;

	if (copy_to_user(buf, kernelBuffer, count)) {
		pr_err("memhealth failed to copy oom_victim to userspace buffer\n");
		kfree(kernelBuffer);
		return -EFAULT;
	}

	kfree(kernelBuffer);
	return bytes_copied;
}

/* Write is for testing purpose only, helps to generate oom kills */
static ssize_t oom_victim_list_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos) {
	const int total_oom_victims_generated = 1;
	pr_info("memhealth generating %d oom victims\n",
		total_oom_victims_generated);
	for (int i = 0; i < total_oom_victims_generated; i++) {
			out_of_memory(&(struct oom_control) {
				.zonelist = node_zonelist(first_memory_node, GFP_KERNEL),
				.nodemask = NULL,
				.memcg = NULL,
				.gfp_mask = GFP_KERNEL,
				.order = -1,
		});
	}
	return count;
}

static __poll_t oom_victim_list_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = DEFAULT_POLLMASK;
	int last_size_polled;

	poll_wait(filp, &memhealth_wq, wait);

	if (list_empty(&oom_victim_list))
		return mask;

	last_size_polled = (int)(uintptr_t)filp->private_data;

	if (last_size_polled != oom_victim_count) {
		filp->private_data = (void*)(uintptr_t)oom_victim_count;
		mask |= EPOLLPRI;
	}

	return mask;
}

static int oom_victim_list_open(struct inode *inode, struct file *file) {
	file->private_data = 0;
	return 0;
}

static const struct proc_ops oom_victims_list_proc_ops = {
	.proc_read	= oom_victim_list_read,
	.proc_poll = oom_victim_list_poll,
	.proc_write = oom_victim_list_write,
	.proc_open = oom_victim_list_open,
};

static int __init memhealthmod_start(void)
{
	struct proc_dir_entry *entry;

	pr_info("Loading Android memhealth module...\n");

	proc_mem_health_dir = proc_mkdir(MEMHEALTH_DIRECTORY, NULL);
	if (!proc_mem_health_dir) {
		pr_err(
			"memhealth failed to create directory (%s)\n", MEMHEALTH_DIRECTORY);
		return -ENOMEM;
	}

	entry = proc_create(OOM_VICTIM_LIST_ENTRY, 0, proc_mem_health_dir,
				&oom_victims_list_proc_ops);
	if (!entry) {
		pr_err("memhealth failed to create proc entry: %s\n",
			OOM_VICTIM_LIST_ENTRY);
		remove_proc_entry(MEMHEALTH_DIRECTORY, NULL);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&oom_victim_list);
	init_waitqueue_head(&memhealth_wq);
	oom_victim_count = 0;
	oom_victim_removed_count = 0;

	if (!register_trace_mark_victim(mark_victim_probe, NULL))
		pr_info("memhealth hooked a probe to the mark_victim tracepoint\n");
	else
		pr_warn(
			"memhealth failed to hook a probe to the mark_victim tracepoint\n");

	pr_info("Android memhealth module loaded\n");

	return 0;
}

static void __exit memhealthmod_end(void)
{
	struct oom_victim *entry, *tmp;

	pr_info("Unloading Android memhealth module\n");

	if (unregister_trace_mark_victim(mark_victim_probe, NULL))
		pr_warn("memhealth failed to unhook a probe from the mark_victim tracepoint\n");

	list_for_each_entry_safe(entry, tmp, &oom_victim_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	remove_proc_entry(OOM_VICTIM_LIST_ENTRY, proc_mem_health_dir);
	remove_proc_entry(MEMHEALTH_DIRECTORY, NULL);
}

module_init(memhealthmod_start);
module_exit(memhealthmod_end);

MODULE_LICENSE("GPL");
