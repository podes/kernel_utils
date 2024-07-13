#include "idr.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

#include "mem.h"

struct idr_node {
	avl_node_t node;
	void *ptr;
	int  id;
};

static int
comp_id(const void *_a, const void *_b)
{
	struct idr_node *a = (struct idr_node *)_a;
	struct idr_node *b = (struct idr_node *)_b;

	if (a->id < b->id)
		return -1;
	if (a->id > b->id)
		return 1;
	return 0;
}

void idr_init_base(struct idr *idr, int base)
{
	init_unrhdr(&idr->unr, base, INT_MAX, NULL);
    dlm_avl_create(&idr->tree, comp_id, sizeof(struct idr_node), offsetof(struct idr_node, node));
}

void idr_destroy(struct idr *idr)
{
	struct idr_node *node;
	void *cookie = NULL;

	while ((node = (struct idr_node *)dlm_avl_destroy_nodes(&idr->tree, &cookie)) != NULL) {
		free_common(node);
	}
    dlm_avl_destroy(&idr->tree);
}

int idr_alloc(struct idr *idr, void *ptr)
{
	struct idr_node *node = (struct idr_node *)malloc_common(sizeof(struct idr_node));
	if (node == NULL)
		return -ENOMEM;

	int id = alloc_unr(&idr->unr);
	if (id == -1) {
		free_common(node);
		return -ENOMEM;
	}
	node->id = id;
	node->ptr = ptr;
    dlm_avl_add(&idr->tree, node);
	return id;
}

void *idr_remove(struct idr *idr, int id)
{
	struct idr_node node, *old;
	void *ptr = NULL;

	node.id = id;
	old = (struct idr_node *)dlm_avl_find(&idr->tree, &node, NULL);
	if (old) {
		ptr = old->ptr;
		free_unr(&idr->unr, id);
        dlm_avl_remove(&idr->tree, old);
        free_common(old);
	}
	return ptr;
}

int idr_for_each(struct idr *idr,
 	int (*fn)(int id, void *p, void *data),
 	void *data)
{
	struct idr_node *p, *next;
	int ret = 0;

	for (p = (struct idr_node *)dlm_avl_first(&idr->tree); p; p = next) {
		next = (struct idr_node *)AVL_NEXT(&idr->tree, p);
		if ((ret = fn(p->id, p->ptr, data)))
			break;
	}
	return ret;
}

void *idr_get_next(struct idr *idr, int *nextid)
{
	struct idr_node node, *old;
	avl_index_t where;
	void *ptr = NULL;

	node.id = *nextid;
	old = (struct idr_node *)dlm_avl_find(&idr->tree, &node, &where);
	if (old == NULL)
		old = (struct idr_node *)dlm_avl_nearest(&idr->tree, where, AVL_AFTER);
	if (old) {
		ptr = old->ptr;
		*nextid = old->id;
	}
	return ptr;
}

void *idr_find(struct idr *idr, int id)
{
	struct idr_node node, *old;
	avl_index_t where;
	void *ptr = NULL;

	node.id = id;
	old = (struct idr_node *)dlm_avl_find(&idr->tree, &node, &where);
	if (old) {
		ptr = old->ptr;
	}
	return ptr;
}

#ifdef IDR_TEST

#include <stdio.h>
#include <string.h>

int print_id(int id, void *p, void *data)
{
    printf("%d %p %p\n", id, p, data);
    return 0;
}

int main()
{
    struct idr idr;
    
    memset(&idr, 0, sizeof(idr));
    idr_init_base(&idr, 1);
    int id = idr_alloc(&idr, (void *)100);
    printf("%d\n", id);
    printf("%p\n", idr_find(&idr, id));

    int id2 = idr_alloc(&idr, (void *)200);

    idr_for_each(&idr, print_id, 0);

    idr_remove(&idr, id);
    printf("%p\n", idr_find(&idr, id));
    printf("%p\n", idr_find(&idr, id2));
    return 0;
}

#endif
