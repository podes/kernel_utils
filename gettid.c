#include "gettid.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE         /* See feature_test_macros(7) */
#endif

#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */

static __thread int my_tid;

int
util_gettid(void)
{
    if (my_tid == 0)
        my_tid = syscall(SYS_gettid);
    return my_tid;
}

