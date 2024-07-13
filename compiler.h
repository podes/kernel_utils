#ifndef PFS_LIB_COMPILER_H
#define PFS_LIB_COMPILER_H

#include <stdint.h>

#ifndef likely
# define likely(x)          __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
# define unlikely(x)        __builtin_expect(!!(x), 0)
#endif

#ifndef __always_inline
# define __always_inline    inline __attribute__((always_inline))
#endif

#define barrier() __asm__ __volatile__("": : :"memory")

/*
 * Following functions are taken from kernel sources and
 * break aliasing rules in their original form.
 *
 * While kernel is compiled with -fno-strict-aliasing,
 * perf uses -Wstrict-aliasing=3 which makes build fail
 * under gcc 4.4.
 *
 * Using extra __may_alias__ type to allow aliasing
 * in this case.
 */
typedef uint8_t  __attribute__((__may_alias__)) uint8_t_alias_t;
typedef uint16_t __attribute__((__may_alias__)) uint16_t_alias_t;
typedef uint32_t __attribute__((__may_alias__)) uint32_t_alias_t;
typedef uint64_t __attribute__((__may_alias__)) uint64_t_alias_t;

static __always_inline void __read_once_size(const volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(uint8_t_alias_t  *) res = *(volatile uint8_t_alias_t  *) p; break;
	case 2: *(uint16_t_alias_t *) res = *(volatile uint16_t_alias_t *) p; break;
	case 4: *(uint32_t_alias_t *) res = *(volatile uint32_t_alias_t *) p; break;
	case 8: *(uint64_t_alias_t *) res = *(volatile uint64_t_alias_t *) p; break;
	default:
		barrier();
		__builtin_memcpy((void *)res, (const void *)p, size);
		barrier();
	}
}

static __always_inline void __write_once_size(volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(volatile  uint8_t_alias_t *) p = *(uint8_t_alias_t  *) res; break;
	case 2: *(volatile uint16_t_alias_t *) p = *(uint16_t_alias_t *) res; break;
	case 4: *(volatile uint32_t_alias_t *) p = *(uint32_t_alias_t *) res; break;
	case 8: *(volatile uint64_t_alias_t *) p = *(uint64_t_alias_t *) res; break;
	default:
		barrier();
		__builtin_memcpy((void *)p, (const void *)res, size);
		barrier();
	}
}

/*
 * Prevent the compiler from merging or refetching reads or writes. The
 * compiler is also forbidden from reordering successive instances of
 * READ_ONCE and WRITE_ONCE, but only when the compiler is aware of some
 * particular ordering. One way to make the compiler aware of ordering is to
 * put the two invocations of READ_ONCE or WRITE_ONCE in different C
 * statements.
 *
 * These two macros will also work on aggregate data types like structs or
 * unions. If the size of the accessed data type exceeds the word size of
 * the machine (e.g., 32 bits or 64 bits) READ_ONCE() and WRITE_ONCE() will
 * fall back to memcpy and print a compile-time warning.
 *
 * Their two major use cases are: (1) Mediating communication between
 * process-level code and irq/NMI handlers, all running on the same CPU,
 * and (2) Ensuring that the compiler does not fold, spindle, or otherwise
 * mutilate accesses that either do not require ordering or that interact
 * with an explicit memory barrier or atomic instruction that provides the
 * required ordering.
 */

#define READ_ONCE(x)					\
({							\
	union { __typeof__(x) __val; char __c[1]; } __u =	\
		{ .__c = { 0 } };			\
	__read_once_size(&(x), __u.__c, sizeof(x));	\
	__u.__val;					\
})

#define WRITE_ONCE(x, val)				\
({							\
	union { __typeof__(x) __val; char __c[1]; } __u =	\
		{ .__val = (val) }; 			\
	__write_once_size(&(x), __u.__c, sizeof(x));	\
	__u.__val;					\
})

#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a, b)

#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

#define _RET_IP_   (unsigned long)__builtin_return_address(0)
#define _THIS_IP_  ({ __label__ __here; __here: (unsigned long)&&__here; })

#define __aligned(n)    __attribute__((__aligned__(n)))
#define __printf(a, b)  __attribute__((format(printf, a, b)))

#endif /* PFS_LIB_COMPILER_H */
