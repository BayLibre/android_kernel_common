/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ashmem

#if !defined(_ASHMEM_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _ASHMEM_TRACE_H

#include <linux/tracepoint.h>

#define ioctl_name(ioctl_cmd) { ioctl_cmd, #ioctl_cmd }
#define print_ioctl_name(ioctl_cmd) \
	__print_symbolic(ioctl_cmd, \
			 ioctl_name(ASHMEM_SET_SIZE), \
			 ioctl_name(ASHMEM_GET_NAME), \
			 ioctl_name(ASHMEM_SET_SIZE), \
			 ioctl_name(ASHMEM_GET_SIZE), \
			 ioctl_name(ASHMEM_SET_PROT_MASK), \
			 ioctl_name(ASHMEM_GET_PROT_MASK), \
			 ioctl_name(ASHMEM_PIN), \
			 ioctl_name(ASHMEM_UNPIN), \
			 ioctl_name(ASHMEM_GET_PIN_STATUS), \
			 ioctl_name(ASHMEM_PURGE_ALL_CACHES), \
			 ioctl_name(ASHMEM_GET_FILE_ID))

#define print_prot_name(prot) \
	__print_symbolic(prot, \
			 { PROT_READ, "PROT_READ" }, \
			 { PROT_EXEC, "PROT_EXEC" }, \
			 { PROT_READ | PROT_EXEC, "PROT_READ | PROT_EXEC" })

TRACE_EVENT(deprecated_feat_unset_prot,

	    TP_PROTO(struct task_struct *t, int unset_prot, const char *buf_name),

	    TP_ARGS(t, unset_prot, buf_name),

	    TP_STRUCT__entry(
		__string(comm, t->comm)
		__field(pid_t, pid)
		__field(int, unset_prot)
		__string(name, buf_name)
	    ),

	    TP_fast_assign(
		__assign_str(comm, t->comm);
		__entry->pid = t->pid;
		__entry->unset_prot = unset_prot;
		__assign_str(name, buf_name);
	),

	TP_printk("Task %s-%d unset prot(s): %s for buffer %s\n", __get_str(comm), __entry->pid,
		  print_prot_name(__entry->unset_prot), __get_str(name))
);

TRACE_EVENT(deprecated_feat_unpin_cmd,

	    TP_PROTO(struct task_struct *t, unsigned int ioctl_cmd, const char *buf_name),

	    TP_ARGS(t, ioctl_cmd, buf_name),

	    TP_STRUCT__entry(
		__string(comm, t->comm)
		__field(pid_t, pid)
		__field(unsigned int, ioctl_cmd)
		__string(name, buf_name)
	    ),

	    TP_fast_assign(
		__assign_str(comm, t->comm);
		__entry->pid = t->pid;
		__entry->ioctl_cmd = ioctl_cmd;
		__assign_str(name, buf_name);
	),

	TP_printk("Task %s-%d used unpinning cmd: %s on buffer: %s\n", __get_str(comm),
		  __entry->pid, print_ioctl_name(__entry->ioctl_cmd), __get_str(name))
);

TRACE_EVENT(memfd_ashmem_compat_usage,

	    TP_PROTO(struct task_struct *t, unsigned int ioctl_cmd, const char *buf_name),

	    TP_ARGS(t, ioctl_cmd, buf_name),

	    TP_STRUCT__entry(
		__string(comm,	t->comm)
		__field(pid_t, pid)
		__field(unsigned int, ioctl_cmd)
		__string(name, buf_name)
	    ),

	    TP_fast_assign(
		__assign_str(comm, t->comm);
		__entry->pid = t->pid;
		__entry->ioctl_cmd = ioctl_cmd;
		__assign_str(name, buf_name);
	),

	TP_printk("Task %s-%d used memfd-ashmem compat layer ioctl: %s buffer: %s", __get_str(comm),
		  __entry->pid, print_ioctl_name(__entry->ioctl_cmd), __get_str(name))
);
#endif /* _TRACE_ASHMEM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
