/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Scheduler internal types and methods:
 */

/* pause externs */
extern void watchdog_enable(unsigned int cpu);
extern void watchdog_disable(unsigned int cpu);
extern void migrate_tasks(struct rq *dead_rq, struct rq_flags *rf,
			  bool migrate_pinned_tasks);
extern void calc_load_migrate(struct rq *rq);

