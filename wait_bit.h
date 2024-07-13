#ifndef PFS_LIB_WAIT_BIT_H
#define PFS_LIB_WAIT_BIT_H 1

#include "task_state.h"

#ifdef __cplusplus
extern "C" {
#endif

int     util_wait_on_bit(unsigned long *word, int bit, unsigned mode);
void    util_wake_up_bit(void *word, int bit);

#define wait_on_bit util_wait_on_bit
#define wake_up_bit util_wake_up_bit

#ifdef __cplusplus
}
#endif
#endif
