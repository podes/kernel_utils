#include "time_util.h"
#include "llist.h"
#include "timer.h"
#include "hash.h"
#include "stdbool.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct timer_queue {
    struct list_head    all_timers;
    struct timer_list    *active_node;
    pthread_mutex_t     mutex;
    pthread_cond_t      cond;
    pthread_cond_t      waiter_cond;
    pthread_t           timer_thread;
};

#define HASH_BITS  2
#define HASH_SIZE  4

static pthread_once_t   time_base_once = PTHREAD_ONCE_INIT;
static struct timer_queue queues[HASH_SIZE];
static void* timer_entry(void *arg);

static int
init_timer_queue(struct timer_queue *q)
{
    int ret;

    INIT_LIST_HEAD(&q->all_timers);
    q->active_node = NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    pthread_cond_init(&q->waiter_cond, NULL);
    if ((ret = pthread_create(&q->timer_thread, NULL, timer_entry, q))) {
        fprintf(stderr, "%s %s: can not create timer thread, %s\n",
                __FILE__, __func__, strerror(ret));
        return -ret;
    }

    return 0;
}

static unsigned
timer_hash_idx(struct timer_list *timer)
{
    return hash_32((uintptr_t)timer, HASH_BITS);
}

static void
init_timer_base(void)
{
    int i;

    for (i = 0; i < HASH_SIZE; ++i) {
        init_timer_queue(&queues[i]);
    }
    usleep(1);
}

void
__util_init_timer(struct timer_list *timer, timer_func_t func, unsigned long flags)
{
    timer->entry.prev = 0;
    timer->entry.next = 0;
    timer->function = func;
    timer->flags = flags;
}

static int
detach_if_pending(struct timer_list *timer)
{
    if (!timer_pending(timer))
        return 0;

    list_del(&timer->entry);
    timer->entry.prev = 0;
    timer->entry.next = 0;
    return 1;
}

static struct timer_queue *
get_timer_queue(struct timer_list *timer)
{
    int idx;

    idx = timer_hash_idx(timer);
    return &queues[idx];
}

static int
__add_timer(struct timer_list *timer)
{
    struct timer_queue *q = get_timer_queue(timer);
    struct timer_list *c, *prev;

    INIT_LIST_HEAD(&timer->entry);
    if (list_empty(&q->all_timers)) {
        list_add(&timer->entry, &q->all_timers);
        return 1;
    }

    prev = NULL;
    list_for_each_entry(c, &q->all_timers, entry) {
        if (timer->expires < c->expires)
            break;
        prev = c;
    }
    
    list_add_tail(&timer->entry, &c->entry);
    return (prev == NULL);
}

/* mod_timer of an inactive timer returns 0, mod_timer of an active timer returns 1. */
static int
mod_timer(struct timer_list *timer, unsigned long expires, bool pending_only)
{
    pthread_once(&time_base_once, init_timer_base);
    struct timer_queue *q = get_timer_queue(timer);
    int ret;
    int at_head;

    pthread_mutex_lock(&q->mutex);

    ret = detach_if_pending(timer);
    if (!ret && pending_only)
        goto out_unlock;

    timer->expires = expires;

    at_head = __add_timer(timer);
    if (at_head) {
        pthread_cond_signal(&q->cond);
    }

out_unlock:
    pthread_mutex_unlock(&q->mutex);
    return (ret);
}

/* mod_timer of an inactive timer returns 0, mod_timer of an active timer returns 1. */
int
util_mod_timer(struct timer_list *timer, unsigned long expires)
{
    return mod_timer(timer, expires, false);
}

int
util_mod_timer_pending(struct timer_list *timer, unsigned long expires)
{
    return mod_timer(timer, expires, true);
}

void
util_add_timer(struct timer_list *timer)
{
    pthread_once(&time_base_once, init_timer_base);
    if (timer_pending(timer)) {
        fprintf(stderr, "%s %s: fault, timer pending\n", __FILE__, __func__);
        abort();
    }
    util_mod_timer(timer, timer->expires);
}

/**
 *  del_timer - deactivate a timer.
 *  @timer: the timer to be deactivated
 * 
 *  del_timer() deactivates a timer - this works on both active and inactive
 *  timers.
 *
 *  The function returns whether it has deactivated a pending timer or not.
 *  (ie. del_timer() of an inactive timer returns 0, del_timer() of an
 *  active timer returns 1.)
 **/

int
util_del_timer(struct timer_list *timer)
{
    pthread_once(&time_base_once, init_timer_base);
    struct timer_queue *q = get_timer_queue(timer);
    int ret = 0;

    if (timer_pending(timer)) {
        pthread_mutex_lock(&q->mutex);
        ret = detach_if_pending(timer);
        pthread_mutex_unlock(&q->mutex);
    }
    return ret;
}

/**
 * util_try_to_del_timer_sync - Try to deactivate a timer
 * @timer: timer to delete
 *
 * This function tries to deactivate a timer. Upon successful (ret >= 0)
 * exit the timer is not queued and the handler is not running on any CPU.
 **/

int
util_try_to_del_timer_sync(struct timer_list *timer)
{
    pthread_once(&time_base_once, init_timer_base);
    struct timer_queue *q = get_timer_queue(timer);
    int ret = -1;

    pthread_mutex_lock(&q->mutex);
    if (q->active_node != timer) {
        ret = detach_if_pending(timer);
    }

    pthread_mutex_unlock(&q->mutex);
    return ret;
}


static void
del_timer_wait_running(struct timer_list *timer)
{
    struct timer_queue *q = get_timer_queue(timer);

    pthread_mutex_lock(&q->mutex);
    while (q->active_node == timer) {
        pthread_cond_wait(&q->waiter_cond, &q->mutex);
    }
    pthread_mutex_unlock(&q->mutex);
}

/**
 * util_del_timer_sync - deactivate a timer and wait for the handler to finish.
 * @timer: the timer to be deactivated
 *
 * This function only differs from del_timer() : besides deactivating
 * the timer it also makes sure the handler has finished executing.
 * 
 * Synchronization rules: Callers must prevent restarting of the timer,
 * otherwise this function is meaningless.
 * The caller must not hold locks which would prevent completion of the timer's
 * handler. The timer's handler must not call add_timer(). Upon exit the
 * timer is not queued and the handler is not running on any CPU.
 **/

int
util_del_timer_sync(struct timer_list *timer)
{
    int ret;
    pthread_once(&time_base_once, init_timer_base);
    do {
        ret = util_try_to_del_timer_sync(timer);

        if (ret < 0) {
            del_timer_wait_running(timer);
        }
    } while (ret < 0);

    return ret;
}

static void *
timer_entry(void *arg)
{
    struct timer_queue *q = (struct timer_queue *)arg;
    struct timer_list *timer;
    struct timespec ts, ts2;

    pthread_setname_np(pthread_self(), "timer_list");
    pthread_mutex_lock(&q->mutex);
    for (;;) {
        long now = util_get_jiffies();
        while (!list_empty(&q->all_timers)) {
            timer = list_first_entry(&q->all_timers, struct timer_list, entry);
            if (timer->expires > now)
                break;
            list_del(&timer->entry);
            timer->entry.next = NULL;
            q->active_node = timer;
            pthread_mutex_unlock(&q->mutex);

            (*(timer->function))(timer);

            pthread_mutex_lock(&q->mutex);
            q->active_node = NULL;
            pthread_cond_broadcast(&q->waiter_cond);
        }

        if (list_empty(&q->all_timers)) {
            pthread_cond_wait(&q->cond, &q->mutex);
        } else {
            timer = list_first_entry(&q->all_timers, struct timer_list, entry);
            long diff = timer->expires - now;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts2.tv_sec = diff / PFS_HZ;
            ts2.tv_nsec = ((diff % PFS_HZ) * 1000UL * 1000UL * 1000UL)/PFS_HZ;
            timespecadd(&ts, &ts2, &ts);
            pthread_cond_timedwait(&q->cond, &q->mutex, &ts);
        }
    }
    pthread_mutex_unlock(&q->mutex);
    return 0;
}
