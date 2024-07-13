#ifndef PFS_LIB_COMPLETION_H
#define PFS_LIB_COMPLETION_H

#include "llist.h"
#include "stdbool.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct completion {
    unsigned int done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct list_head list;
};

#define util_init_completion(x) __util_init_completion(x)

static inline void __util_init_completion(struct completion *x)
{
    x->done = 0;
    pthread_mutex_init(&x->lock, NULL);
    pthread_cond_init(&x->cond, NULL);
    INIT_LIST_HEAD(&x->list);
}

static inline void util_reinit_completion(struct completion *x)
{
    x->done = 0;
}

#define COMPLETION_INITIALIZER(work) \
    { 0, PTHREAD_MUTEX_INITIALIZER,  PTHREAD_COND_INITIALIZER, LLIST_HEAD_VALUE(work.list) }

#define DECLARE_COMPLETION(work) \
    struct completion work = COMPLETION_INITIALIZER(work)

void util_wait_for_completion(struct completion *);
bool util_try_wait_for_completion(struct completion *x);
unsigned long util_wait_for_completion_timeout(struct completion *x,
                                unsigned long timeout);
bool util_completion_done(struct completion *x);
void util_complete(struct completion *);
void util_complete_all(struct completion *);

#ifdef __cplusplus
}
#endif

#endif  /* PFS_LIB_COMPLETION_H */
