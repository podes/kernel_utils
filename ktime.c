#include "ktime.h"

#include <time.h>

uint64_t util_ktime_get(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
