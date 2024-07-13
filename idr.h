#pragma once

#include <pthread.h>

#include "unr.h"
#include "avl.h"

#ifdef __cplusplus
extern "C" {
#endif

struct idr {
	struct unrhdr unr;
	avl_tree_t tree;
};

void	idr_init_base(struct idr *idr, int base);
void	idr_destroy(struct idr *idr);
int 	idr_for_each(struct idr *idr, int (*fn)(int id, void *p, void *data), void *data);
int 	idr_alloc(struct idr *idr, void *ptr);
void	*idr_remove(struct idr *idr, int id);
void	*idr_get_next(struct idr *idr, int *nextid);
void    *idr_find(struct idr *idr, int id);

#define idr_for_each_entry(idr, entry, id)          \
	for (id = 0; ((entry) = idr_get_next(idr, &(id))) != NULL; id += 1U)

#ifdef __cplusplus
}
#endif
