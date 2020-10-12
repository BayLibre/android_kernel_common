==================================
Using the Linux Kernel debug_list
==================================

This document introduces Linux Kernel debug_list and their use. It provides
examples of how to use debug_list in the kernel to collect log and provides
some usage examples.


Purpose of debug_list
----------------------
A debug_list placed in code provides a logger mechanism to log debug messages
you planed to record. These log messages include timestamp will be cached into
a list structure with fixed capacities and can be dumped to seq_file,
you can declare a debugfs node in your subsystem driver and dump these cached
debug messages to user space at the timing you wnated.
The common use case is when you detect your subsystem crash in hal/stack code
in user space, you can read these cached debug messages from kernel driver debugfs
via read related system call.


Usage
-----

In order to use debug_list, you should include #include <linux/debug_list.h>
, declare static DEBUG_LIST(debug_list) and place TRY_INIT_DEBUG_LIST(debug_list)
in your driver probe or initialize function in your driver c files, first.

In your subsystem_driver.c ::

	#include <linux/debug_list.h>

	static DEBUG_LIST(debug_list);

	void subsys_probe(...)
	{
		...
		TRY_INIT_DEBUG_LIST(debug_list);
		...
	}

Where :
  - subsys is the name of your subsystem driver.

then you can log your debug messages using debug_list_write function, just put
the debug_list_write() function at any important locations you want to record for
tracking in the code.
Where :
  - the function prototype
    void debug_list_write(struct debug_list *c, const char *fmt, ...)

for example:

to log the values of packet buffer.
debug_list_write(&debug_list, "%s: len %3d, %s", __func__, lenth, buf);

to log the critical error messages.
debug_list_write(&debug_list, "command 0x%4.4x tx timeout", opcode);


Finally, you can dump these debug messages you collected in debug_list to user
space via debugfs file node using debug_list_dump_seq function.
Where :
  - the function prototype
    void debug_list_dump_seq(struct debug_list *c, struct seq_file *f);

for example:

struct dentry *subsys_debugfs;

static int subsys_debug_show(struct seq_file *f, void *x)
{
	seq_printf(f, "\n<< %s >>\n", __func__);
  debug_list_dump_seq(&debug_list, f);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(subsys_debug);

Where :
  - subsys is the name of your subsystem driver.
