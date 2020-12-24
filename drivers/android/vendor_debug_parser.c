// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/sched.h>

#include "../../kernel/sched/sched.h"

static void dump_cfs_rq(struct seq_buf *runq_buf, struct cfs_rq *cfs,
			struct task_struct *curr, int *offset);

#ifdef CONFIG_FAIR_GROUP_SCHED
static struct cfs_rq *get_se_cfs_rq(struct sched_entity *se_p)
{
	return se_p->my_q;
}
#else
static struct cfs_rq *get_se_cfs_rq(struct sched_entity *se_p)
{
	return NULL;
}
#endif

#ifdef CONFIG_RT_GROUP_SCHED
static struct rt_rq *get_se_rt_rq(struct sched_rt_entity *se_p)
{
	return se_p->my_q;
}
#else
static struct rt_rq *get_se_rt_rq(struct sched_rt_entity *se_p)
{
	return NULL;
}
#endif

static void dump_align(struct seq_buf *runq_buf, int tab_offset)
{
	while (tab_offset--)
		seq_buf_printf(runq_buf, " | ");
	seq_buf_printf(runq_buf, " |--");
}

static void dump_task_info(struct seq_buf *runq_buf, struct task_struct *task,
			   char *status, struct task_struct *curr, int *offset)
{
	struct sched_entity *se;

	dump_align(runq_buf, *offset);
	if (!task) {
		seq_buf_printf(runq_buf, "%s : None(0)\n", status);
		return;
	}

	se = &task->se;
	if (task == curr) {
		seq_buf_printf(runq_buf,
			       "[status: curr] pid: %d comm: %s preempt: %#x\n",
			       task_pid_nr(task), task->comm,
			       task->thread_info.preempt_count);
		return;
	}

	seq_buf_printf(runq_buf,
		       "[status: %s] pid: %d tsk: %#lx comm: %s stack: %#lx",
		       status, task_pid_nr(task),
		       (unsigned long)task,
		       task->comm, (unsigned long)task->stack);
	seq_buf_printf(runq_buf,
		       " prio: %d aff: %*pb",
		       task->prio, cpumask_pr_args(&task->cpus_mask));
	seq_buf_printf(runq_buf,
		       " vrun: %lu arr: %lu sum_ex: %lu\n",
		       (unsigned long)se->vruntime,
		       (unsigned long)se->exec_start,
		       (unsigned long)se->sum_exec_runtime);
}

static void dump_cgroup_state(struct seq_buf *runq_buf, char *status,
			      struct sched_entity *se_p,
			      struct task_struct *curr, int *offset)
{
	struct task_struct *task;
	struct cfs_rq *my_q = NULL;
	unsigned int nr_running;

	if (!se_p) {
		dump_task_info(runq_buf, NULL, status, NULL, offset);
		return;
	}

	my_q = get_se_cfs_rq(se_p);

	if (!my_q) {
		task = container_of(se_p, struct task_struct, se);
		dump_task_info(runq_buf, task, status, curr, offset);
		return;
	}

	nr_running = my_q->nr_running;
	dump_align(runq_buf, *offset);
	seq_buf_printf(runq_buf, "%s: %d process is grouping\n",
		       status, nr_running);

	(*offset)++;
	dump_cfs_rq(runq_buf, my_q, curr, offset);
	(*offset)--;
}

static void dump_cfs_node_func(struct seq_buf *runq_buf, struct rb_node *node,
			       struct task_struct *curr, int *offset)
{
	struct sched_entity *se_p = container_of(node, struct sched_entity,
						 run_node);

	dump_cgroup_state(runq_buf, "pend", se_p, curr, offset);
}

static void rb_walk_cfs(struct seq_buf *runq_buf, struct rb_root_cached *p,
			struct task_struct *curr, int *offset)
{
	int max_walk = 200;	/* Bail out, in case of loop */
	struct rb_node *leftmost = p->rb_leftmost;
	struct rb_root *root = &p->rb_root;
	struct rb_node *rb_node = rb_first(root);

	if (!leftmost)
		return;
	while (rb_node && max_walk--) {
		dump_cfs_node_func(runq_buf, rb_node, curr, offset);
		rb_node = rb_next(rb_node);
	}
}

static void dump_cfs_rq(struct seq_buf *runq_buf, struct cfs_rq *cfs,
			struct task_struct *curr, int *offset)
{
	struct rb_root_cached *rb_root_cached_p = &cfs->tasks_timeline;

	dump_cgroup_state(runq_buf, "curr", cfs->curr, curr, offset);
	dump_cgroup_state(runq_buf, "next", cfs->next, curr, offset);
	dump_cgroup_state(runq_buf, "last", cfs->last, curr, offset);
	dump_cgroup_state(runq_buf, "skip", cfs->skip, curr, offset);
	rb_walk_cfs(runq_buf, rb_root_cached_p, curr, offset);
}

static void dump_rt_rq(struct seq_buf *runq_buf, struct rt_rq *rt_rq,
		       struct task_struct *curr, int *offset)
{
	struct rt_prio_array *array = &rt_rq->active;
	struct sched_rt_entity *rt_se;
	int idx;

	if (bitmap_empty(array->bitmap, MAX_RT_PRIO))
		return;

	idx = sched_find_first_bit(array->bitmap);
	while (idx < MAX_RT_PRIO) {
		list_for_each_entry(rt_se, array->queue + idx, run_list) {
			struct task_struct *p;

			if (get_se_rt_rq(rt_se))
				continue;

			p = container_of(rt_se, struct task_struct, rt);
			dump_task_info(runq_buf, p, "pend", curr, offset);
		}
		idx = find_next_bit(array->bitmap, MAX_RT_PRIO, idx + 1);
	}
}

void android_dump_runqueues(struct seq_buf *runq_buf)
{
	int cpu;
	struct rq *rq;
	struct rt_rq *rt;
	struct cfs_rq *cfs;
	int tab_offset = 0;

	if (!oops_in_progress || !runq_buf)
		return;

	for_each_possible_cpu(cpu) {
		rq = cpu_rq(cpu);
		rt = &rq->rt;
		cfs = &rq->cfs;

		seq_buf_printf(runq_buf, "CPU%d %d process is running\n",
			       cpu, rq->nr_running);
		dump_task_info(runq_buf, cpu_curr(cpu), "curr", NULL,
			       &tab_offset);

		seq_buf_printf(runq_buf, "CFS %d process is pending\n",
			       cfs->nr_running);
		dump_cfs_rq(runq_buf, cfs, cpu_curr(cpu), &tab_offset);

		seq_buf_printf(runq_buf, "RT %d process is pending\n",
			       rt->rt_nr_running);
		dump_rt_rq(runq_buf, rt, cpu_curr(cpu), &tab_offset);

		seq_buf_printf(runq_buf, "\n");
	}
}

EXPORT_SYMBOL_GPL(android_dump_runqueues);
