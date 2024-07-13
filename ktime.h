#pragma once

#include <stdint.h>
#include "stdint.h"

typedef uint64_t ktime_t;

uint64_t util_ktime_get(void);

static inline s64 util_ktime_to_us(const ktime_t kt)
{
    return kt / 1000;
}

static inline ktime_t ktime_add_us(const ktime_t kt, const u64 usec)
{
    return kt + usec * 1000;
}

#define ktime_sub(lhs, rhs) ((lhs) - (rhs))

#define ktime_get       util_ktime_get
#define ktime_to_us     util_ktime_to_us

