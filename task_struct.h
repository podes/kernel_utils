#ifndef PFS_LIB_TASK_STRUCT_H
#define PFS_LIB_TASK_STRUCT_H

#include <pthread.h>
#include <errno.h>

#include "completion.h"
#include "time_util.h"
#include "task_state.h"

#ifdef __cplusplus
extern "C" {
#endif

struct task_struct
{
    pthread_t tid;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int state;
    void *data;
    int (*entry)(void *data);
    void *arg;
    unsigned long flags;
    int exit_code;
    struct completion exited;
    int refcount;
    char name[128];
};

#define PF_WQ_WORKER 0x01

#define KTHREAD_SHOULD_STOP		1

extern __thread struct task_struct *__util_current_task;

static inline void wake_up_process(struct task_struct *task)
{
    pthread_mutex_lock(&task->mutex);
    task->state = TASK_RUNNING;
    pthread_cond_signal(&task->cond);
    pthread_mutex_unlock(&task->mutex); 
}

static inline void *kthread_data(struct task_struct *task)
{
    return task->data;
}

#define current_task  __util_current_task

static inline void  set_current_state(int state)
{
    __atomic_exchange_n(&__util_current_task->state, state, __ATOMIC_ACQ_REL);
}

static inline void task_schedule(void)
{
    struct task_struct *task = current_task;

    pthread_mutex_lock(&task->mutex);
    while (task->state != TASK_RUNNING) {
        pthread_cond_wait(&task->cond, &task->mutex);
    }
    pthread_mutex_unlock(&task->mutex);
}

static inline long schedule_timeout_interruptible(long timeout)
{
    struct task_struct *task = __util_current_task;
    struct timespec ts, now;
    long rc;

    jiffies_to_timespec(timeout, &ts);
    clock_gettime(CLOCK_REALTIME, &now);
    timespecadd(&ts, &now, &ts);

    set_current_state(TASK_INTERRUPTIBLE);
    pthread_mutex_lock(&task->mutex);
    while (task->state != TASK_RUNNING) {
        int ret = pthread_cond_timedwait(&task->cond, &task->mutex, &ts);
        if (ret == ETIMEDOUT)
            rc = 0;
        else {
            clock_gettime(CLOCK_REALTIME, &now);
            timespecsub(&ts, &now, &ts);
            rc = timespec_to_jiffies(&ts);
            if (rc < 0)
                rc = 0;
        }
        break;
    }
    pthread_mutex_unlock(&task->mutex);
    return rc;
}

struct task_struct *
util_kthread_create(int (*threadfn)(void *data),
               void *data, 
               const char namefmt[], ...);

struct task_struct *
util_kthread_run(int (*threadfn)(void *data),
               void *data, 
               const char namefmt[], ...);

int util_kthread_should_stop(void);

int util_kthread_stop(struct task_struct *t);

#ifdef __cplusplus
}
#endif

#endif
