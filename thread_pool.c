//
//  thread_pool.cpp
//  kdlm_v_bufs
//
//  Created by yx on 2022/12/20.
//

#include "thread_pool.h"
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "time_util.h"
#include "mem.h"

static void* thread_routine(void* arg)
{
    kthread_pool_t* pool = (kthread_pool_t*)arg;
    struct work_struct* wk;
    while (1) {
        pthread_mutex_lock(&pool->q_lock);
        while (list_empty(&pool->tasks) && !pool->shutdown) {
            pthread_cond_wait(&pool->q_cond, &pool->q_lock);
        }
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->q_lock);
            pthread_exit(NULL);
        }
        wk = list_first_entry(&pool->tasks, struct work_struct, entry);
        list_del(&wk->entry);
        pthread_mutex_unlock(&pool->q_lock);
        wk->func(wk);
    }
    return NULL;
}

kthread_pool_t* create_pool(int32_t max_thr)
{
    int i;
    kthread_pool_t* pool = (kthread_pool_t*)calloc_common(1, sizeof(kthread_pool_t));
    if (!pool) {
        return NULL;
    }
    pool->thread_count_max = max_thr;
    INIT_LIST_HEAD(&pool->tasks);
    if (pthread_mutex_init(&pool->q_lock, NULL)) {
        free_common(pool);
        return NULL;
    }
    if (pthread_cond_init(&pool->q_cond, NULL)) {
        free_common(pool);
        return NULL;
    }
    pool->pt = (pthread_t*)calloc_common(max_thr, sizeof(pthread_t));
    if (!pool->pt) {
        free_common(pool);
        return NULL;
    }
    for (i = 0; i<max_thr; i++) {
        if (pthread_create(&pool->pt[i], NULL, thread_routine, pool)) {
            free_common(pool);
            free_common(pool->pt);
            return NULL;
        }
    }
    return pool;
}

int kthread_pool_add(kthread_pool_t* pool, struct work_struct* wk)
{
    if (!pool || !wk) {
        return 0;
    }
    pthread_mutex_lock(&pool->q_lock);
    list_add_tail(&wk->entry, &pool->tasks);
    pthread_cond_signal(&pool->q_cond);
    pthread_mutex_unlock(&pool->q_lock);
    return 0;
}

void kthread_pool_destroy(kthread_pool_t* pool)
{
    int i;
    if (!pool)
        return;
    
    if (pool->shutdown)
        return;
    
    pool->shutdown = 1;
    pthread_mutex_lock(&pool->q_lock);
    pthread_cond_broadcast(&pool->q_cond);
    pthread_mutex_unlock(&pool->q_lock);
    for (i = 0; i<pool->thread_count_max; i++) {
        pthread_join(pool->pt[i], NULL);
    }
    free_common(pool->pt);
    INIT_LIST_HEAD(&pool->tasks);
    pthread_mutex_destroy(&pool->q_lock);
    pthread_cond_destroy(&pool->q_cond);
}

void kthread_pool_flush(kthread_pool_t* pool)
{
    if (!pool) {
        return;
    }
    if (pool->shutdown) {
        return;
    }
    while (1) {
        pthread_mutex_lock(&pool->q_lock);
        if (list_empty(&pool->tasks)) {
            pthread_mutex_unlock(&pool->q_lock);
            break;
        } else {
            pthread_cond_broadcast(&pool->q_cond);
        }
        pthread_mutex_unlock(&pool->q_lock);
    }
    exactly_usleep(10);
}
