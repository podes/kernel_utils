#ifndef LIB_RWSEM_H
#define LIB_RWSEM_H

#include <pthread.h>
#include <err.h>

struct rw_semaphore {
	pthread_rwlock_t rw;
#ifndef RWSEM_DEBUG
	pthread_t owner;
#endif
};

#define down_read           util_down_read
#define up_read             util_up_read
#define down_write          util_down_write
#define up_write            util_up_write
#define init_rwsem          util_init_rwsem
#define down_read_trylock   util_down_read_trylock

static inline int util_down_read(struct rw_semaphore *sem)
{
	return pthread_rwlock_rdlock(&sem->rw);
}

static inline int util_up_read(struct rw_semaphore *sem)
{
	return pthread_rwlock_unlock(&sem->rw);
}

static inline int util_down_write(struct rw_semaphore *sem)
{
	int ret = pthread_rwlock_wrlock(&sem->rw);
#ifdef RWSEM_DEBUG
	if (ret == 0) {
		sem->owner = pthread_self();
	}
#endif
	return ret;
}

static inline int util_up_write(struct rw_semaphore *sem)
{
#ifdef RWSEM_DEBUG
	if (sem->owner != pthread_self()) {
		errx(EINVAL, "util_up_write not owner");
	}
#endif

	sem->owner = 0;
	return pthread_rwlock_unlock(&sem->rw);
}

static inline int util_init_rwsem(struct rw_semaphore *sem)
{
#ifdef RWSEM_DEBUG
	sem->owner = 0;
#endif
	return pthread_rwlock_init(&sem->rw, NULL);
}

static inline int util_down_read_trylock(struct rw_semaphore *sem)
{
    return !pthread_rwlock_tryrdlock(&sem->rw);
}

#endif
