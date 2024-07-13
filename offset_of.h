#ifndef PFS_LIB_OFFSET_OF_H
#define PFS_LIB_OFFSET_OF_H

#include <stddef.h>

#ifndef container_of
#define container_of(ptr, type, member) ({          \
    void *__mptr = (void *)(ptr);                   \
    ((type *)(__mptr - offsetof(type, member))); })
#endif

#endif  /* PFS_LIB_OFFSET_OF_H */

