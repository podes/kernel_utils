
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int kstrtoint(const char *s, int base, int *res);
int kstrtouint(const char *s, unsigned int base, unsigned int *res);

#ifdef __cplusplus
}
#endif

