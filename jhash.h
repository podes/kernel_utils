#pragma once

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

u32 jhash(const void *key, u32 length, u32 initval);

u32 jcrc32(const char* src, u32 len);

#ifdef __cplusplus
}
#endif

