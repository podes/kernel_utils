#pragma once

#include <sys/types.h>


void* malloc_common(size_t sz);

void* calloc_common(int n, size_t sz);

void free_common(void* ptr);
