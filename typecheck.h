/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PFS_LIB_TYPECHECK_H
#define PFS_LIB_TYPECHECK_H

/*
 * Check at compile time that something is of a particular type.
 * Always evaluates to 1 so you may use it easily in comparisons.
 */
#define typecheck(type,x) \
({	type __dummy; \
	__typeof__(x) __dummy2; \
	(void)(&__dummy == &__dummy2); \
	1; \
})

/*
 * Check at compile time that 'function' is a certain type, or is a pointer
 * to that type (needs to use typedef for the function type.)
 */
#define typecheck_fn(type,function) \
({	__typeof__(type) __tmp = function; \
	(void)__tmp; \
})

#endif	/* PFS_LIB_TYPECHECK_H */
