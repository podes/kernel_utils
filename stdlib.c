
#include "stdlib.h"

#include <stdlib.h>
#include <errno.h>

int kstrtoint(const char *s, int base, int *res)
{
	char *end;
	errno = 0;
	long r = strtol(s, &end, base);
	if (errno) {
		return -errno;
	}
	
	*res = r;
	return 0;
}

int kstrtouint(const char * s, unsigned int base, unsigned int *res)
{
	char *end;
	errno = 0;
	long r = strtol(s, &end, base);
	if (errno) {
		return -errno;
	}
	
	*res = r;
	return 0;
}
