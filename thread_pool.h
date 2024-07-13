//
//  thread_pool.hpp
//  kdlm_v_bufs
//
//  Created by yx on 2022/12/20.
//

#ifndef thread_pool_hpp
#define thread_pool_hpp
#include <stdint.h>
#include <pthread.h>
#include "llist.h"
#include "workqueue.h"

typedef struct kthread_pool {
    int         shutdown;
    list_t      tasks;
    pthread_mutex_t     q_lock;
    pthread_cond_t      q_cond;
    int32_t     thread_count_max;
    pthread_t*   pt;
} kthread_pool_t;


kthread_pool_t* create_pool(int32_t max_thr);

int kthread_pool_add(kthread_pool_t* pool, struct work_struct* wk);

void kthread_pool_destroy(kthread_pool_t* pool);

void kthread_pool_flush(kthread_pool_t* pool);

#endif /* thread_pool_hpp */
