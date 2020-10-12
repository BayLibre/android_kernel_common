/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>

/* Debug logging data structures */
#define DEBUG_LIST(x) struct debug_list x;

#define DEBUG_LIST_SIZE 256
#define DEBUG_LIST_WIDTH 128

#define INIT_DEBUG_LIST(c)\
	do {\
		c.initialized = true;\
		c.length = 0;\
		rwlock_init(&c.lock);\
		INIT_LIST_HEAD(&c.head);\
	} while (0)

#define TRY_INIT_DEBUG_LIST(c)\
	do {\
		if (!c.initialized)\
			INIT_DEBUG_LIST(c);\
	} while (0)

struct debug_list {
	rwlock_t lock;
	struct list_head head;
	int length;
	bool initialized;
};

struct debug_context {
	uint64_t ts_sec;
	unsigned long ts_ns;
	char content[DEBUG_LIST_WIDTH];
	struct list_head list;
};

void debug_list_dump_seq(struct debug_list *c, struct seq_file *f);
void debug_list_write(struct debug_list *c, const char *fmt, ...);
