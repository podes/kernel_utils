#include "task_struct.h"
#include "bitops.h"
#include "mem.h"

#include <semaphore.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

__thread struct task_struct *__util_current_task;

struct start_arg
{
    struct task_struct *task;
};

static struct start_arg *
alloc_start_arg()
{
    struct start_arg *sa;

    sa = (struct start_arg *) calloc_common(1, sizeof(*sa));
    if (sa == NULL) {
        return NULL;
    }
    return sa;
}

static void
free_start_arg(struct start_arg *sa)
{
    free_common(sa);
}

static struct task_struct *
alloc_task(void)
{
    struct task_struct *task;

    task = (struct task_struct *)calloc_common(1, sizeof(*task));
    if (task) {
        pthread_mutex_init(&task->mutex, NULL);
        pthread_cond_init(&task->cond, NULL);
	    util_init_completion(&task->exited);
    }
    return task;
}

static void
free_task(struct task_struct *task)
{
    if (task) {
        pthread_mutex_destroy(&task->mutex);
        pthread_cond_destroy(&task->cond);
		// TODO fini completion
        free_common(task);
    }
}

static void
get_task(struct task_struct *task)
{
	__atomic_add_fetch(&task->refcount, 1, __ATOMIC_RELEASE);
}

static void
put_task(struct task_struct *task)
{
	if (__atomic_add_fetch(&task->refcount, -1, __ATOMIC_RELEASE) == 0) {
		free_task(task);
	}
}

static void *
start_routine(void *arg)
{
    struct start_arg *sa = (struct start_arg *)arg;
    struct task_struct *task = sa->task;

    __util_current_task = sa->task;

    pthread_mutex_lock(&task->mutex);
    while (task->state != TASK_RUNNING) {
        pthread_cond_wait(&task->cond, &task->mutex);
    }
    pthread_mutex_unlock(&task->mutex);

    task->exit_code = task->entry(task->arg);
    
    free_start_arg(sa);
    
    util_complete_all(&task->exited);
    put_task(task);
    return 0;
}

static struct task_struct *
do_util_kthread_create(int (*threadfn)(void *data),
			void *data, 
			const char namefmt[],
			va_list ap)
{
    struct start_arg *sa;
    struct task_struct *task;

    task = alloc_task();
    if (task == NULL) {
        return NULL;
    }

    task->state = TASK_NEW;

    sa = alloc_start_arg();
    if (sa == NULL) {
        free_task(task);
        return NULL;
    }

    sa->task = task;

    vsnprintf(task->name, sizeof(task->name), namefmt, ap);

    task->entry = threadfn;
    task->arg = data;
    task->refcount = 1;
    if (pthread_create(&task->tid, NULL, start_routine, sa)) {
        fprintf(stderr, "%s can not create thread\n", __func__);
        free_task(task);
        free_start_arg(sa);
        return NULL;
    }
    pthread_setname_np(task->tid, task->name);
    pthread_detach(task->tid);

    return task;
}

struct task_struct *
util_kthread_create(int (*threadfn)(void *data),
			void *data, 
			const char namefmt[], ...)

{
	struct task_struct *task;
	va_list ap;

	va_start(ap, namefmt);
	task = do_util_kthread_create(threadfn, data, namefmt, ap);
	va_end(ap);
	if (task == NULL)
		errno = ENOMEM; // FIXME
	return task;
}

struct task_struct *
util_kthread_run(int (*threadfn)(void *data),
			void *data, 
			const char namefmt[], ...)
{
	struct task_struct *task;
	va_list ap;

	va_start(ap, namefmt);
	task = do_util_kthread_create(threadfn, data, namefmt, ap);
	va_end(ap);

	if (task) 
		wake_up_process(task);
	return task;
}

int
util_kthread_stop(struct task_struct *k)
{
	int ret;

	get_task(k);
	set_bit(KTHREAD_SHOULD_STOP, &k->flags);
	wake_up_process(k);
	util_wait_for_completion(&k->exited);
	ret = k->exit_code;
	put_task(k);
	return ret;
}

int
util_kthread_should_stop(void)
{
	return test_bit(KTHREAD_SHOULD_STOP, &__util_current_task->flags);
}
