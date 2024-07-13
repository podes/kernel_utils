#ifndef PFS_LIB_TIME_UTIL_H
#define PFS_LIB_TIME_UTIL_H

#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "typecheck.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PFS_HZ 1000
#define PFS_US_PER_SEC 1000000
#define PFS_NS_PER_SEC 1000000000

#define timespecclear(tvp)      ((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define timespecisset(tvp)      ((tvp)->tv_sec || (tvp)->tv_nsec)
#define timespeccmp(tvp, uvp, cmp)                                      \
        (((tvp)->tv_sec == (uvp)->tv_sec) ?                             \
            ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :                       \
            ((tvp)->tv_sec cmp (uvp)->tv_sec))

#define timespecadd(tsp, usp, vsp)                                      \
        do {                                                            \
                (vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;          \
                (vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;       \
                if ((vsp)->tv_nsec >= 1000000000L) {                    \
                        (vsp)->tv_sec++;                                \
                        (vsp)->tv_nsec -= 1000000000L;                  \
                }                                                       \
        } while (0)
#define timespecsub(tsp, usp, vsp)                                      \
        do {                                                            \
                (vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;          \
                (vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;       \
                if ((vsp)->tv_nsec < 0) {                               \
                        (vsp)->tv_sec--;                                \
                        (vsp)->tv_nsec += 1000000000L;                  \
                }                                                       \
        } while (0)

#define time_after(a,b)     \
    (typecheck(unsigned long, a) && \
     typecheck(unsigned long, b) && \
     ((long)((b) - (a)) < 0))
#define time_before(a,b)    time_after(b,a)

#define time_after_eq(a,b)  \
    (typecheck(unsigned long, a) && \
     typecheck(unsigned long, b) && \
     ((long)((a) - (b)) >= 0))
#define time_before_eq(a,b) time_after_eq(b,a)

/*
 *  * Calculate whether a is in the range of [b, c].
 *   */
#define time_in_range(a,b,c) \
    (time_after_eq(a,b) && \
     time_before_eq(a,c))

void            util_getboottime(struct timeval *tv);
unsigned long   util_get_jiffies(void);

#define jiffies util_get_jiffies()

static inline unsigned long jiffies_to_msecs(const unsigned long j)
{
    return (j * 1000) / PFS_HZ;
}

static inline unsigned long jiffies_to_usecs(const unsigned long j)
{
	return (j * 1000000) / PFS_HZ;
}

static inline void jiffies_to_timespec(const unsigned long j,
									   struct timespec *ts)
{
    ts->tv_sec = j / PFS_HZ;
    ts->tv_nsec = (j % PFS_HZ) * 1000ll * 1000 * 1000 / PFS_HZ;
}

static inline unsigned long timespec_to_jiffies(const struct timespec *ts)
{
    return (ts->tv_sec * PFS_HZ) + (PFS_HZ * ts->tv_nsec / 1000000000ll);
}

static inline void msleep(unsigned int msecs)
{
    usleep(msecs * 1000);
}

void exactly_usleep(long usecs);

#ifdef __cplusplus
}
#endif
#endif

