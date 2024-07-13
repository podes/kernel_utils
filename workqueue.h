#ifndef PFS_LIB_WORKQUEUE_H
#define PFS_LIB_WORKQUEUE_H

#include "stdbool.h"
#include "offset_of.h"
#include "llist.h"
#include "timer.h"
#include "atomic_long.h"
#include "minmax.h"
#include "bitops.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BITS_PER_LONG
#define BITS_PER_LONG (8 * sizeof(long))
#endif

struct work_struct;
struct workqueue_struct;

typedef void (*work_func_t)(struct work_struct *work);
void util_delayed_work_timer_fn(struct timer_list *t);

/*
 * The first word is the work queue pointer and the flags rolled into
 * one
 */
#define work_data_bits(work) ((unsigned long *)(&(work)->data))
#define WORK_NR_CPUS 128

enum {
    WORK_STRUCT_PENDING_BIT = 0,    /* work item is pending execution */
    WORK_STRUCT_DELAYED_BIT = 1,    /* work item is delayed */
    WORK_STRUCT_PWQ_BIT = 2,    /* data points to pwq */
    WORK_STRUCT_LINKED_BIT  = 3,    /* next work is linked to this one */
#ifdef CONFIG_DEBUG_OBJECTS_WORK
    WORK_STRUCT_STATIC_BIT  = 4,    /* static initializer (debugobjects) */
    WORK_STRUCT_COLOR_SHIFT = 5,    /* color for workqueue flushing */
#else
    WORK_STRUCT_COLOR_SHIFT = 4,    /* color for workqueue flushing */
#endif

    WORK_STRUCT_COLOR_BITS  = 4,

    WORK_STRUCT_PENDING = 1 << WORK_STRUCT_PENDING_BIT,
    WORK_STRUCT_DELAYED = 1 << WORK_STRUCT_DELAYED_BIT,
    WORK_STRUCT_PWQ     = 1 << WORK_STRUCT_PWQ_BIT,
    WORK_STRUCT_LINKED  = 1 << WORK_STRUCT_LINKED_BIT,
#ifdef CONFIG_DEBUG_OBJECTS_WORK
    WORK_STRUCT_STATIC  = 1 << WORK_STRUCT_STATIC_BIT,
#else
    WORK_STRUCT_STATIC  = 0,
#endif

    /*
     * The last color is no color used for works which don't
     * participate in workqueue flushing.
     */
    WORK_NR_COLORS      = (1 << WORK_STRUCT_COLOR_BITS) - 1,
    WORK_NO_COLOR       = WORK_NR_COLORS,

    /* not bound to any CPU, prefer the local CPU */
    WORK_CPU_UNBOUND    = WORK_NR_CPUS,

    /*
     * Reserve 8 bits off of pwq pointer w/ debugobjects turned off.
     * This makes pwqs aligned to 256 bytes and allows 15 workqueue
     * flush colors.
     */
    WORK_STRUCT_FLAG_BITS   = WORK_STRUCT_COLOR_SHIFT +
                  WORK_STRUCT_COLOR_BITS,

    /* data contains off-queue information when !WORK_STRUCT_PWQ */
    WORK_OFFQ_FLAG_BASE = WORK_STRUCT_COLOR_SHIFT,

    __WORK_OFFQ_CANCELING   = WORK_OFFQ_FLAG_BASE,
    WORK_OFFQ_CANCELING = (1 << __WORK_OFFQ_CANCELING),


    /*
     * When a work item is off queue, its high bits point to the last
     * pool it was on.  Cap at 31 bits and use the highest number to
     * indicate that no pool is associated.
     */
    WORK_OFFQ_FLAG_BITS = 1,
    WORK_OFFQ_POOL_SHIFT    = WORK_OFFQ_FLAG_BASE + WORK_OFFQ_FLAG_BITS,
    WORK_OFFQ_LEFT      = BITS_PER_LONG - WORK_OFFQ_POOL_SHIFT,
    WORK_OFFQ_POOL_BITS = WORK_OFFQ_LEFT <= 31 ? WORK_OFFQ_LEFT : 31,
    WORK_OFFQ_POOL_NONE = (1LU << WORK_OFFQ_POOL_BITS) - 1,

    /* convenience constants */
    WORK_STRUCT_FLAG_MASK   = (1UL << WORK_STRUCT_FLAG_BITS) - 1,
    WORK_STRUCT_WQ_DATA_MASK = ~WORK_STRUCT_FLAG_MASK,
    WORK_STRUCT_NO_POOL = (unsigned long)WORK_OFFQ_POOL_NONE << WORK_OFFQ_POOL_SHIFT,

    /* bit mask for work_busy() return values */
    WORK_BUSY_PENDING   = 1 << 0,
    WORK_BUSY_RUNNING   = 1 << 1,

    /* maximum string length for set_worker_desc() */
    WORKER_DESC_LEN     = 24,
};

struct work_struct {                                                            
    atomic_long_t data;
    struct list_head entry;
    work_func_t func;
};

#define WORK_DATA_INIT()    ATOMIC_LONG_INIT((unsigned long)WORK_STRUCT_NO_POOL)
#define WORK_DATA_STATIC_INIT() \
    ATOMIC_LONG_INIT((unsigned long)(WORK_STRUCT_NO_POOL | WORK_STRUCT_STATIC))

struct delayed_work {
    struct work_struct work;
    struct timer_list timer;
    /* target workqueue */
    struct workqueue_struct *wq;
};

/**
 * struct workqueue_attrs - A struct for workqueue attributes.
 *
 * This can be used to change attributes of an unbound workqueue.
 */
struct workqueue_attrs {
    int cookie;
};

static inline struct delayed_work *to_delayed_work(struct work_struct *work)
{
    return container_of(work, struct delayed_work, work);
}

struct execute_work {
    struct work_struct work;
};

#define __WORK_INITIALIZER(n, f) {					\
	.data = WORK_DATA_STATIC_INIT(),				\
	.entry	= { &(n).entry, &(n).entry },				\
	.func = (f)							\
	}

#define __DELAYED_WORK_INITIALIZER(n, f, tflags) {			\
	.work = __WORK_INITIALIZER((n).work, (f)),			\
	.timer = __TIMER_INITIALIZER(util_delayed_work_timer_fn,\
				     (tflags)),		\
	}

#define DECLARE_WORK(n, f)						\
	struct work_struct n = __WORK_INITIALIZER(n, f)

#define DECLARE_DELAYED_WORK(n, f)					\
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, 0)

static inline void __util_init_work(struct work_struct *work, int onstack) { }
static inline void util_destroy_work_on_stack(struct work_struct *work) { }
static inline void util_destroy_delayed_work_on_stack(struct delayed_work *work) { }
static inline unsigned int work_static(struct work_struct *work) { return 0; }

#define __INIT_WORK(_work, _func, _onstack)				\
	do {								\
		__util_init_work((_work), _onstack);				\
		(_work)->data = (atomic_long_t) WORK_DATA_INIT();	\
		INIT_LIST_HEAD(&(_work)->entry);			\
		(_work)->func = (_func);				\
	} while (0)

#define INIT_WORK(_work, _func)						\
	__INIT_WORK((_work), (_func), 0)

#define INIT_WORK_ONSTACK(_work, _func)					\
	__INIT_WORK((_work), (_func), 1)

#define __INIT_DELAYED_WORK(_work, _func, _tflags)			\
	do {								\
		INIT_WORK(&(_work)->work, (_func));			\
		__util_init_timer(&(_work)->timer,				\
			     util_delayed_work_timer_fn,			\
			     (_tflags));		\
	} while (0)

#define INIT_DELAYED_WORK(_work, _func)                 \
    __INIT_DELAYED_WORK(_work, _func, 0)

/**
 * work_pending - Find out whether a work item is currently pending
 * @work: The work item in question
 */
#define work_pending(work) \
	test_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))

/**
 * delayed_work_pending - Find out whether a delayable work item is currently
 * pending
 * @w: The work item in question
 */
#define delayed_work_pending(w) \
	work_pending(&(w)->work)

/*
 * Workqueue flags and constants.  For details, please refer to
 * Documentation/core-api/workqueue.rst.
 */
enum {
	WQ_UNBOUND		= 1 << 1, /* not bound to any cpu */
	WQ_HIGHPRI		= 1 << 4, /* high priority */

	__WQ_DRAINING		= 1 << 16, /* internal: workqueue is draining */
	__WQ_ORDERED		= 1 << 17, /* internal: workqueue is ordered */
	__WQ_ORDERED_EXPLICIT	= 1 << 19, /* internal: alloc_ordered_workqueue() */

	WQ_MAX_ACTIVE		= 512,	  /* I like 512, better ideas? */
	WQ_MAX_UNBOUND_PER_CPU	= 4,	  /* 4 * #cpus for unbound wq */
	WQ_DFL_ACTIVE		= WQ_MAX_ACTIVE / 2,
};

/* unbound wq's aren't per-cpu, scale max_active according to #cpus */
#define WQ_UNBOUND_MAX_ACTIVE	\
	max_t(int, WQ_MAX_ACTIVE, WORK_NR_CPUS * WQ_MAX_UNBOUND_PER_CPU)

/**
 * alloc_workqueue - allocate a workqueue
 * @fmt: printf format for the name of the workqueue
 * @flags: WQ_* flags
 * @max_active: max in-flight work items, 0 for default
 * remaining args: args for @fmt
 *
 * Allocate a workqueue with the specified parameters.  For detailed
 * information on WQ_* flags, please refer to
 * Documentation/core-api/workqueue.rst.
 *
 * RETURNS:
 * Pointer to the allocated workqueue on success, %NULL on failure.
 */
struct workqueue_struct *util_alloc_workqueue(const char *fmt,
					 unsigned int flags,
					 int max_active, ...);

/**
 * alloc_ordered_workqueue - allocate an ordered workqueue
 * @fmt: printf format for the name of the workqueue
 * @flags: WQ_* flags (only WQ_FREEZABLE and WQ_MEM_RECLAIM are meaningful)
 * @args...: args for @fmt
 *
 * Allocate an ordered workqueue.  An ordered workqueue executes at
 * most one work item at any given time in the queued order.  They are
 * implemented as unbound workqueues with @max_active of one.
 *
 * RETURNS:
 * Pointer to the allocated workqueue on success, %NULL on failure.
 */
#define util_alloc_ordered_workqueue(fmt, flags, args...)		\
	util_alloc_workqueue(fmt, WQ_UNBOUND | __WQ_ORDERED |		\
			__WQ_ORDERED_EXPLICIT | (flags), 1, ##args)

void util_destroy_workqueue(struct workqueue_struct *wq);
struct workqueue_attrs *util_alloc_workqueue_attrs(void);
void util_free_workqueue_attrs(struct workqueue_attrs *attrs);
int util_apply_workqueue_attrs(struct workqueue_struct *wq,
			  const struct workqueue_attrs *attrs);
void util_flush_workqueue(struct workqueue_struct *wq);
void util_drain_workqueue(struct workqueue_struct *wq);

bool util_flush_work(struct work_struct *work);
bool util_cancel_work_sync(struct work_struct *work);

bool util_flush_delayed_work(struct delayed_work *dwork);
bool util_cancel_delayed_work(struct delayed_work *dwork);
bool util_cancel_delayed_work_sync(struct delayed_work *dwork);

void util_workqueue_set_max_active(struct workqueue_struct *wq,
				     int max_active);

bool util_queue_work(struct workqueue_struct *wq,
                    struct work_struct *work);
bool util_queue_delayed_work(struct workqueue_struct *wq,
                      struct delayed_work *dwork,
                      unsigned long delay);
bool util_schedule_work(struct work_struct *work);

#ifdef __cplusplus
}
#endif
#endif // PFS_LIB_WORKQUEUE_H
