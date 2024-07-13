#ifndef PFS_LIB_ASSERT_H
#define PFS_LIB_ASSERT_H

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void __assert_fail(const char *__assertion, const char *__file,
                          unsigned int __line, const char *__function)
         __THROW __attribute__ ((__noreturn__));

#ifdef __cplusplus
}
#endif

#define always_assert(expr)                           \
  ((expr)                               \
   ? __ASSERT_VOID_CAST (0)                     \
   : __assert_fail (__STRING(expr), __FILE__, __LINE__, __func__))

#endif /* PFS_LIB_ASSERT_H */
