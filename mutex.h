#pragma once

#include <stddef.h>
#include <pthread.h>

#include "gettid.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mutex {
    int spin;
	pthread_mutex_t m;
    pthread_spinlock_t sp;
};

#define MUTEX_INITIALIZER {0, PTHREAD_MUTEX_INITIALIZER, 0}

#define MUTEX_RECURSIVE_INITIALIZER {0, PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP, 0}

static inline int mutex_lock(struct mutex *mtx)
{
    if (mtx->spin) {
        return pthread_spin_lock(&mtx->sp);
    } else {
        return pthread_mutex_lock(&mtx->m);
    }
}

static inline int mutex_unlock(struct mutex *mtx)
{
    if (mtx->spin) {
        return pthread_spin_unlock(&mtx->sp);
    } else {
        return pthread_mutex_unlock(&mtx->m);
    }
}

static inline void mutex_init(struct mutex *mtx, int spin)
{
    mtx->spin = spin != 0;
    if (spin) {
        pthread_spin_init(&mtx->sp, 0);
    } else {
        pthread_mutex_init(&mtx->m, NULL);
    }
}

static inline void mutex_destroy(struct mutex *mtx)
{
    if (mtx->spin) {
        pthread_spin_destroy(&mtx->sp);
    } else {
        pthread_mutex_destroy(&mtx->m);
    }
}

static inline int mutex_owned(struct mutex *mtx)
{
    if (mtx->spin) {
        return 1;
    } else {
        return mtx->m.__data.__owner == util_gettid();
    }
}

#ifdef __cplusplus
}
#endif

