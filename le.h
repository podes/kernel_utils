#pragma once

#include "byteorder.h"
#include "bitops.h"

#if __BYTE_ORDER == __LITTLE_ENDIAN

#define BITOP_LE_SWIZZLE    0

#else

#define BITOP_LE_SWIZZLE    ((BITS_PER_LONG-1) & ~0x7)

#endif

static inline int test_bit_le(int nr, const void *addr)
{
	return test_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline void set_bit_le(int nr, void *addr)
{
	set_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline void clear_bit_le(int nr, void *addr)
{
	clear_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline void __set_bit_le(int nr, void *addr)
{
	__set_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline void __clear_bit_le(int nr, void *addr)
{
	__clear_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline int test_and_set_bit_le(int nr, void *addr)
{
	return test_and_set_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline int test_and_clear_bit_le(int nr, void *addr)
{
	return test_and_clear_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline int __test_and_set_bit_le(int nr, void *addr)
{
	return __test_and_set_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}

static inline int __test_and_clear_bit_le(int nr, void *addr)
{
	return __test_and_clear_bit(nr ^ BITOP_LE_SWIZZLE, addr);
}
