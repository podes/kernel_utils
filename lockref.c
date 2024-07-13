#include "lockref.h"
#include "assert.h"
#include <errno.h>

void
util_lockref_init(struct lockref *lockref, int c)
{
    pthread_mutex_init(&lockref->lock, NULL);
    lockref->count = c; 
}

void
util_lockref_fini(struct lockref *lockref)
{
    pthread_mutex_destroy(&lockref->lock);
}

void
util_lockref_get(struct lockref *lockref)
{
    pthread_mutex_lock(&lockref->lock);
    lockref->count++;
    pthread_mutex_unlock(&lockref->lock);
}

int
util_lockref_get_not_dead(struct lockref *lockref)
{
    int retval;

    pthread_mutex_lock(&lockref->lock);
    retval = 0;
    if (lockref->count >= 0) {
        lockref->count++;
        retval = 1;
    }
    pthread_mutex_unlock(&lockref->lock);
    return retval;
}

void
util_lockref_mark_dead(struct lockref *lockref)
{
    // assert mutex is locked
    always_assert(pthread_mutex_trylock(&lockref->lock) == EBUSY);
    lockref->count = -128;
}

/**
 * lockref_put_or_lock - decrements count unless count <= 1 before decrement
 * @lockref: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count <= 1 and lock taken
 */
int
util_lockref_put_or_lock(struct lockref *lockref)
{
    pthread_mutex_lock(&lockref->lock);
    if (lockref->count <= 1)
        return 0;
    lockref->count--;
    pthread_mutex_unlock(&lockref->lock);
    return 1;
}


/**
 * lockref_put_not_zero - Decrements count unless count <= 1 before decrement
 * @lockref: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count would become zero
 */
int
util_lockref_put_not_zero(struct lockref *lockref)
{
    int retval;

    pthread_mutex_lock(&lockref->lock);
    retval = 0;
    if (lockref->count > 1) {
        lockref->count--;
        retval = 1;
    }
    pthread_mutex_unlock(&lockref->lock);
    return retval;
}

