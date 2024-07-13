#ifndef PFS_KERNEL_H
#define PFS_KERNEL_H

#include <stddef.h>
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NR_CPUS         128
#define CACHELINE_SIZE  64

#define alloc_percpu(type)                              \
    ({                                                  \
        void *tmp = __util_alloc_per_cpu(sizeof(type));  \
        (type *) tmp;                                   \
    })

#define free_percpu(p)                                  \
    __util_free_per_cpu(p)

#define this_cpu_ptr(p)                                 \
    ({                                                  \
        void *tmp = __util_this_cpu_ptr(p, sizeof(p[0]));\
        (__typeof__((&(p))[0])) tmp;                    \
    })

extern void *__util_alloc_per_cpu(size_t elem_size);
extern void __util_free_per_cpu(void *p);
extern void *__util_this_cpu_ptr(void *p, size_t elem_size);

extern const char util_hex_asc[];

#define hex_asc_lo(x)   util_hex_asc[((x) & 0x0f)]
#define hex_asc_hi(x)   util_hex_asc[((x) & 0xf0) >> 4]

static inline char *util_hex_byte_pack(char *buf, u8 byte)
{
    *buf++ = hex_asc_hi(byte);
    *buf++ = hex_asc_lo(byte);
    return buf;
}

int util_hex_to_bin(char ch);
int util_hex2bin(u8 *dst, const char *src, size_t count);
char *util_bin2hex(char *dst, const void *src, size_t count);

#ifdef __cplusplus
}
#endif

#endif
