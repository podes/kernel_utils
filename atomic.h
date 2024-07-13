#ifndef ATOMIC_79652d87_H
#define ATOMIC_79652d87_H

#include "bits.h"

typedef int atomic_t;

#define ATOMIC_INIT(v) (v)

static inline void smp_mb__after_atomic()
{
}

static inline void smp_mb()
{
}

static inline int
atomic_read(atomic_t *addr)
{
    return __atomic_load_n(addr, __ATOMIC_ACQUIRE);
}

static inline void
atomic_set(atomic_t *addr, int i)
{
    __atomic_store_n(addr, i, __ATOMIC_RELEASE);
}

static inline int
atomic_compare_set(atomic_t* addr, int oldval, int newval)
{
    return __atomic_compare_exchange_n(addr, &oldval, newval, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
}

static inline void
atomic_inc(atomic_t *addr)
{
    __atomic_add_fetch(addr, 1, __ATOMIC_RELEASE);
}

static inline void
atomic_dec(atomic_t *addr)
{
    __atomic_add_fetch(addr, -1, __ATOMIC_RELEASE);
}

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
static inline int
atomic_dec_and_test(atomic_t *addr)
{
    return __atomic_add_fetch(addr, -1, __ATOMIC_RELEASE) == 0;
}

#endif // ATOMIC_79652d87_H
