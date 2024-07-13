#pragma once

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

int util_ratecheck(struct timeval *lasttime, const struct timeval *mininterval);

#ifdef __cplusplus
}
#endif

