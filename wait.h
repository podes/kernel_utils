#pragma once

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wait_queue_head {
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

typedef struct wait_queue_head wait_queue_head_t;

#define wait_event(wq_head, condition)				\
    do {							                \
		if (condition)					            \
			break;					                \
		__wait_event(wq_head, condition);		    \
	} while (0)

#define __wait_event(wq_head, condition) 			\
	pthread_mutex_lock(&(wq_head).lock);			\
	while (!(condition)) {					        \
		pthread_cond_wait(&(wq_head).cond, &(wq_head).lock);	\
	}							                    \
	pthread_mutex_unlock(&(wq_head).lock);	

#define ___wait_cond_timeout(condition)                     \
({                                                          \
    _Bool __cond = (condition);                             \
    if (__cond && !__ret)                                   \
        __ret = 1;                                          \
    __cond || !__ret;                                       \
})

#define __wait_event_timeout(wq_head, condition, timeout)   \
({                                                      \
    int first = 1;                                      \
    pthread_mutex_lock(&(wq_head).lock);                \
    while (!___wait_cond_timeout(condition)) {          \
        __ret = __wait_jiffies(&(wq_head), first ? timeout: __ret);    \
        first = 0;                                      \
    }                                                   \
    pthread_mutex_unlock(&(wq_head).lock);              \
    __ret;                                              \
})

#define wait_event_timeout(wq_head, condition, timeout) \
({                                                      \
    long __ret = timeout;                               \
    if (!___wait_cond_timeout(condition))               \
        __ret = __wait_event_timeout(wq_head, condition, timeout);  \
    __ret;                                              \
})

static inline int
wake_up(wait_queue_head_t *wq_head)
{
    pthread_mutex_lock(&wq_head->lock);
    pthread_cond_broadcast(&wq_head->cond);
    pthread_mutex_unlock(&wq_head->lock);
	return 0;
}

static inline void
init_waitqueue_head(wait_queue_head_t *wq_head)
{
    pthread_mutex_init(&wq_head->lock, NULL);
    pthread_cond_init(&wq_head->cond, NULL);
}

/* internal function */
extern long __wait_jiffies(struct wait_queue_head *wq_head, long timeout);

#ifdef __cplusplus
}
#endif

