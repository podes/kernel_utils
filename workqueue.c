#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <search.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "workqueue.h"
#include "workqueue_internal.h"

#include "atomic.h"
#include "barrier.h"
#include "bug.h"
#include "compiler.h"
#include "completion.h"
#include "fnv_hash.h"
#include "hashtable.h"
#include "kernel.h"
#include "processor.h"
#include "safe_mutex.h"
#include "time_util.h"
#include "unr.h"
#include "task_struct.h"
#include "log.h"
#include "mem.h"

enum {
    /*
     * worker_pool flags
     */
    POOL_MANAGER_ACTIVE	= 1 << 0,	/* being managed */

    /* worker flags */
    WORKER_DIE		= 1 << 1,	/* die die die */
    WORKER_IDLE		= 1 << 2,	/* is idle */
    WORKER_PREP		= 1 << 3,	/* preparing to run works */

    WORKER_NOT_RUNNING	= WORKER_PREP,

    NR_STD_WORKER_POOLS	= 2,		/* # standard pools per cpu */

    UNBOUND_POOL_HASH_ORDER	= 6,		/* hashed by pool->attrs */
    BUSY_WORKER_HASH_ORDER	= 6,		/* 64 pointers */

    MAX_IDLE_WORKERS_RATIO	= 4,		/* 1/4 of busy can be idle */
    IDLE_WORKER_TIMEOUT	= 300 * PFS_HZ,	/* keep idle ones for 5 mins */

    CREATE_COOLDOWN		= PFS_HZ,		/* time to breath after fail */

    WQ_NAME_LEN		= 24,
};

#define ____cacheline_aligned   __aligned(CACHELINE_SIZE)

/*
 * Structure fields follow one of the following exclusion rules.
 *
 * I: Modifiable by initialization/destruction paths and read-only for
 *    everyone else.
 *
 * L: pool->lock protected.  Access with pool->lock held.
 *
 * X: During normal operation, modification requires pool->lock and should
 *    be done only from local cpu.
 *
 * A: wq_pool_attach_mutex protected.
 *
 * PL: wq_pool_mutex protected.
 *
 * PR: wq_pool_mutex protected for writes. 
 *
 * PW: wq_pool_mutex and wq->mutex protected for writes.  Either for reads.
 *
 * WQ: wq->mutex protected.
 *
 * WR: wq->mutex protected for writes. 
 */

struct worker_pool {
    safe_mutex_t		lock;		/* the pool lock */
    int			        id;		    /* I: pool ID */
    unsigned int		flags;		/* X: flags */

    unsigned long		watchdog_ts;	/* L: watchdog timestamp */

    struct list_head	worklist;	/* L: list of pending works */

    int			nr_workers;	/* L: total number of workers */
    int			nr_idle;	/* L: currently idle workers */

    struct list_head	idle_list;	/* X: list of idle workers */
    struct timer_list	idle_timer;	/* L: worker idle timeout */

    /* a workers is either on busy_hash or idle_list, or the manager */
    DECLARE_HASHTABLE(busy_hash, BUSY_WORKER_HASH_ORDER);
    /* L: hash of busy workers */

    struct worker		*manager;	/* L: purely informational */
    struct list_head	workers;	/* A: attached workers */
    struct completion	*detach_completion; /* all workers detached */

    struct unrhdr		worker_ida;	/* worker IDs for task name */

    struct workqueue_attrs	*attrs;		/* I: worker attributes */
    struct hlist_node	hash_node;	/* PL: unbound_pool_hash node */
    int			refcnt;		/* PL: refcnt for unbound pools */

    pthread_cond_t      manage_cond;

    /*
     * The current concurrency level.  As it's likely to be accessed
     * from other CPUs during try_to_wake_up(), put it in a separate
     * cacheline.
     */
    atomic_t		nr_running;
} ____cacheline_aligned;

/*
 * The per-pool workqueue.  While queued, the lower WORK_STRUCT_FLAG_BITS
 * of work_struct->data are used for flags and the remaining high bits
 * point to the pwq; thus, pwqs need to be aligned at two's power of the
 * number of flag bits.
 */
struct pool_workqueue {
    struct worker_pool	*pool;		/* I: the associated pool */
    struct workqueue_struct *wq;		/* I: the owning workqueue */
    int			work_color;	/* L: current color */
    int			flush_color;	/* L: flushing color */
    int			refcnt;		/* L: reference count */
    int			nr_in_flight[WORK_NR_COLORS];
    /* L: nr of in_flight works */
    int			nr_active;	/* L: nr of active works */
    int			max_active;	/* L: max active works */
    struct list_head	delayed_works;	/* L: delayed works */
    struct list_head	pwqs_node;	/* WR: node on wq->pwqs */
    struct list_head	mayday_node;	/* MD: node on wq->maydays */
    struct list_head    unbound_release_node;
} __aligned( 1 << WORK_STRUCT_FLAG_BITS);

/*
 * Structure used to wait for workqueue flush.
 */
struct wq_flusher {
    struct list_head	list;		/* WQ: list of flushers */
    int			flush_color;	/* WQ: flush color waiting for */
    struct completion	done;		/* flush completion */
};

/*
 * The externally visible workqueue.  It relays the issued work items to
 * the appropriate worker_pool through its pool_workqueues.
 */
struct workqueue_struct {
    struct list_head	pwqs;		/* WR: all pwqs of this wq */
    struct list_head	list;		/* PR: list of all workqueues */

    safe_mutex_t	    mutex;		/* protects this wq */
    int			work_color;	/* WQ: current work color */
    int			flush_color;	/* WQ: current flush color */
    atomic_t		nr_pwqs_to_flush; /* flush in progress */
    struct wq_flusher	*first_flusher;	/* WQ: first flusher */
    struct list_head	flusher_queue;	/* WQ: flush waiters */
    struct list_head	flusher_overflow; /* WQ: flush overflow list */

    int			nr_drainers;	/* WQ: drain in progress */
    int			saved_max_active; /* WQ: saved pwq max_active */

    struct workqueue_attrs	*unbound_attrs;	/* PW: only for unbound wqs */
    struct pool_workqueue	*dfl_pwq;	/* PW: only for unbound wqs */

    char			name[WQ_NAME_LEN]; /* I: workqueue name */

    /* hot fields used during command issue, aligned to cacheline */
    unsigned int		flags ____cacheline_aligned; /* WQ: WQ_* flags */
} ____cacheline_aligned;

struct pool_id_pair {
    int id;
    struct worker_pool *pool;
};

static bool wq_online;			/* can kworkers be created yet? */
static pthread_rwlock_t wq_pool_lock = PTHREAD_RWLOCK_INITIALIZER;	/* protects pools and workqueues list */
static __thread int wq_pool_lock_cnt;
static safe_mutex_t wq_pool_attach_mutex = { PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP, 0 }; /* protects worker attach/detach */
static safe_mutex_t cancel_mutex = { PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP, 0 };
static pthread_cond_t  cancel_cond = PTHREAD_COND_INITIALIZER;
static safe_mutex_t pwq_release_mutex = { PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP, 0 };
static pthread_cond_t  pwq_release_cond = PTHREAD_COND_INITIALIZER;
static pthread_t pwq_release_tid;

static LLIST_HEAD(workqueues);		/* PR: list of all workqueues */
static LLIST_HEAD(pwq_release_list);

/* PL: hash of all unbound pools keyed by pool->attrs */
static DEFINE_HASHTABLE(unbound_pool_hash, UNBOUND_POOL_HASH_ORDER);

/* I: attributes used when instantiating standard unbound pools on demand */
static struct workqueue_attrs *unbound_std_wq_attrs[NR_STD_WORKER_POOLS];

/* I: attributes used when instantiating ordered pools on demand */
static struct workqueue_attrs *ordered_wq_attrs[NR_STD_WORKER_POOLS];

static pthread_rwlock_t pool_id_lock = PTHREAD_RWLOCK_INITIALIZER;
static void *pool_id_root;
struct unrhdr *pool_id_unr;
struct workqueue_struct *system_unbound_wq;

static int worker_thread(void *__worker);
static void free_workqueue_attrs(struct workqueue_attrs *attrs);

#define for_each_pwq(pwq, wq)						\
	list_for_each_entry((pwq), &(wq)->pwqs, pwqs_node)

static void
wq_pool_rdlock(void)
{
    always_assert(wq_pool_lock_cnt >= 0);
    pthread_rwlock_rdlock(&wq_pool_lock);
    wq_pool_lock_cnt++;
}

static void
wq_pool_wrlock(void)
{
    always_assert(wq_pool_lock_cnt == 0);
    pthread_rwlock_wrlock(&wq_pool_lock);
    wq_pool_lock_cnt--;
}

static void
wq_pool_unlock(void)
{
    always_assert(wq_pool_lock_cnt != 0);
    if (wq_pool_lock_cnt < 0)
        wq_pool_lock_cnt = 0;
    else
        wq_pool_lock_cnt--;
    pthread_rwlock_unlock(&wq_pool_lock);
}

static void
assert_wq_pool_wrlocked(void)
{
    always_assert(wq_pool_lock_cnt < 0);
}

static void
assert_wq_pool_locked(void)
{
    always_assert(wq_pool_lock_cnt != 0);
}

static bool
wq_pool_is_locked(void)
{
    return wq_pool_lock_cnt != 0;
}

static int
pool_id_compare(const void *a, const void *b)
{
    struct pool_id_pair *ap = (struct pool_id_pair *)a;
    struct pool_id_pair *bp = (struct pool_id_pair *)b;

    return (ap->id > bp->id) - (ap->id < bp->id);
}

/**
 * worker_pool_assign_id - allocate ID and assing it to @pool
 * @pool: the pool pointer of interest
 *
 * Returns 0 if ID in [0, WORK_OFFQ_POOL_NONE) is allocated and assigned
 * successfully, -errno on failure.
 */
static int
worker_pool_assign_id(struct worker_pool *pool)
{
    struct pool_id_pair *pair, **val;
    int id;

    id = alloc_unr(pool_id_unr);
    if (id == -1) {
        return -ERANGE;
    }
    pair = (struct pool_id_pair *)malloc_common(sizeof(*pair));
    if (pair == NULL) {
        free_unr(pool_id_unr, id);
        return -ENOMEM;
    }
    pair->id = id;
    pair->pool = pool;
    pthread_rwlock_wrlock(&pool_id_lock);
    val = (struct pool_id_pair **)tsearch(pair, &pool_id_root, pool_id_compare);
    if (val == NULL) {
        pthread_rwlock_unlock(&pool_id_lock);
        free_common(pair);
        free_unr(pool_id_unr, id);
        return -ENOMEM;
    }
    if (*val != pair) {
        fprintf(stderr, "%s inconsistent searching-tree\n", __func__);
        abort();
    }
    pthread_rwlock_unlock(&pool_id_lock);
    return 0;
}

static struct worker_pool *
worker_pool_find(int pool_id)
{
    struct pool_id_pair pair, **val;
    struct worker_pool *pool;

    pair.id = pool_id;
    pthread_rwlock_rdlock(&pool_id_lock);
    val = (struct pool_id_pair **)tfind(&pair, &pool_id_root, pool_id_compare);
    if (val == NULL) {
        pthread_rwlock_unlock(&pool_id_lock);
        return NULL;
    }
    pool = (*val)->pool;
    pthread_rwlock_unlock(&pool_id_lock);
    return pool;
}

static void
worker_pool_id_remove(int pool_id)
{
    struct pool_id_pair pair, **val;

    pair.id = pool_id;
    pthread_rwlock_wrlock(&pool_id_lock);
    val = (struct pool_id_pair **)tfind(&pair, &pool_id_root, pool_id_compare);
    if (val == NULL) {
        pthread_rwlock_unlock(&pool_id_lock);
        return;
    }
    tdelete(&pair, &pool_id_root, pool_id_compare);
    pthread_rwlock_unlock(&pool_id_lock);
    free_common(*val); /* free pair's memory */
    free_unr(pool_id_unr, pool_id);
}

static void
free_wq(struct workqueue_struct *wq)
{
    if (wq) {
        safe_mutex_destroy(&wq->mutex);
        free_workqueue_attrs(wq->unbound_attrs);
        free_common(wq);
    }
}

static struct pool_workqueue *
unbound_pwq_by_node(struct workqueue_struct *wq,
                    int node)
{
    always_assert(wq_pool_is_locked() || safe_mutex_held(&wq->mutex));

    return wq->dfl_pwq;
}

static unsigned int
work_color_to_flags(int color)
{
    return color << WORK_STRUCT_COLOR_SHIFT;
}

static int
get_work_color(struct work_struct *work)
{
    return (*work_data_bits(work) >> WORK_STRUCT_COLOR_SHIFT) &
           ((1 << WORK_STRUCT_COLOR_BITS) - 1);
}

static int
work_next_color(int color)
{
    return (color + 1) % WORK_NR_COLORS;
}

static inline void
set_work_data(struct work_struct *work, unsigned long data,
              unsigned long flags)
{
    WARN_ON_ONCE(!work_pending(work));
    atomic_long_set(&work->data, data | flags | work_static(work));
}

static void
set_work_pwq(struct work_struct *work, struct pool_workqueue *pwq,
             unsigned long extra_flags)
{
    set_work_data(work, (unsigned long)pwq,
                  WORK_STRUCT_PENDING | WORK_STRUCT_PWQ | extra_flags);
}

static void
set_work_pool_and_keep_pending(struct work_struct *work,
                               int pool_id)
{
    set_work_data(work, (unsigned long)pool_id << WORK_OFFQ_POOL_SHIFT,
                  WORK_STRUCT_PENDING);
}

static void
set_work_pool_and_clear_pending(struct work_struct *work,
                                int pool_id)
{
    /*
     * The following wmb is paired with the implied mb in
     * test_and_set_bit(PENDING) and ensures all updates to @work made
     * here are visible to and precede any updates by the next PENDING
     * owner.
     */
    smp_wmb();
    set_work_data(work, (unsigned long)pool_id << WORK_OFFQ_POOL_SHIFT, 0);
    /*
     * The following mb guarantees that previous clear of a PENDING bit
     * will not be reordered with any speculative LOADS or STORES from
     * work->current_func, which is executed afterwards.  This possible
     * reordering can lead to a missed execution on attempt to queue
     * the same @work.  E.g. consider this case:
     *
     *   CPU#0                         CPU#1
     *   ----------------------------  --------------------------------
     *
     * 1  STORE event_indicated
     * 2  queue_work_on() {
     * 3    test_and_set_bit(PENDING)
     * 4 }                             set_..._and_clear_pending() {
     * 5                                 set_work_data() # clear bit
     * 6                                 smp_mb()
     * 7                               work->current_func() {
     * 8				      LOAD event_indicated
     *				   }
     *
     * Without an explicit full barrier speculative LOAD on line 8 can
     * be executed before CPU#0 does STORE on line 1.  If that happens,
     * CPU#0 observes the PENDING bit is still set and new execution of
     * a @work is not queued in a hope, that CPU#1 will eventually
     * finish the queued @work.  Meanwhile CPU#1 does not see
     * event_indicated is set, because speculative LOAD was executed
     * before actual STORE.
     */
    smp_mb();
}

static void
clear_work_data(struct work_struct *work)
{
    smp_wmb();	/* see set_work_pool_and_clear_pending() */
    set_work_data(work, WORK_STRUCT_NO_POOL, 0);
}

static struct pool_workqueue *
get_work_pwq(struct work_struct *work)
{
    unsigned long data = atomic_long_read(&work->data);

    if (data & WORK_STRUCT_PWQ)
        return (struct pool_workqueue *)(data & WORK_STRUCT_WQ_DATA_MASK);
    else
        return NULL;
}

/**
 * get_work_pool - return the worker_pool a given work was associated with
 * @work: the work item of interest
 *
 * Pools are created and destroyed under wq_pool_mutex, and allows read
 * access under RCU read lock.  As such, this function should be
 * called under wq_pool_mutex or inside of a rcu_read_lock() region.
 *
 * All fields of the returned pool are accessible as long as the above
 * mentioned locking is in effect.  If the returned pool needs to be used
 * beyond the critical section, the caller is responsible for ensuring the
 * returned pool is and stays online.
 *
 * Return: The worker_pool @work was last associated with.  %NULL if none.
 */
static struct worker_pool *
get_work_pool(struct work_struct *work)
{
    unsigned long data = atomic_long_read(&work->data);
    int pool_id;

    assert_wq_pool_locked();
    if (data & WORK_STRUCT_PWQ)
        return ((struct pool_workqueue *)
                (data & WORK_STRUCT_WQ_DATA_MASK))->pool;

    pool_id = data >> WORK_OFFQ_POOL_SHIFT;
    if (pool_id == WORK_OFFQ_POOL_NONE)
        return NULL;

    return worker_pool_find(pool_id);
}

/**
 * get_work_pool_id - return the worker pool ID a given work is associated with
 * @work: the work item of interest
 *
 * Return: The worker_pool ID @work was last associated with.
 * %WORK_OFFQ_POOL_NONE if none.
 */
static int
get_work_pool_id(struct work_struct *work)
{
    unsigned long data = atomic_long_read(&work->data);

    if (data & WORK_STRUCT_PWQ)
        return ((struct pool_workqueue *)
                (data & WORK_STRUCT_WQ_DATA_MASK))->pool->id;

    return data >> WORK_OFFQ_POOL_SHIFT;
}

static void
mark_work_canceling(struct work_struct *work)
{
    unsigned long pool_id = get_work_pool_id(work);

    pool_id <<= WORK_OFFQ_POOL_SHIFT;
    set_work_data(work, pool_id | WORK_OFFQ_CANCELING, WORK_STRUCT_PENDING);
}

static bool
work_is_canceling(struct work_struct *work)
{
    unsigned long data = atomic_long_read(&work->data);

    return !(data & WORK_STRUCT_PWQ) && (data & WORK_OFFQ_CANCELING);
}

/*
 * Policy functions.  These define the policies on how the global worker
 * pools are managed.  Unless noted otherwise, these functions assume that
 * they're being called with pool->lock held.
 */

static bool
__need_more_worker(struct worker_pool *pool)
{
    return !atomic_read(&pool->nr_running);
}

/*
 * Need to wake up a worker?  Called from anything but currently
 * running workers.
 *
 * Note that, because unbound workers never contribute to nr_running, this
 * function will always return %true for unbound pools as long as the
 * worklist isn't empty.
 */
static bool
need_more_worker(struct worker_pool *pool)
{
    return !list_empty(&pool->worklist) && __need_more_worker(pool);
}

/* Can I start working?  Called from busy but !running workers. */
static bool
may_start_working(struct worker_pool *pool)
{
    return pool->nr_idle;
}

/* Do I need to keep working?  Called from currently running workers. */
static bool
keep_working(struct worker_pool *pool)
{
    return !list_empty(&pool->worklist) &&
           atomic_read(&pool->nr_running) <= 1;
}

/* Do we need a new worker?  Called from manager. */
static bool
need_to_create_worker(struct worker_pool *pool)
{
    return need_more_worker(pool) && !may_start_working(pool);
}

/* Do we have too many workers and should some go away? */
static bool
too_many_workers(struct worker_pool *pool)
{
    bool managing = !!(pool->flags & POOL_MANAGER_ACTIVE);
    int nr_idle = pool->nr_idle + managing; /* manager is considered idle */
    int nr_busy = pool->nr_workers - nr_idle;

    return nr_idle > 2 && (nr_idle - 2) * MAX_IDLE_WORKERS_RATIO >= nr_busy;
}

/*
 * Wake up functions.
 */

/* Return the first idle worker.  Safe with preemption disabled */
static struct worker *
first_idle_worker(struct worker_pool *pool)
{
    if (unlikely(list_empty(&pool->idle_list)))
        return NULL;

    return list_first_entry(&pool->idle_list, struct worker, entry);
}

/**
 * wake_up_worker - wake up an idle worker
 * @pool: worker pool to wake worker from
 *
 * Wake up the first idle worker of @pool.
 *
 * CONTEXT:
 * raw_spin_lock_irq(pool->lock).
 */
static void
wake_up_worker(struct worker_pool *pool)
{
    struct worker *worker = first_idle_worker(pool);

    if (likely(worker))
        wake_up_process(worker->task);
}

static inline void
worker_set_flags(struct worker *worker, unsigned int flags)
{
    struct worker_pool *pool = worker->pool;

    WARN_ON_ONCE(worker->task != current_task);

    /* If transitioning into NOT_RUNNING, adjust nr_running. */
    if ((flags & WORKER_NOT_RUNNING) &&
        !(worker->flags & WORKER_NOT_RUNNING)) {
        atomic_dec(&pool->nr_running);
    }

    worker->flags |= flags;
}

static inline void
worker_clr_flags(struct worker *worker, unsigned int flags)
{
    struct worker_pool *pool = worker->pool;
    unsigned int oflags = worker->flags;

    WARN_ON_ONCE(worker->task != current_task);

    worker->flags &= ~flags;

    /*
     * If transitioning out of NOT_RUNNING, increment nr_running.  Note
     * that the nested NOT_RUNNING is not a noop.  NOT_RUNNING is mask
     * of multiple flags, not a single flag.
     */
    if ((flags & WORKER_NOT_RUNNING) && (oflags & WORKER_NOT_RUNNING))
        if (!(worker->flags & WORKER_NOT_RUNNING))
            atomic_inc(&pool->nr_running);
}

static struct worker *
find_worker_executing_work(struct worker_pool *pool,
                           struct work_struct *work)
{
    struct worker *worker;

    hash_for_each_possible(pool->busy_hash, worker, hentry,
                           (unsigned long)work)
    if (worker->current_work == work &&
        worker->current_func == work->func)
        return worker;

    return NULL;
}

static void
move_linked_works(struct work_struct *work, struct list_head *head,
                  struct work_struct **nextp)
{
    struct work_struct *n;

    /*
     * Linked worklist will always end before the end of the list,
     * use NULL for list head.
     */
    list_for_each_entry_safe_from(work, n, NULL, entry) {
        list_move_tail(&work->entry, head);
        if (!(*work_data_bits(work) & WORK_STRUCT_LINKED))
            break;
    }

    /*
     * If we're already inside safe list traversal and have moved
     * multiple works to the scheduled queue, the next position
     * needs to be updated.
     */
    if (nextp)
        *nextp = n;
}

static void
get_pwq(struct pool_workqueue *pwq)
{
    WARN_ON_ONCE(pwq->refcnt <= 0);
    pwq->refcnt++;
}

static void
put_pwq(struct pool_workqueue *pwq)
{
    safe_mutex_assert_held(&pwq->pool->lock);

    if (likely(--pwq->refcnt))
        return;
    if (WARN_ON_ONCE(!(pwq->wq->flags & WQ_UNBOUND)))
        return;
    /*
     * @pwq can't be released under pool->lock, bounce to
     * pwq_unbound_release().
     */
    safe_mutex_lock(&pwq_release_mutex);
    list_add_tail(&pwq->unbound_release_node, &pwq_release_list);
    pthread_cond_broadcast(&pwq_release_cond);
    safe_mutex_unlock(&pwq_release_mutex);
}

/**
 * put_pwq_unlocked - put_pwq() with surrounding pool lock/unlock
 * @pwq: pool_workqueue to put (can be %NULL)
 *
 * put_pwq() with locking.  This function also allows %NULL @pwq.
 */
static void
put_pwq_unlocked(struct pool_workqueue *pwq)
{
    if (pwq) {
        /*
         * As both pwqs and pools are RCU protected, the
         * following lock operations are safe.
         */
        safe_mutex_lock(&pwq->pool->lock);
        put_pwq(pwq);
        safe_mutex_unlock(&pwq->pool->lock);
    }
}

static void
pwq_activate_delayed_work(struct work_struct *work)
{
    struct pool_workqueue *pwq = get_work_pwq(work);

    if (list_empty(&pwq->pool->worklist))
        pwq->pool->watchdog_ts = jiffies;
    move_linked_works(work, &pwq->pool->worklist, NULL);
    clear_bit(WORK_STRUCT_DELAYED_BIT, work_data_bits(work));
    pwq->nr_active++;
}

static void
pwq_activate_first_delayed(struct pool_workqueue *pwq)
{
    struct work_struct *work = list_first_entry(&pwq->delayed_works,
                               struct work_struct, entry);

    pwq_activate_delayed_work(work);
}

static void
pwq_dec_nr_in_flight(struct pool_workqueue *pwq, int color)
{
    /* uncolored work items don't participate in flushing or nr_active */
    if (color == WORK_NO_COLOR)
        goto out_put;

    pwq->nr_in_flight[color]--;

    pwq->nr_active--;
    if (!list_empty(&pwq->delayed_works)) {
        /* one down, submit a delayed one */
        if (pwq->nr_active < pwq->max_active)
            pwq_activate_first_delayed(pwq);
    }

    /* is flush in progress and are we at the flushing tip? */
    if (likely(pwq->flush_color != color))
        goto out_put;

    /* are there still in-flight works? */
    if (pwq->nr_in_flight[color])
        goto out_put;

    /* this pwq is done, clear flush_color */
    pwq->flush_color = -1;

    /*
     * If this was the last pwq, wake up the first flusher.  It
     * will handle the rest.
     */
    if (atomic_dec_and_test(&pwq->wq->nr_pwqs_to_flush))
        util_complete(&pwq->wq->first_flusher->done);
out_put:
    put_pwq(pwq);
}

static int
try_to_grab_pending(struct work_struct *work, bool is_dwork)
{
    struct worker_pool *pool;
    struct pool_workqueue *pwq;

    /* try to steal the timer if it exists */
    if (is_dwork) {
        struct delayed_work *dwork = to_delayed_work(work);

        /*
         * dwork->timer is irqsafe.  If del_timer() fails, it's
         * guaranteed that the timer is not queued anywhere and not
         * running on the local CPU.
         */
        if (likely(util_del_timer(&dwork->timer)))
            return 1;
    }

    /* try to claim PENDING the normal way */
    if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work)))
        return 0;

    wq_pool_rdlock();

    /*
     * The queueing is in progress, or it is already queued. Try to
     * steal it from ->worklist without clearing WORK_STRUCT_PENDING.
     */
    pool = get_work_pool(work);
    if (!pool)
        goto fail;

    safe_mutex_lock(&pool->lock);
    /*
     * work->data is guaranteed to point to pwq only while the work
     * item is queued on pwq->wq, and both updating work->data to point
     * to pwq on queueing and to pool on dequeueing are done under
     * pwq->pool->lock.  This in turn guarantees that, if work->data
     * points to pwq which is associated with a locked pool, the work
     * item is currently queued on that pool.
     */
    pwq = get_work_pwq(work);
    if (pwq && pwq->pool == pool) {

        /*
         * A delayed work item cannot be grabbed directly because
         * it might have linked NO_COLOR work items which, if left
         * on the delayed_list, will confuse pwq->nr_active
         * management later on and cause stall.  Make sure the work
         * item is activated before grabbing.
         */
        if (*work_data_bits(work) & WORK_STRUCT_DELAYED)
            pwq_activate_delayed_work(work);

        list_del_init(&work->entry);
        pwq_dec_nr_in_flight(pwq, get_work_color(work));

        /* work->data points to pwq iff queued, point to pool */
        set_work_pool_and_keep_pending(work, pool->id);

        safe_mutex_unlock(&pool->lock);
        wq_pool_unlock();
        return 1;
    }
    safe_mutex_unlock(&pool->lock);
fail:
    wq_pool_unlock();
    if (work_is_canceling(work))
        return -ENOENT;
    cpu_relax();
    return -EAGAIN;
}

static void
insert_work(struct pool_workqueue *pwq, struct work_struct *work,
            struct list_head *head, unsigned int extra_flags)
{
    struct worker_pool *pool = pwq->pool;

    /* we own @work, set data and link */
    set_work_pwq(work, pwq, extra_flags);
    list_add_tail(&work->entry, head);
    get_pwq(pwq);

    /*
     * Ensure either wq_worker_sleeping() sees the above
     * list_add_tail() or we see zero nr_running to avoid workers lying
     * around lazily while there are works to be processed.
     */
    smp_mb();

    if (__need_more_worker(pool))
        wake_up_worker(pool);
}

static bool
is_chained_work(struct workqueue_struct *wq)
{
    struct worker *worker;

    worker = current_wq_worker();
    /*
     * Return %true iff I'm a worker executing a work item on @wq.  If
     * I'm @worker, it's safe to dereference it without locking.
     */
    return worker && worker->current_pwq->wq == wq;
}

static void
__queue_work(struct workqueue_struct *wq,
             struct work_struct *work)
{
    struct pool_workqueue *pwq;
    struct worker_pool *last_pool;
    struct list_head *worklist;
    unsigned int work_flags;

    /*
     * While a work item is PENDING && off queue, a task trying to
     * steal the PENDING will busy-loop waiting for it to either get
     * queued or lose PENDING.
     */

    /* if draining, only works from the same workqueue are allowed */
    if (unlikely(wq->flags & __WQ_DRAINING) &&
        WARN_ON_ONCE(!is_chained_work(wq)))
        return;

retry:
    wq_pool_rdlock();
    safe_mutex_lock(&wq->mutex);

    /* pwq which will be used unless @work is executing elsewhere */
    pwq = unbound_pwq_by_node(wq, 0);

    /*
     * If @work was previously on a different pool, it might still be
     * running there, in which case the work needs to be queued on that
     * pool to guarantee non-reentrancy.
     */
    last_pool = get_work_pool(work);
    if (last_pool && last_pool != pwq->pool) {
        struct worker *worker;

        safe_mutex_lock(&last_pool->lock);

        worker = find_worker_executing_work(last_pool, work);

        if (worker && worker->current_pwq->wq == wq) {
            pwq = worker->current_pwq;
        } else {
            /* meh... not running there, queue here */
            safe_mutex_unlock(&last_pool->lock);
            safe_mutex_lock(&pwq->pool->lock);
        }
    } else {
        safe_mutex_lock(&pwq->pool->lock);
    }

    /*
     * pwq is determined and locked.  For unbound pools, we could have
     * raced with pwq release and it could already be dead.  If its
     * refcnt is zero, repeat pwq selection.  Note that pwqs never die
     * without another pwq replacing it in the numa_pwq_tbl or while
     * work items are executing on it, so the retrying is guaranteed to
     * make forward-progress.
     */
    if (unlikely(!pwq->refcnt)) {
        if (wq->flags & WQ_UNBOUND) {
            safe_mutex_unlock(&pwq->pool->lock);
            safe_mutex_unlock(&wq->mutex);
            wq_pool_unlock();

            cpu_relax();
            goto retry;
        }
        /* oops */
        WARN_ONCE(true, "workqueue: pwq for %s has 0 refcnt",
                  wq->name);
    }

    /* pwq determined, queue */

    if (WARN_ON(!list_empty(&work->entry)))
        goto out;

    pwq->nr_in_flight[pwq->work_color]++;
    work_flags = work_color_to_flags(pwq->work_color);

    if (likely(pwq->nr_active < pwq->max_active)) {
        pwq->nr_active++;
        worklist = &pwq->pool->worklist;
        if (list_empty(worklist))
            pwq->pool->watchdog_ts = jiffies;
    } else {
        work_flags |= WORK_STRUCT_DELAYED;
        worklist = &pwq->delayed_works;
    }

    insert_work(pwq, work, worklist, work_flags);

out:
    safe_mutex_unlock(&pwq->pool->lock);
    safe_mutex_unlock(&wq->mutex);
    wq_pool_unlock();
}

bool
util_queue_work(struct workqueue_struct *wq,
               struct work_struct *work)
{
    bool ret = false;

    if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
        __queue_work(wq, work);
        ret = true;
    }

    return ret;
}

void
util_delayed_work_timer_fn(struct timer_list *t)
{
    struct delayed_work *dwork = from_timer(dwork, t, timer);

    __queue_work(dwork->wq, &dwork->work);
}

static void
__queue_delayed_work(struct workqueue_struct *wq,
                     struct delayed_work *dwork, unsigned long delay)
{
    struct timer_list *timer = &dwork->timer;
    struct work_struct *work = &dwork->work;

    WARN_ON_ONCE(!wq);
    WARN_ON_ONCE(timer->function != util_delayed_work_timer_fn);
    WARN_ON_ONCE(timer_pending(timer));
    WARN_ON_ONCE(!list_empty(&work->entry));

    /*
     * If @delay is 0, queue @dwork->work immediately.  This is for
     * both optimization and correctness.  The earliest @timer can
     * expire is on the closest next tick and delayed_work users depend
     * on that there's no such delay when @delay is 0.
     */
    if (!delay) {
        __queue_work(wq, &dwork->work);
        return;
    }

    dwork->wq = wq;
    timer->expires = jiffies + delay;

    util_add_timer(timer);
}

bool
util_queue_delayed_work(struct workqueue_struct *wq,
                       struct delayed_work *dwork, unsigned long delay)
{
    struct work_struct *work = &dwork->work;
    bool ret = false;

    if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
        __queue_delayed_work(wq, dwork, delay);
        ret = true;
    }

    return ret;
}

static void
worker_enter_idle(struct worker *worker)
{
    struct worker_pool *pool = worker->pool;

    if (WARN_ON_ONCE(worker->flags & WORKER_IDLE) ||
        WARN_ON_ONCE(!list_empty(&worker->entry) &&
                     (worker->hentry.next || worker->hentry.pprev)))
        return;

    /* can't use worker_set_flags(), also called from create_worker() */
    worker->flags |= WORKER_IDLE;
    pool->nr_idle++;
    worker->last_active = jiffies;

    /* idle_list is LIFO */
    list_add(&worker->entry, &pool->idle_list);

    if (too_many_workers(pool) && !timer_pending(&pool->idle_timer))
        util_mod_timer(&pool->idle_timer, jiffies + IDLE_WORKER_TIMEOUT);
}

static void
worker_leave_idle(struct worker *worker)
{
    struct worker_pool *pool = worker->pool;

    if (WARN_ON_ONCE(!(worker->flags & WORKER_IDLE)))
        return;
    worker_clr_flags(worker, WORKER_IDLE);
    pool->nr_idle--;
    list_del_init(&worker->entry);
}

static struct worker *
alloc_worker(int node)
{
    struct worker *worker;

    worker = (struct worker *)calloc_common(1, sizeof(*worker));
    if (worker) {
        INIT_LIST_HEAD(&worker->entry);
        INIT_LIST_HEAD(&worker->scheduled);
        INIT_LIST_HEAD(&worker->node);
        /* on creation a worker is in !idle && prep state */
        worker->flags = WORKER_PREP;
    }
    return worker;
}

static void
worker_attach_to_pool(struct worker *worker,
                      struct worker_pool *pool)
{
    safe_mutex_lock(&wq_pool_attach_mutex);

    list_add_tail(&worker->node, &pool->workers);
    worker->pool = pool;

    safe_mutex_unlock(&wq_pool_attach_mutex);
}

static void
worker_detach_from_pool(struct worker *worker)
{
    struct worker_pool *pool = worker->pool;
    struct completion *detach_completion = NULL;

    safe_mutex_lock(&wq_pool_attach_mutex);

    list_del(&worker->node);
    worker->pool = NULL;

    if (list_empty(&pool->workers))
        detach_completion = pool->detach_completion;
    safe_mutex_unlock(&wq_pool_attach_mutex);

    if (detach_completion)
        util_complete(detach_completion);
}

static struct worker *
create_worker(struct worker_pool *pool)
{
    struct worker *worker = NULL;
    int id = -1;
    char id_buf[128];

    /* ID is needed to determine kthread name */
    id = alloc_unr(&pool->worker_ida);
    if (id < 0)
        goto fail;

    worker = alloc_worker(0);
    if (!worker)
        goto fail;

    worker->id = id;

    snprintf(id_buf, sizeof(id_buf), "%d:%d", pool->id, id);

    worker->task = util_kthread_create(worker_thread, worker, "workqueue worker/%s", id_buf);
    if (worker->task == NULL)
        goto fail;

    /* successful, attach the worker to the pool */
    worker_attach_to_pool(worker, pool);

    /* start the newly created worker */
    safe_mutex_lock(&pool->lock);
    worker->pool->nr_workers++;
    worker_enter_idle(worker);
    wake_up_process(worker->task);
    safe_mutex_unlock(&pool->lock);

    return worker;

fail:
    if (id >= 0)
        free_unr(&pool->worker_ida, id);
    free_common(worker);
    return NULL;
}

static void
destroy_worker(struct worker *worker)
{
    struct worker_pool *pool = worker->pool;

    safe_mutex_assert_held(&pool->lock);

    /* sanity check frenzy */
    if (WARN_ON(worker->current_work) ||
        WARN_ON(!list_empty(&worker->scheduled)) ||
        WARN_ON(!(worker->flags & WORKER_IDLE)))
        return;

    pool->nr_workers--;
    pool->nr_idle--;

    list_del_init(&worker->entry);
    worker->flags |= WORKER_DIE;
    wake_up_process(worker->task);
}

static void
idle_worker_timeout(struct timer_list *t)
{
    struct worker_pool *pool = from_timer(pool, t, idle_timer);

    safe_mutex_lock(&pool->lock);

    while (too_many_workers(pool)) {
        struct worker *worker;
        unsigned long expires;

        /* idle_list is kept in LIFO order, check the last one */
        worker = list_entry(pool->idle_list.prev, struct worker, entry);
        expires = worker->last_active + IDLE_WORKER_TIMEOUT;

        if (time_before(jiffies, expires)) {
            util_mod_timer(&pool->idle_timer, expires);
            break;
        }

        destroy_worker(worker);
    }

    safe_mutex_unlock(&pool->lock);
}

static void
maybe_create_worker(struct worker_pool *pool)
{
restart:
    safe_mutex_unlock(&pool->lock);

    while (true) {
        if (create_worker(pool) || !need_to_create_worker(pool))
            break;

        fprintf(stderr, "can not create worker, sleep for 1 second\n");
        sleep(1);

        if (!need_to_create_worker(pool))
            break;
    }

    safe_mutex_lock(&pool->lock);
    /*
     * This is necessary even after a new worker was just successfully
     * created as @pool->lock was dropped and the new worker might have
     * already become busy.
     */
    if (need_to_create_worker(pool))
        goto restart;
}

static bool
manage_workers(struct worker *worker)
{
    struct worker_pool *pool = worker->pool;

    if (pool->flags & POOL_MANAGER_ACTIVE)
        return false;

    pool->flags |= POOL_MANAGER_ACTIVE;
    pool->manager = worker;

    maybe_create_worker(pool);

    pool->manager = NULL;
    pool->flags &= ~POOL_MANAGER_ACTIVE;

    pthread_cond_broadcast(&pool->manage_cond);

    return true;
}

static void
process_one_work(struct worker *worker, struct work_struct *work)
{
    struct pool_workqueue *pwq = get_work_pwq(work);
    struct worker_pool *pool = worker->pool;
    int work_color;
    struct worker *collision;

    /*
     * A single work shouldn't be executed concurrently by
     * multiple workers on a single cpu.  Check whether anyone is
     * already processing the work.  If so, defer the work to the
     * currently executing one.
     */
    collision = find_worker_executing_work(pool, work);
    if (unlikely(collision)) {
        move_linked_works(work, &collision->scheduled, NULL);
        return;
    }

    /* claim and dequeue */
    hash_add(pool->busy_hash, &worker->hentry, (unsigned long)work);
    worker->current_work = work;
    worker->current_func = work->func;
    worker->current_pwq = pwq;
    work_color = get_work_color(work);

    /*
     * Record wq name for cmdline and debug reporting, may get
     * overridden through set_worker_desc().
     */
    strncpy(worker->desc, pwq->wq->name, WORKER_DESC_LEN);
    worker->desc[WORKER_DESC_LEN-1] = '\0';

    list_del_init(&work->entry);

    atomic_dec(&pool->nr_running);
    /*
     * Wake up another worker if necessary.  The condition is always
     * false for normal per-cpu workers since nr_running would always
     * be >= 1 at this point.  This is used to chain execution of the
     * pending work items for WORKER_NOT_RUNNING workers such as the
     * UNBOUND and CPU_INTENSIVE ones.
     */
    if (need_more_worker(pool))
        wake_up_worker(pool);

    /*
     * Record the last pool and clear PENDING which should be the last
     * update to @work.  Also, do this inside @pool->lock so that
     * PENDING and queued state changes happen together while IRQ is
     * disabled.
     */
    set_work_pool_and_clear_pending(work, pool->id);

    safe_mutex_unlock(&pool->lock);

    worker->current_func(work);

    /* we must be careful to not use "work" after this */

    safe_mutex_lock(&pool->lock);

    atomic_inc(&pool->nr_running);

    /* we're done with it, release */
    hash_del(&worker->hentry);
    worker->current_work = NULL;
    worker->current_func = NULL;
    worker->current_pwq = NULL;
    pwq_dec_nr_in_flight(pwq, work_color);
}

static void
process_scheduled_works(struct worker *worker)
{
    while (!list_empty(&worker->scheduled)) {
        struct work_struct *work = list_first_entry(&worker->scheduled,
                                   struct work_struct, entry);
        process_one_work(worker, work);
    }
}

static void
set_pf_worker(bool val)
{
    safe_mutex_lock(&wq_pool_attach_mutex);
    if (val)
        current_task->flags |= PF_WQ_WORKER;
    else
        current_task->flags &= ~PF_WQ_WORKER;
    safe_mutex_unlock(&wq_pool_attach_mutex);
}

static int
worker_thread(void *__worker)
{
    struct worker *worker = (struct worker *)__worker;
    struct worker_pool *pool = worker->pool;

    /* tell the scheduler that this is a workqueue worker */
    set_pf_worker(true);
woke_up:
    safe_mutex_lock(&pool->lock);

    /* am I supposed to die? */
    if (unlikely(worker->flags & WORKER_DIE)) {
        safe_mutex_unlock(&pool->lock);
        WARN_ON_ONCE(!list_empty(&worker->entry));
        set_pf_worker(false);

        pthread_setname_np(pthread_self(), "util_worker/dying");
        free_unr(&pool->worker_ida, worker->id);
        worker_detach_from_pool(worker);
        free_common(worker);
        pr_debug("%s: thread %ld exit", __func__, pthread_self());
        return 0;
    }

    worker_leave_idle(worker);
recheck:
    /* no more worker necessary? */
    if (!need_more_worker(pool))
        goto sleep;

    /* do we need to manage? */
    if (unlikely(!may_start_working(pool)) && manage_workers(worker))
        goto recheck;

    /*
     * ->scheduled list can only be filled while a worker is
     * preparing to process a work or actually processing it.
     * Make sure nobody diddled with it while I was sleeping.
     */
    WARN_ON_ONCE(!list_empty(&worker->scheduled));

    /*
     * Finish PREP stage.  We're guaranteed to have at least one idle
     * worker or that someone else has already assumed the manager
     * role.  This is where @worker starts participating in concurrency
     * management if applicable and concurrency management is restored
     * after being rebound.  See rebind_workers() for details.
     */
    worker_clr_flags(worker, WORKER_PREP);

    do {
        struct work_struct *work =
            list_first_entry(&pool->worklist,
                             struct work_struct, entry);

        pool->watchdog_ts = jiffies;

        if (likely(!(*work_data_bits(work) & WORK_STRUCT_LINKED))) {
            /* optimization path, not strictly necessary */
            process_one_work(worker, work);
            if (unlikely(!list_empty(&worker->scheduled)))
                process_scheduled_works(worker);
        } else {
            move_linked_works(work, &worker->scheduled, NULL);
            process_scheduled_works(worker);
        }
    } while (keep_working(pool));

    worker_set_flags(worker, WORKER_PREP);
sleep:
    /*
     * pool->lock is held and there's no work to process and no need to
     * manage, sleep.  Workers are woken up only while holding
     * pool->lock or from local cpu, so setting the current state
     * before releasing pool->lock is enough to prevent losing any
     * event.
     */
    worker_enter_idle(worker);
    set_current_state(TASK_IDLE);
    safe_mutex_unlock(&pool->lock);
    task_schedule();
    goto woke_up;
}

struct wq_barrier {
    struct work_struct	work;
    struct completion	done;
};

static void
wq_barrier_func(struct work_struct *work)
{
    struct wq_barrier *barr = container_of(work, struct wq_barrier, work);
    util_complete(&barr->done);
}

static void
insert_wq_barrier(struct pool_workqueue *pwq,
                  struct wq_barrier *barr,
                  struct work_struct *target, struct worker *worker)
{
    struct list_head *head;
    unsigned int linked = 0;

    /*
     * debugobject calls are safe here even with pool->lock locked
     * as we know for sure that this will not trigger any of the
     * checks and call back into the fixup functions where we
     * might deadlock.
     */
    INIT_WORK_ONSTACK(&barr->work, wq_barrier_func);
    set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(&barr->work));

    __util_init_completion(&barr->done);

    /*
     * If @target is currently being executed, schedule the
     * barrier to the worker; otherwise, put it after @target.
     */
    if (worker)
        head = worker->scheduled.next;
    else {
        unsigned long *bits = work_data_bits(target);

        head = target->entry.next;
        /* there can already be other linked works, inherit and set */
        linked = *bits & WORK_STRUCT_LINKED;
        set_bit(WORK_STRUCT_LINKED_BIT, bits);
    }

    insert_work(pwq, &barr->work, head,
                work_color_to_flags(WORK_NO_COLOR) | linked);
}


static bool
flush_workqueue_prep_pwqs(struct workqueue_struct *wq,
                          int flush_color, int work_color)
{
    bool wait = false;
    struct pool_workqueue *pwq;

    if (flush_color >= 0) {
        WARN_ON_ONCE(atomic_read(&wq->nr_pwqs_to_flush));
        atomic_set(&wq->nr_pwqs_to_flush, 1);
    }

    for_each_pwq(pwq, wq) {
        struct worker_pool *pool = pwq->pool;

        safe_mutex_lock(&pool->lock);

        if (flush_color >= 0) {
            WARN_ON_ONCE(pwq->flush_color != -1);

            if (pwq->nr_in_flight[flush_color]) {
                pwq->flush_color = flush_color;
                atomic_inc(&wq->nr_pwqs_to_flush);
                wait = true;
            }
        }

        if (work_color >= 0) {
            WARN_ON_ONCE(work_color != work_next_color(pwq->work_color));
            pwq->work_color = work_color;
        }

        safe_mutex_unlock(&pool->lock);
    }

    if (flush_color >= 0 && atomic_dec_and_test(&wq->nr_pwqs_to_flush))
        util_complete(&wq->first_flusher->done);

    return wait;
}

void
util_flush_workqueue(struct workqueue_struct *wq)
{
    struct wq_flusher this_flusher = {
        .list = LLIST_HEAD_VALUE(this_flusher.list),
        .flush_color = -1,
        .done = COMPLETION_INITIALIZER(this_flusher.done)
    };
    int next_color;

    if (WARN_ON(!wq_online))
        return;

    safe_mutex_lock(&wq->mutex);

    /*
     * Start-to-wait phase
     */
    next_color = work_next_color(wq->work_color);

    if (next_color != wq->flush_color) {
        /*
         * Color space is not full.  The current work_color
         * becomes our flush_color and work_color is advanced
         * by one.
         */
        WARN_ON_ONCE(!list_empty(&wq->flusher_overflow));
        this_flusher.flush_color = wq->work_color;
        wq->work_color = next_color;

        if (!wq->first_flusher) {
            /* no flush in progress, become the first flusher */
            WARN_ON_ONCE(wq->flush_color != this_flusher.flush_color);

            wq->first_flusher = &this_flusher;

            if (!flush_workqueue_prep_pwqs(wq, wq->flush_color,
                                           wq->work_color)) {
                /* nothing to flush, done */
                wq->flush_color = next_color;
                wq->first_flusher = NULL;
                goto out_unlock;
            }
        } else {
            /* wait in queue */
            WARN_ON_ONCE(wq->flush_color == this_flusher.flush_color);
            list_add_tail(&this_flusher.list, &wq->flusher_queue);
            flush_workqueue_prep_pwqs(wq, -1, wq->work_color);
        }
    } else {
        /*
         * Oops, color space is full, wait on overflow queue.
         * The next flush completion will assign us
         * flush_color and transfer to flusher_queue.
         */
        list_add_tail(&this_flusher.list, &wq->flusher_overflow);
    }

    safe_mutex_unlock(&wq->mutex);

    util_wait_for_completion(&this_flusher.done);

    /*
     * Wake-up-and-cascade phase
     *
     * First flushers are responsible for cascading flushes and
     * handling overflow.  Non-first flushers can simply return.
     */
    if (READ_ONCE(wq->first_flusher) != &this_flusher)
        return;

    safe_mutex_lock(&wq->mutex);

    /* we might have raced, check again with mutex held */
    if (wq->first_flusher != &this_flusher)
        goto out_unlock;

    WRITE_ONCE(wq->first_flusher, NULL);

    WARN_ON_ONCE(!list_empty(&this_flusher.list));
    WARN_ON_ONCE(wq->flush_color != this_flusher.flush_color);

    while (true) {
        struct wq_flusher *next, *tmp;

        /* complete all the flushers sharing the current flush color */
        list_for_each_entry_safe(next, tmp, &wq->flusher_queue, list) {
            if (next->flush_color != wq->flush_color)
                break;
            list_del_init(&next->list);
            util_complete(&next->done);
        }

        WARN_ON_ONCE(!list_empty(&wq->flusher_overflow) &&
                     wq->flush_color != work_next_color(wq->work_color));

        /* this flush_color is finished, advance by one */
        wq->flush_color = work_next_color(wq->flush_color);

        /* one color has been freed, handle overflow queue */
        if (!list_empty(&wq->flusher_overflow)) {
            /*
             * Assign the same color to all overflowed
             * flushers, advance work_color and append to
             * flusher_queue.  This is the start-to-wait
             * phase for these overflowed flushers.
             */
            list_for_each_entry(tmp, &wq->flusher_overflow, list)
                tmp->flush_color = wq->work_color;

            wq->work_color = work_next_color(wq->work_color);

            list_splice_tail_init(&wq->flusher_overflow,
                                  &wq->flusher_queue);
            flush_workqueue_prep_pwqs(wq, -1, wq->work_color);
        }

        if (list_empty(&wq->flusher_queue)) {
            WARN_ON_ONCE(wq->flush_color != wq->work_color);
            break;
        }

        /*
         * Need to flush more colors.  Make the next flusher
         * the new first flusher and arm pwqs.
         */
        WARN_ON_ONCE(wq->flush_color == wq->work_color);
        WARN_ON_ONCE(wq->flush_color != next->flush_color);

        list_del_init(&next->list);
        wq->first_flusher = next;

        if (flush_workqueue_prep_pwqs(wq, wq->flush_color, -1))
            break;

        /*
         * Meh... this color is already done, clear first
         * flusher and repeat cascading.
         */
        wq->first_flusher = NULL;
    }

out_unlock:
    safe_mutex_unlock(&wq->mutex);
}

void
util_drain_workqueue(struct workqueue_struct *wq)
{
    unsigned int flush_cnt = 0;
    struct pool_workqueue *pwq;

    /*
     * __queue_work() needs to test whether there are drainers, is much
     * hotter than drain_workqueue() and already looks at @wq->flags.
     * Use __WQ_DRAINING so that queue doesn't have to check nr_drainers.
     */
    safe_mutex_lock(&wq->mutex);
    if (!wq->nr_drainers++)
        wq->flags |= __WQ_DRAINING;
    safe_mutex_unlock(&wq->mutex);
reflush:
    util_flush_workqueue(wq);

    safe_mutex_lock(&wq->mutex);

    for_each_pwq(pwq, wq) {
        bool drained;

        safe_mutex_lock(&pwq->pool->lock);
        drained = !pwq->nr_active && list_empty(&pwq->delayed_works);
        safe_mutex_unlock(&pwq->pool->lock);

        if (drained)
            continue;

        if (++flush_cnt == 10 ||
            (flush_cnt % 100 == 0 && flush_cnt <= 1000))
            pr_warn("workqueue %s: %s() isn't complete after %u tries\n",
                    wq->name, __func__, flush_cnt);

        safe_mutex_unlock(&wq->mutex);
        goto reflush;
    }

    if (!--wq->nr_drainers)
        wq->flags &= ~__WQ_DRAINING;
    safe_mutex_unlock(&wq->mutex);
}

static bool
start_flush_work(struct work_struct *work, struct wq_barrier *barr,
                 bool from_cancel)
{
    struct worker *worker = NULL;
    struct worker_pool *pool;
    struct pool_workqueue *pwq;

    wq_pool_rdlock();
    pool = get_work_pool(work);
    if (!pool) {
        wq_pool_unlock();
        return false;
    }

    safe_mutex_lock(&pool->lock);
    /* see the comment in try_to_grab_pending() with the same code */
    pwq = get_work_pwq(work);
    if (pwq) {
        if (unlikely(pwq->pool != pool))
            goto already_gone;
    } else {
        worker = find_worker_executing_work(pool, work);
        if (!worker)
            goto already_gone;
        pwq = worker->current_pwq;
    }

    insert_wq_barrier(pwq, barr, work, worker);
    safe_mutex_unlock(&pool->lock);
    wq_pool_unlock();

    return true;
already_gone:
    safe_mutex_unlock(&pool->lock);
    wq_pool_unlock();
    return false;
}

static bool
__flush_work(struct work_struct *work, bool from_cancel)
{
    struct wq_barrier barr;

    if (WARN_ON(!wq_online))
        return false;

    if (WARN_ON(!work->func))
        return false;

    if (start_flush_work(work, &barr, from_cancel)) {
        util_wait_for_completion(&barr.done);
        util_destroy_work_on_stack(&barr.work);
        return true;
    } else {
        return false;
    }
}

/*
 * return true if flush_work waited for the work to finish execution,
 * false if it was already idle.
 */
bool
util_flush_work(struct work_struct *work)
{
    return __flush_work(work, false);
}

static bool
__cancel_work_timer(struct work_struct *work, bool is_dwork)
{
    int spin = 0;
    int ret;

    do {
        ret = try_to_grab_pending(work, is_dwork);
        /*
         * If someone else is already canceling, wait for it to
         * finish.
         * this may lead to the thundering herd problem. --- TODO
         */
        if (unlikely(ret == -ENOENT)) {
            safe_mutex_lock(&cancel_mutex);
            if (work_is_canceling(work))
                safe_cond_wait(&cancel_cond, &cancel_mutex);
            safe_mutex_unlock(&cancel_mutex);
        }
        if (spin++ > 200) {
            pthread_yield();
            spin = 0;
        }
    } while (unlikely(ret < 0));

    /* tell other tasks trying to grab @work to back off */
    mark_work_canceling(work);

    /*
     * This allows canceling during early boot.  We know that @work
     * isn't executing.
     */
    if (wq_online)
        __flush_work(work, true);

    clear_work_data(work);

    /*
     * make other's work_is_canceling() visible .
     */
    smp_mb();

    safe_mutex_lock(&cancel_mutex);
    pthread_cond_broadcast(&cancel_cond);
    safe_mutex_unlock(&cancel_mutex);

    return ret;
}

/* return true if canceled pending work, false if not
 * (running not pending at all)
 */
bool
util_cancel_work_sync(struct work_struct *work)
{
    return __cancel_work_timer(work, false);
}

/*
 * return true if flush_work waited for the work to finish execution,
 * false if it was already idle.
 */
bool
util_flush_delayed_work(struct delayed_work *dwork)
{
    if (util_del_timer_sync(&dwork->timer))
        __queue_work(dwork->wq, &dwork->work);
    return util_flush_work(&dwork->work);
}

static bool
__cancel_work(struct work_struct *work, bool is_dwork)
{
    int ret;

    do {
        ret = try_to_grab_pending(work, is_dwork);
    } while (unlikely(ret == -EAGAIN));

    if (unlikely(ret < 0))
        return false;

    set_work_pool_and_clear_pending(work, get_work_pool_id(work));
    return ret;
}

/* The work callback function may still be running on return,
 * unless it returns true and the work doesn't re-arm itself.
 * Explicitly flush or use cancel_delayed_work_sync to wait on it.
 */
bool
util_cancel_delayed_work(struct delayed_work *dwork)
{
    return __cancel_work(&dwork->work, true);
}

bool
util_cancel_delayed_work_sync(struct delayed_work *dwork)
{
    return __cancel_work_timer(&dwork->work, true);
}

static void
free_workqueue_attrs(struct workqueue_attrs *attrs)
{
    if (attrs) {
        free_common(attrs);
    }
}

void
util_free_workqueue_attrs(struct workqueue_attrs *attrs)
{
    free_workqueue_attrs(attrs);
}

static struct workqueue_attrs *
alloc_workqueue_attrs(void)
{
    struct workqueue_attrs *attrs;

    attrs = (struct workqueue_attrs *)calloc_common(1, sizeof(*attrs));
    if (!attrs)
        return NULL;
    return attrs;
}

struct workqueue_attrs *util_alloc_workqueue_attrs(void)
{
    return alloc_workqueue_attrs();
}

static void
copy_workqueue_attrs(struct workqueue_attrs *to,
                     const struct workqueue_attrs *from)
{
    to->cookie = from->cookie;
}

/* hash value of the content of @attr */
static uint32_t
wqattrs_hash(const struct workqueue_attrs *attrs)
{
    return attrs->cookie;
}

/* content equality test */
static bool
wqattrs_equal(const struct workqueue_attrs *a,
              const struct workqueue_attrs *b)
{
    return a->cookie == b->cookie;
}

static int
init_worker_pool(struct worker_pool *pool)
{
    safe_mutex_init(&pool->lock);
    pool->id = -1;
    pool->watchdog_ts = jiffies;
    INIT_LIST_HEAD(&pool->worklist);
    INIT_LIST_HEAD(&pool->idle_list);
    hash_init(pool->busy_hash);

    timer_setup(&pool->idle_timer, idle_worker_timeout, 0);

    INIT_LIST_HEAD(&pool->workers);

    init_unrhdr(&pool->worker_ida, 0, INT_MAX, NULL);
    INIT_HLIST_NODE(&pool->hash_node);
    pool->refcnt = 1;

    pthread_cond_init(&pool->manage_cond, NULL);

    /* shouldn't fail above this point */
    pool->attrs = alloc_workqueue_attrs();
    if (!pool->attrs)
        return -ENOMEM;
    return 0;
}

static void
free_pool(struct worker_pool *pool)
{
    clear_unrhdr(&pool->worker_ida);
    free_workqueue_attrs(pool->attrs);
    safe_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->manage_cond);
    free_common(pool);
}

static void
put_unbound_pool(struct worker_pool *pool)
{
    DECLARE_COMPLETION(detach_completion);
    struct worker *worker;

    assert_wq_pool_wrlocked();

    if (--pool->refcnt)
        return;

    /* sanity checks */
    if (WARN_ON(!list_empty(&pool->worklist)))
        return;

    /* release id and unhash */
    if (pool->id >= 0)
        worker_pool_id_remove(pool->id);
    hash_del(&pool->hash_node);


    /*
     * Become the manager and destroy all workers.  This prevents
     * @pool's workers from blocking on attach_mutex.  We're the last
     * manager and @pool gets freed with the flag set.
     */
again:
    safe_mutex_lock(&pool->lock);

    if (pool->flags & POOL_MANAGER_ACTIVE) {
        safe_cond_wait(&pool->manage_cond, &pool->lock);
        safe_mutex_unlock(&pool->lock);
        goto again;
    }

    pool->flags |= POOL_MANAGER_ACTIVE;

    while ((worker = first_idle_worker(pool)))
        destroy_worker(worker);
    WARN_ON(pool->nr_workers || pool->nr_idle);
    safe_mutex_unlock(&pool->lock);

    safe_mutex_lock(&wq_pool_attach_mutex);
    if (!list_empty(&pool->workers))
        pool->detach_completion = &detach_completion;
    safe_mutex_unlock(&wq_pool_attach_mutex);

    if (pool->detach_completion)
        util_wait_for_completion(pool->detach_completion);

    /* shut down the timers */
    util_del_timer_sync(&pool->idle_timer);

    free_pool(pool);
}

static struct worker_pool *
get_unbound_pool(const struct workqueue_attrs *attrs)
{
    uint32_t hash = wqattrs_hash(attrs);
    struct worker_pool *pool;

    assert_wq_pool_wrlocked();

    /* do we already have a matching pool? */
    hash_for_each_possible(unbound_pool_hash, pool, hash_node, hash) {
        if (wqattrs_equal(pool->attrs, attrs)) {
            pool->refcnt++;
            return pool;
        }
    }

    /* nope, create a new one */
    if (posix_memalign((void **)&pool, CACHELINE_SIZE, sizeof(*pool)))
        goto fail;
    memset(pool, 0, sizeof(*pool));
    if (!pool || init_worker_pool(pool) < 0)
        goto fail;

    copy_workqueue_attrs(pool->attrs, attrs);

    if (worker_pool_assign_id(pool) < 0)
        goto fail;

    /* create and start the initial worker */
    if (wq_online && !create_worker(pool))
        goto fail;

    /* install */
    hash_add(unbound_pool_hash, &pool->hash_node, hash);

    return pool;
fail:
    if (pool)
        put_unbound_pool(pool);
    return NULL;
}

static void
pwq_unbound_release(struct pool_workqueue *pwq)
{
    struct workqueue_struct *wq = pwq->wq;
    struct worker_pool *pool = pwq->pool;
    bool is_last;

    if (WARN_ON_ONCE(!(wq->flags & WQ_UNBOUND)))
        return;

    /* pwq disappears with pool mutex held */
    wq_pool_wrlock();

    safe_mutex_lock(&wq->mutex);
    list_del(&pwq->pwqs_node);
    is_last = list_empty(&wq->pwqs);
    safe_mutex_unlock(&wq->mutex);

    put_unbound_pool(pool);
    wq_pool_unlock();

    free_common(pwq);

    /*
     * If we're the last pwq going away, @wq is already dead and no one
     * is gonna access it anymore.
     */
    if (is_last) {
        free_wq(wq);
    }
}

static void *
pwq_release_thread(void *arg)
{
    struct pool_workqueue *pwq;

    for (;;) {
        safe_mutex_lock(&pwq_release_mutex);
        while (list_empty(&pwq_release_list)) {
            safe_cond_wait(&pwq_release_cond, &pwq_release_mutex);
        }
        pwq = list_first_entry(&pwq_release_list, struct pool_workqueue, unbound_release_node);
        list_del(&pwq->unbound_release_node);
        safe_mutex_unlock(&pwq_release_mutex);

        pwq_unbound_release(pwq);
    }

    return 0;
}

static void
pwq_adjust_max_active(struct pool_workqueue *pwq)
{
    struct workqueue_struct *wq = pwq->wq;

    /* for @wq->saved_max_active */
    safe_mutex_assert_held(&wq->mutex);

    if (pwq->max_active == wq->saved_max_active)
        return;

    /* this function can be called during early boot w/ irq disabled */
    safe_mutex_lock(&pwq->pool->lock);

    /*
     * During [un]freezing, the caller is responsible for ensuring that
     * this function is called at least once after @workqueue_freezing
     * is updated and visible.
     */
    pwq->max_active = wq->saved_max_active;

    while (!list_empty(&pwq->delayed_works) &&
           pwq->nr_active < pwq->max_active)
        pwq_activate_first_delayed(pwq);

    /*
     * Need to kick a worker after thawed or an unbound wq's
     * max_active is bumped.  It's a slow path.  Do it always.
     */
    wake_up_worker(pwq->pool);

    safe_mutex_unlock(&pwq->pool->lock);
}

/* initialize newly alloced @pwq which is associated with @wq and @pool */
static void
init_pwq(struct pool_workqueue *pwq, struct workqueue_struct *wq,
         struct worker_pool *pool)
{
    BUG_ON((unsigned long)pwq & WORK_STRUCT_FLAG_MASK);

    memset(pwq, 0, sizeof(*pwq));

    pwq->pool = pool;
    pwq->wq = wq;
    pwq->flush_color = -1;
    pwq->refcnt = 1;
    INIT_LIST_HEAD(&pwq->delayed_works);
    INIT_LIST_HEAD(&pwq->pwqs_node);
    INIT_LIST_HEAD(&pwq->mayday_node);
    INIT_LIST_HEAD(&pwq->unbound_release_node);
}

/* sync @pwq with the current state of its associated wq and link it */
static void
link_pwq(struct pool_workqueue *pwq)
{
    struct workqueue_struct *wq = pwq->wq;

    safe_mutex_assert_held(&wq->mutex);

    /* may be called multiple times, ignore if already linked */
    if (!list_empty(&pwq->pwqs_node))
        return;

    /* set the matching work_color */
    pwq->work_color = wq->work_color;

    /* sync max_active to the current setting */
    pwq_adjust_max_active(pwq);

    /* link in @pwq */
    list_add(&pwq->pwqs_node, &wq->pwqs);
}

/* obtain a pool matching @attr and create a pwq associating the pool and @wq */
static struct pool_workqueue *
alloc_unbound_pwq(struct workqueue_struct *wq,
                  const struct workqueue_attrs *attrs)
{
    struct worker_pool *pool;
    struct pool_workqueue *pwq;

    assert_wq_pool_wrlocked();

    pool = get_unbound_pool(attrs);
    if (!pool)
        return NULL;

    if (posix_memalign((void **)&pwq, 1UL << WORK_STRUCT_FLAG_BITS, sizeof(*pwq))) {
        put_unbound_pool(pool);
        return NULL;
    }

    init_pwq(pwq, wq, pool);
    return pwq;
}

struct apply_wqattrs_ctx {
    struct workqueue_struct	*wq;		/* target workqueue */
    struct workqueue_attrs	*attrs;		/* attrs to apply */
    struct list_head	list;		/* queued for batching commit */
    struct pool_workqueue	*dfl_pwq;
};

/* free the resources after success or abort */
static void
apply_wqattrs_cleanup(struct apply_wqattrs_ctx *ctx)
{
    if (ctx) {
        put_pwq_unlocked(ctx->dfl_pwq);

        free_workqueue_attrs(ctx->attrs);

        free_common(ctx);
    }
}

static struct apply_wqattrs_ctx *
apply_wqattrs_prepare(struct workqueue_struct *wq,
                      const struct workqueue_attrs *attrs)
{
    struct apply_wqattrs_ctx *ctx;
    struct workqueue_attrs *new_attrs;

    assert_wq_pool_wrlocked();

    ctx = (struct apply_wqattrs_ctx *)calloc_common(1, sizeof(*ctx));

    new_attrs = alloc_workqueue_attrs();
    if (!ctx || !new_attrs)
        goto out_free;

    copy_workqueue_attrs(new_attrs, attrs);

    ctx->dfl_pwq = alloc_unbound_pwq(wq, new_attrs);
    if (!ctx->dfl_pwq)
        goto out_free;

    ctx->attrs = new_attrs;
    ctx->wq = wq;
    return ctx;

out_free:
    free_workqueue_attrs(new_attrs);
    apply_wqattrs_cleanup(ctx);
    return NULL;
}

/* set attrs and install prepared pwqs, @ctx points to old pwqs on return */
static void
apply_wqattrs_commit(struct apply_wqattrs_ctx *ctx)
{

    /* all pwqs have been created successfully, let's install'em */
    safe_mutex_lock(&ctx->wq->mutex);

    copy_workqueue_attrs(ctx->wq->unbound_attrs, ctx->attrs);

    /* @dfl_pwq might not have been used, ensure it's linked */
    link_pwq(ctx->dfl_pwq);
    Swap(ctx->wq->dfl_pwq, ctx->dfl_pwq);

    safe_mutex_unlock(&ctx->wq->mutex);
}

static int
apply_workqueue_attrs_locked(struct workqueue_struct *wq,
                             const struct workqueue_attrs *attrs)
{
    struct apply_wqattrs_ctx *ctx;

    /* only unbound workqueues can change attributes */
    if (WARN_ON(!(wq->flags & WQ_UNBOUND)))
        return -EINVAL;

    /* creating multiple pwqs breaks ordering guarantee */
    if (!list_empty(&wq->pwqs)) {
        if (WARN_ON(wq->flags & __WQ_ORDERED_EXPLICIT))
            return -EINVAL;

        wq->flags &= ~__WQ_ORDERED;
    }

    ctx = apply_wqattrs_prepare(wq, attrs);
    if (!ctx)
        return -ENOMEM;

    /* the ctx has been prepared successfully, let's commit it */
    apply_wqattrs_commit(ctx);
    apply_wqattrs_cleanup(ctx);

    return 0;
}

int
util_apply_workqueue_attrs(struct workqueue_struct *wq,
                          const struct workqueue_attrs *attrs)
{
    int ret;

    wq_pool_wrlock();
    ret = apply_workqueue_attrs_locked(wq, attrs);
    wq_pool_unlock();

    return ret;
}

static int
alloc_and_link_pwqs(struct workqueue_struct *wq)
{
    bool highpri = wq->flags & WQ_HIGHPRI;
    int ret;

    if (wq->flags & __WQ_ORDERED) {
        ret = util_apply_workqueue_attrs(wq, ordered_wq_attrs[highpri]);
        /* there should only be single pwq for ordering guarantee */
        WARN(!ret && (wq->pwqs.next != &wq->dfl_pwq->pwqs_node ||
                      wq->pwqs.prev != &wq->dfl_pwq->pwqs_node),
             "ordering guarantee broken for workqueue %s\n", wq->name);
    } else {
        ret = util_apply_workqueue_attrs(wq, unbound_std_wq_attrs[highpri]);
    }

    return ret;
}

static int
wq_clamp_max_active(int max_active, unsigned int flags,
                    const char *name)
{
    int lim = flags & WQ_UNBOUND ? WQ_UNBOUND_MAX_ACTIVE : WQ_MAX_ACTIVE;

    if (max_active < 1 || max_active > lim)
        pr_warn("workqueue: max_active %d requested for %s is out of range, clamping between %d and %d\n",
                max_active, name, 1, lim);

    return clamp_val(max_active, 1, lim);
}

struct workqueue_struct *
util_alloc_workqueue(const char *fmt, unsigned int flags, int max_active, ...)
{
    va_list args;
    struct workqueue_struct *wq;
    struct pool_workqueue *pwq;

    /* only support UNBOUND queue */
    always_assert(flags & WQ_UNBOUND);

    if ((flags & WQ_UNBOUND) && max_active == 1)
        flags |= __WQ_ORDERED;

    /* allocate wq and format name */
    if (posix_memalign((void **)&wq, CACHELINE_SIZE, sizeof(*wq)))
        return NULL;

    memset(wq, 0, sizeof(*wq));
    if (flags & WQ_UNBOUND) {
        wq->unbound_attrs = alloc_workqueue_attrs();
        if (!wq->unbound_attrs)
            goto err_free_wq;
    }

    va_start(args, max_active);
    vsnprintf(wq->name, sizeof(wq->name), fmt, args);
    va_end(args);

    max_active = max_active ?: WQ_DFL_ACTIVE;
    max_active = wq_clamp_max_active(max_active, flags, wq->name);

    /* init wq */
    wq->flags = flags;
    wq->saved_max_active = max_active;
    safe_mutex_init(&wq->mutex);
    atomic_set(&wq->nr_pwqs_to_flush, 0);
    INIT_LIST_HEAD(&wq->pwqs);
    INIT_LIST_HEAD(&wq->flusher_queue);
    INIT_LIST_HEAD(&wq->flusher_overflow);
    INIT_LIST_HEAD(&wq->list);

    if (alloc_and_link_pwqs(wq) < 0)
        goto err_free_wq;

    /*
     * wq_pool_lock protects workqueues list.
     * Grab it, adjust max_active and add the new @wq to workqueues
     * list.
     */
    wq_pool_wrlock();

    safe_mutex_lock(&wq->mutex);
    for_each_pwq(pwq, wq) {
        pwq_adjust_max_active(pwq);
    }
    safe_mutex_unlock(&wq->mutex);

    list_add_tail(&wq->list, &workqueues);

    wq_pool_unlock();

    return wq;

err_free_wq:
    free_wq(wq);
    return NULL;
}

static bool
pwq_busy(struct pool_workqueue *pwq)
{
    unsigned int i;

    for (i = 0; i < WORK_NR_COLORS; i++)
        if (pwq->nr_in_flight[i])
            return true;

    if ((pwq != pwq->wq->dfl_pwq) && (pwq->refcnt > 1))
        return true;
    if (pwq->nr_active || !list_empty(&pwq->delayed_works))
        return true;

    return false;
}

void
util_destroy_workqueue(struct workqueue_struct *wq)
{
    struct pool_workqueue *pwq;

    /* drain it before proceeding with destruction */
    util_drain_workqueue(wq);

    /*
     * Sanity checks - grab all the locks so that we wait for all
     * in-flight operations which may do put_pwq().
     */
    wq_pool_wrlock();
    safe_mutex_lock(&wq->mutex);
    for_each_pwq(pwq, wq) {
        safe_mutex_lock(&pwq->pool->lock);
        if (WARN_ON(pwq_busy(pwq))) {
            pr_warn("%s: %s has busy pwq\n",
                    __func__, wq->name);
            safe_mutex_unlock(&pwq->pool->lock);
            safe_mutex_unlock(&wq->mutex);
            wq_pool_unlock();
            return;
        }
        safe_mutex_unlock(&pwq->pool->lock);
    }
    safe_mutex_unlock(&wq->mutex);

    /*
     * remove from list after flushing is complete
     */
    list_del(&wq->list);

    /*
     * Put dfl_pwq.  @wq may be freed any time after dfl_pwq is
     * put.  Don't access it afterwards.
     * We set dfl_pwq to null, see __queue_work, the __queue_work should
     * get null ptr.
     */
    pwq = wq->dfl_pwq;
    wq->dfl_pwq = NULL;

    wq_pool_unlock();

    put_pwq_unlocked(pwq);
}

void
__attribute__((constructor))
util_workqueue_init(void);

void
__attribute__((constructor))
util_workqueue_init(void)
{
    int i, bkt;
    struct worker_pool *pool;

    pool_id_unr = new_unrhdr(0, INT_MAX, NULL);
    BUG_ON(pool_id_unr == NULL);
    BUG_ON(pthread_create(&pwq_release_tid, NULL, pwq_release_thread, NULL));

    for (i = 0; i < NR_STD_WORKER_POOLS; i++) {
        struct workqueue_attrs *attrs;

        BUG_ON(!(attrs = alloc_workqueue_attrs()));
        unbound_std_wq_attrs[i] = attrs;

        /*
         * An ordered wq should have only one pwq as ordering is
         * guaranteed by max_active which is enforced by pwqs.
         */
        BUG_ON(!(attrs = alloc_workqueue_attrs()));
        ordered_wq_attrs[i] = attrs;
    }

    system_unbound_wq = util_alloc_workqueue("events_unbound", WQ_UNBOUND,
                                            WQ_UNBOUND_MAX_ACTIVE);
    BUG_ON(!system_unbound_wq);

    hash_for_each(unbound_pool_hash, bkt, pool, hash_node) {
        BUG_ON(!create_worker(pool));
    }

    wq_online = true;
}

bool util_schedule_work(struct work_struct *work)
{
    return util_queue_work(system_unbound_wq, work);
}
