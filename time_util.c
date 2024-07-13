#include "time_util.h"

#include <sys/time.h>
#include <pthread.h>

static struct timeval tv_boot;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void
init_boot_time(void)
{
    gettimeofday(&tv_boot, NULL);
}

void
util_getboottime(struct timeval *foo)
{
    struct timeval tv_now;

    pthread_once(&once, init_boot_time);

    gettimeofday(&tv_now, NULL);
    timersub(&tv_now, &tv_boot, foo);
}

unsigned long
util_get_jiffies(void)
{
    struct timeval tv;

    util_getboottime(&tv);
    return (tv.tv_sec * PFS_HZ) + (PFS_HZ * tv.tv_usec / 1000000);
}

void exactly_usleep(long usecs)
{
    struct timespec ts0;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    long startns = ((long)ts0.tv_sec)*PFS_NS_PER_SEC + ts0.tv_nsec;
    while (1) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long endns = ((long)ts.tv_sec)*PFS_NS_PER_SEC + ts.tv_nsec;
        if (startns + (usecs*1000) < endns)
            break;
    }
}
