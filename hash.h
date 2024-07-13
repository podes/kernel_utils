#ifndef PFS_LIB_HASH_H
#define PFS_LIB_HASH_H

#include "compiler.h"
#include <stdint.h>

/* Fast hashing routine for ints,  longs and pointers.
   (C) 2002 Nadia Yvette Chambers, IBM */

#define GOLDEN_RATIO_32 0x61C88647
#define GOLDEN_RATIO_64 0x61C8864680B583EBull

#define hash_long(val, bits) hash_64(val, bits)

static inline uint32_t __hash_32(uint32_t val)
{
	return val * GOLDEN_RATIO_32;
}

static inline uint32_t hash_32(uint32_t val, unsigned int bits)
{
	/* High bits are more random, so use them. */
	return __hash_32(val) >> (32 - bits);
}

static __always_inline uint32_t hash_64(uint64_t val, unsigned int bits)
{
	/* 64x64-bit multiply is efficient on all 64-bit processors */
	return val * GOLDEN_RATIO_64 >> (64 - bits);
}

static inline uint32_t hash_ptr(const void *ptr, unsigned int bits)
{
	return hash_long((unsigned long)ptr, bits);
}

/* This really should be called fold32_ptr; it does no hashing to speak of. */
static inline uint32_t hash32_ptr(const void *ptr)
{
	unsigned long val = (unsigned long)ptr;

	val ^= (val >> 32);
	return (uint32_t)val;
}

#endif /* PFS_LIB_HASH_H */
