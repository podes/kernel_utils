#include "completion.h"
#include "time_util.h"
#include "compiler.h"

#include <limits.h>
#include <time.h>
#include <errno.h>

void
util_wait_for_completion(struct completion *x)
{
    pthread_mutex_lock(&x->lock);
    while (!x->done) {
        pthread_cond_wait(&x->cond, &x->lock);
    }

    if (x->done != UINT_MAX) {
        x->done--;
    }

    pthread_mutex_unlock(&x->lock);
}

unsigned long
util_wait_for_completion_timeout(struct completion *x,
                                unsigned long timeout)
{
    struct timespec ts, now;
    unsigned long end = timeout + jiffies;
 
    ts.tv_sec = timeout / PFS_HZ;
    ts.tv_nsec = ((1000UL * 1000UL * 1000UL) * (timeout % PFS_HZ)) / PFS_HZ;
    clock_gettime(CLOCK_REALTIME, &now);
    timespecadd(&ts, &now, &ts);

    pthread_mutex_lock(&x->lock);
    while (!x->done) {
        int rv = pthread_cond_timedwait(&x->cond, &x->lock, &ts);
        if (rv == ETIMEDOUT)
            break;
    }
   
    if (x->done) {
        if (x->done != UINT_MAX)
            x->done--;
        pthread_mutex_unlock(&x->lock);
        if (end > jiffies)
            return end - jiffies;
        return 1;
    }

    pthread_mutex_unlock(&x->lock);
    return 0;
}

bool
util_try_wait_for_completion(struct completion *x)
{
    bool ret = true;

    /*
     * Since x->done will need to be locked only
     * in the non-blocking case, we check x->done
     * first without taking the lock so we can
     * return early in the blocking case.
     */
    if (!READ_ONCE(x->done))
        return false;

    pthread_mutex_lock(&x->lock);
    if (!x->done)
        ret = false;
    else if (x->done != UINT_MAX)
        x->done--;
    pthread_mutex_unlock(&x->lock);
    return ret;
}

bool
util_completion_done(struct completion *x)
{

    if (!READ_ONCE(x->done))
        return false;

    /*
     * If ->done, we need to wait for complete() to release ->wait.lock
     * otherwise we can end up freeing the util_completion before complete()
     * is done referencing it.
     */
    pthread_mutex_lock(&x->lock);
    pthread_mutex_unlock(&x->lock);
    return true;
}

void
util_complete(struct completion *x)
{
    pthread_mutex_lock(&x->lock);
    if (x->done != UINT_MAX)
        x->done++;
    pthread_cond_signal(&x->cond);
    pthread_mutex_unlock(&x->lock);
}

void
util_complete_all(struct completion *x)
{
    pthread_mutex_lock(&x->lock);
    x->done = UINT_MAX;
    pthread_cond_broadcast(&x->cond);
    pthread_mutex_unlock(&x->lock);
}

