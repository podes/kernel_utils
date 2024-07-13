#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "random.h"

__attribute__((constructor)) static void init_rand() {
    srand(time(NULL));
}

void get_random_bytes(void *buf, int nbytes)
{
    short v;
    char *p = buf;
    int cpbytes, left = nbytes;

    while (left > 0) {
        v = rand();
        cpbytes = (int)sizeof(v) > left ? left : (int)sizeof(v);    
        memcpy(p, &v, cpbytes);
        left -= cpbytes;    
        p += cpbytes;
    }
}
