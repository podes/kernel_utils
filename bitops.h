#ifndef PFS_BITOPS_H
#define PFS_BITOPS_H

#include "bits.h"

/**************************************************************************
 * Behavior is described in Linux kernel Documentation/atomic_bitops.txt
 **************************************************************************/
#if 0
#define BITS_PER_LONG		(sizeof(long) * 8)
#define BIT_MASK(nr)		(1ul << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#endif

static inline void __set_bit(unsigned int nr, volatile unsigned long *p)
{
	p += BIT_WORD(nr);
	*p |= BIT_MASK(nr);
}

static inline void set_bit(unsigned int nr, volatile unsigned long *p)
{
	p += BIT_WORD(nr);
	__atomic_fetch_or(p, BIT_MASK(nr), __ATOMIC_RELAXED);
}

static inline void __clear_bit(unsigned int nr, volatile unsigned long *p)
{
	p += BIT_WORD(nr);
	*p &= ~BIT_MASK(nr);
}

static inline void clear_bit(unsigned int nr, volatile unsigned long *p)
{
	p += BIT_WORD(nr);
	__atomic_fetch_and(p, ~BIT_MASK(nr), __ATOMIC_RELAXED);
}

static inline void __change_bit(unsigned int nr, volatile unsigned long *p)
{
	p += BIT_WORD(nr);
	*p ^= BIT_MASK(nr);
}

static inline int __test_bit(unsigned int nr, const volatile unsigned long *p)
{
	unsigned long mask = BIT_MASK(nr);

	p += BIT_WORD(nr);
	return !!(*p & mask);
}

static inline int test_bit(unsigned int nr, const volatile unsigned long *p)
{
	unsigned long mask = BIT_MASK(nr);

	p += BIT_WORD(nr);
	return !!(*p & mask);
}

static inline void change_bit(unsigned int nr, volatile unsigned long *p)
{
	p += BIT_WORD(nr);
	__atomic_fetch_xor(p, BIT_MASK(nr), __ATOMIC_RELAXED);
}

/* Set a bit and return its old value. */
static inline int __test_and_set_bit(unsigned int nr, volatile unsigned long *p)
{
	unsigned long mask = BIT_MASK(nr);

	p += BIT_WORD(nr);
	if (*p & mask)
		return 1;

	*p |= mask;
	return 0;
}

static inline int test_and_set_bit(unsigned int nr, volatile unsigned long *p)
{
	long old;
	unsigned long mask = BIT_MASK(nr);

	p += BIT_WORD(nr);
	if (*p & mask)
		return 1;

	old = __atomic_fetch_or(p, mask, __ATOMIC_ACQ_REL);
	return !!(old & mask);
}

static inline int __test_and_clear_bit(unsigned int nr, volatile unsigned long *p)
{
	unsigned long mask = BIT_MASK(nr);

	p += BIT_WORD(nr);
	if (!(*p & mask))
		return 0;

	*p &= ~mask;
	return 1;
}

static inline int test_and_clear_bit(unsigned int nr, volatile unsigned long *p)
{
	long old;
	unsigned long mask = BIT_MASK(nr);

	p += BIT_WORD(nr);
	if (!(*p & mask))
		return 0;

	old = __atomic_fetch_and(p, ~mask, __ATOMIC_ACQ_REL);
	return !!(old & mask);
}

static inline int __test_and_change_bit(unsigned int nr, volatile unsigned long *p)
{
	long old;
	unsigned long mask = BIT_MASK(nr);

	p += BIT_WORD(nr);
	old = *p;
	*p ^= mask;
	return !!(old & mask);
}

static inline int test_and_change_bit(unsigned int nr, volatile unsigned long *p)
{
	long old;
	unsigned long mask = BIT_MASK(nr);

	p += BIT_WORD(nr);
	old = __atomic_fetch_xor(p, mask, __ATOMIC_ACQ_REL);
	return !!(old & mask);
}

#endif
