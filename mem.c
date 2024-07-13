#include "mem.h"
#include <stdlib.h>

typedef void* (*mem_alloc_fn)(size_t sz);
typedef void* (*mem_calloc_fn)(int n, size_t sz);
typedef void (*mem_free_fn)(void* ptr);

static void* dummy_mem_alloc(size_t sz)
{
	return malloc(sz);
}

static void* dummy_mem_calloc(int n, size_t sz)
{
	return calloc(n, sz);
}

static void dummy_mem_free(void* ptr)
{
	if (ptr)
		free(ptr);
}

mem_alloc_fn alloc_default = dummy_mem_alloc;
mem_calloc_fn calloc_default = dummy_mem_calloc;
mem_free_fn free_default = dummy_mem_free;

void* malloc_common(size_t sz)
{
	return alloc_default(sz);
}

void* calloc_common(int n, size_t sz)
{
	return calloc_default(n, sz);
}

void free_common(void* ptr)
{
	free_default(ptr);
}

