#ifndef LOCKREF_6cefa1704dbb_H
#define LOCKREF_6cefa1704dbb_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lockref
{
    pthread_mutex_t lock;
    int count;
};

extern void util_lockref_init(struct lockref *lockref, int c);
extern void util_lockref_fini(struct lockref *lockref);
extern void util_lockref_get(struct lockref *lockref);
extern int  util_lockref_get_not_dead(struct lockref *lockref);
extern void util_lockref_mark_dead(struct lockref *lockref);
extern int  util_lockref_put_not_zero(struct lockref *lockref);
extern int  util_lockref_put_or_lock(struct lockref *lockref);

/* Must be called under spinlock for reliable results */
static inline int __util_lockref_is_dead(const struct lockref *l)
{
    return ((int)l->count < 0);
}

#ifdef __cplusplus
}
#endif

#endif
