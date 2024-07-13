#ifndef PFS_LIB_SAFE_MUTEX_H
#define PFS_LIB_SAFE_MUTEX_H

#include "stdbool.h"
#include "assert.h"

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct safe_mutex {
    pthread_mutex_t m;
    pthread_t       owner;
};

typedef struct safe_mutex safe_mutex_t;

#define SAFE_MUTEX_INITIALIZER { PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP, 0 }

static inline void
safe_mutex_init(struct safe_mutex *mtx)
{
    int err;
    pthread_mutexattr_t attr;

    mtx->owner = 0;
    err = pthread_mutexattr_init(&attr);
    err |= pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    err |= pthread_mutex_init(&mtx->m, &attr);
    err |= pthread_mutexattr_destroy(&attr);
    always_assert(err == 0);
}

static inline void
safe_mutex_destroy(struct safe_mutex *mtx)
{
    pthread_mutex_destroy(&mtx->m);
}

static inline void
safe_mutex_lock(safe_mutex_t *mtx)
{
    always_assert(pthread_mutex_lock(&mtx->m) == 0);
    mtx->owner = pthread_self();
}

static inline void
safe_mutex_unlock(safe_mutex_t *mtx)
{
    always_assert(pthread_self() == mtx->owner);
    mtx->owner = 0;
    always_assert(pthread_mutex_unlock(&mtx->m) == 0);
}

static inline void
safe_mutex_assert_held(safe_mutex_t *mtx)
{
    always_assert(mtx->owner == pthread_self());
}

static inline bool
safe_mutex_held(safe_mutex_t *mtx)
{
    return (mtx->owner == pthread_self());
}


static int
safe_cond_wait(pthread_cond_t *cond, safe_mutex_t *mtx)
{
    int ret;

    always_assert(mtx->owner == pthread_self());
    mtx->owner = 0;
    ret = pthread_cond_wait(cond, &mtx->m);
    mtx->owner = pthread_self();
    return ret;
}

#ifdef __cplusplus
}
#endif

#endif
