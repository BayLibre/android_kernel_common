/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <linux/slab.h>
#include <linux/debug_list.h>

void debug_list_dump_seq(struct debug_list *c, struct seq_file *f)
{
	struct debug_context *dc, *n;
	if (!c->initialized) {
		return;
	}
	read_lock(&c->lock);
	list_for_each_entry_safe(dc, n, &c->head, list) {
		seq_printf(f, "[%6u.%09lu]: ", dc->ts_sec, dc->ts_ns);
		seq_printf(f, "%s\n", dc->content);
	}
	read_unlock(&c->lock);
}

void debug_list_write(struct debug_list *c, const char *fmt, ...)
{
	struct debug_context *new_dc, *del_dc;
	va_list vl;
	u64 ts;

	if (c == NULL || !c->initialized) {
		return;
	}
	ts = ktime_get_ns();
	new_dc = kzalloc(sizeof(*new_dc), GFP_KERNEL);
	new_dc->ts_sec = ts / 1000000000U;
	new_dc->ts_ns = do_div(ts, 1000000000U);

	va_start(vl, fmt);
	vscnprintf(new_dc->content, DEBUG_LIST_WIDTH, fmt, vl);
	va_end(vl);

	write_lock(&c->lock);
	if (c->length >= DEBUG_LIST_SIZE) {
		del_dc = list_entry(c->head.next, struct debug_context, list);
		list_del(&del_dc->list);
		kfree(del_dc);
		c->length--;
	}

	list_add_tail_rcu(&new_dc->list, &c->head);
	c->length++;
	write_unlock(&c->lock);
}
