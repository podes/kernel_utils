#ifndef _PFS_LIB_ATOMIC_LONG_H_
#define _PFS_LIB_ATOMIC_LONG_H_

#include "compiler.h"

typedef long atomic_long_t;

#define ATOMIC_LONG_INIT(i)  { (i) }

static __always_inline void
atomic_long_set(atomic_long_t *v, long i)
{
    __atomic_store_n(v, i, __ATOMIC_RELEASE);
}

static __always_inline atomic_long_t
atomic_long_read(atomic_long_t *v)
{
    return __atomic_load_n(v, __ATOMIC_ACQUIRE);
}

#endif /* _PFS_LIB_ATOMIC_LONG_H_ */
