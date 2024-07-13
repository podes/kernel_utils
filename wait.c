#include "wait.h"
#include "time_util.h"

#include <errno.h>

// precondition: wq_head->lock must be locked
long __wait_jiffies(struct wait_queue_head *wq_head, long timeout)
{
    struct timespec ts, now;
    long rc;

    jiffies_to_timespec(timeout, &ts);
    clock_gettime(CLOCK_REALTIME, &now);
    timespecadd(&ts, &now, &ts);

    int ret = pthread_cond_timedwait(&wq_head->cond, &wq_head->lock, &ts);
    if (ret == ETIMEDOUT) {
        rc = 0;
    } else {
        clock_gettime(CLOCK_REALTIME, &now);
        if (timespeccmp(&now, &ts, >=)) {
            rc = 0;
        } else {
            timespecsub(&ts, &now, &ts);
            rc = timespec_to_jiffies(&ts);
        }
    }
    return rc;
}

#ifdef WQ_TEST
#include <stdio.h>

wait_queue_head_t wq;
int value;

void *routine(void *arg)
{
    sleep(1);
    value = 1;
    wake_up(&wq);
}

int main()
{
    pthread_t td;
    init_waitqueue_head(&wq);

//    pthread_create(&td, NULL, routine, NULL);
    int rv = wait_event_timeout(wq, value != 0, 200);
    printf("%d\n", rv); 
}

#endif
