#ifndef PFS_LIB_UNR_H
#define PFS_LIB_URN_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "_unrhdr.h"

void clean_unrhdr(struct unrhdr *uh);
void init_unrhdr(struct unrhdr *uh, int low, int high, pthread_mutex_t *mutex);
struct unrhdr *new_unrhdr(int low, int high, pthread_mutex_t *);
void delete_unrhdr(struct unrhdr *uh);
void clear_unrhdr(struct unrhdr *uh);
void clean_unrhdrl(struct unrhdr *uh);
int alloc_unrl(struct unrhdr *uh);
int alloc_unr(struct unrhdr *uh);
int alloc_unr_specific(struct unrhdr *uh, unsigned item);
void free_unr(struct unrhdr *uh, unsigned item);

#ifdef __cplusplus
}
#endif

#endif
