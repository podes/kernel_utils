#include "kernel.h"

#include <sched.h>
#include <stdlib.h>

#include "mem.h"

void *
__util_alloc_per_cpu(size_t elem_size)
{
	void *ret;

	if (!posix_memalign(&ret, CACHELINE_SIZE, NR_CPUS * elem_size))
		return ret;
    return NULL;
}

void
__util_free_per_cpu(void *mem)
{
    free_common(mem);
}

void *
__util_this_cpu_ptr(void *p, size_t elem_size)
{
    int cpu = sched_getcpu();
    
    while (cpu >= NR_CPUS) {
        cpu /= 2;
    }

    return ((char *)p) + elem_size *cpu;
}
