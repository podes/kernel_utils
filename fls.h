#ifndef PFS_LIB_FLS_H
#define PFS_LIB_FLS_H

#include "compiler.h"

/**
 * fls - find last (most-significant) bit set
 * @x: the word to search
 *
 * This is defined the same way as ffs.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */
static __always_inline int fls(unsigned int x)
{
    return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}

static __always_inline int fls64(unsigned long x)
{
    return x ? sizeof(x) * 8 - __builtin_clzl(x) : 0;
}

static __always_inline int fls_long(unsigned long x)
{
    return x ? sizeof(x) * 8 - __builtin_clzl(x) : 0;
}


#endif
