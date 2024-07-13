#ifndef PFS_LIB_STRING_H
#define PFS_LIB_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *dlm_memchr_inv(const void *start, int c, size_t bytes);

size_t strlcpy(char *dest, const char *src, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PFS_LIB_STRING_H */
