/* SPDX-License-Identifier: GPL-2.0 */

#ifndef PFS_WORKQUEUE_INTERNAL_H
#define PFS_WORKQUEUE_INTERNAL_H

#include "task_struct.h"

struct worker_pool;

/*
 * The poor guys doing the actual heavy lifting.  All on-duty workers are
 * either serving the manager role, on idle list or on busy hash.  For
 * details on the locking annotation (L, I, X...), refer to workqueue.c.
 *
 * Only to be used in workqueue and async.
 */
struct worker {
	/* on idle list while idle, on busy hash table while busy */
	union {
		struct list_head	entry;	/* L: while idle */
		struct hlist_node	hentry;	/* L: while busy */
	};

	struct work_struct	*current_work;	/* L: work being processed */
	work_func_t		current_func;	/* L: current_work's fn */
	struct pool_workqueue	*current_pwq; /* L: current_work's pwq */
	struct list_head	scheduled;	/* L: scheduled works */

	/* 64 bytes boundary on 64bit, 32 on 32bit */

	struct task_struct	*task;		/* I: worker task */
	struct worker_pool	*pool;		/* A: the associated pool */
						/* L: for rescuers */
	struct list_head	node;		/* A: anchored at pool->workers */
						/* A: runs through worker->node */

	unsigned long		last_active;	/* L: last active timestamp */
	unsigned int		flags;		/* X: flags */
	int			id;		/* I: worker id */
	int			sleeping;	/* None */

	/*
	 * Opaque string set with work_set_desc().  Printed out with task
	 * dump for debugging - WARN, BUG, panic or sysrq.
	 */
	char			desc[WORKER_DESC_LEN];
};

static inline struct worker *current_wq_worker(void)
{
    if (current_task) {
        return (struct worker *)kthread_data(current_task);
    }
    return NULL;
}

#endif /* PFS_WORKQUEUE_INTERNAL_H */
