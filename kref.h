#pragma once

struct kref {
	int refcount;
};

#define KREF_INIT(n)	{ .refcount = 0, }

/**
 * kref_init - initialize object.
 * @kref: object in question.
 */
static inline void kref_init(struct kref *kref)
{
	__atomic_store_n(&kref->refcount, 1, __ATOMIC_RELEASE);
}

static inline unsigned int kref_read(const struct kref *kref)
{
	return __atomic_load_n(&kref->refcount, __ATOMIC_ACQUIRE);
}

/**
 * kref_get - increment refcount for object.
 * @kref: object.
 */
static inline void kref_get(struct kref *kref)
{
	__atomic_add_fetch(&kref->refcount, 1, __ATOMIC_RELEASE);
}

/**
 * kref_put - decrement refcount for object.
 * @kref: object.
 * @release: pointer to the function that will clean up the object when the
 *	     last reference to the object is released.
 *	     This pointer is required, and it is not acceptable to pass kfree
 *	     in as this function.
 *
 * Decrement the refcount, and if 0, call release().
 * Return 1 if the object was removed, otherwise return 0.  Beware, if this
 * function returns 0, you still can not count on the kref from remaining in
 * memory.  Only use the return value if you want to see if the kref is now
 * gone, not present.
 */
static inline int kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
	if (!__atomic_add_fetch(&kref->refcount, -1, __ATOMIC_RELEASE)) {
		release(kref);
		return 1;
	}
	return 0;
}
