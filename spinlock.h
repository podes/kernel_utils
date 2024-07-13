#pragma once

#include <pthread.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef pthread_spinlock_t spinlock_t;

#define spin_lock(x)		pthread_spin_lock(x)
#define spin_unlock(x)		pthread_spin_unlock(x)
#define spin_lock_init(x)	pthread_spin_init(x, 0)

#define DEFINE_SPINLOCK(x)  \
    spinlock_t x; \
    pthread_spin_init(&x, 0);

#ifdef __cplusplus
}
#endif
