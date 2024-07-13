#ifndef PFS_LIST_SORT_H
#define PFS_LIST_SORT_H

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((nonnull(2,3)))
void util_list_sort(void *priv, struct list_head *head,
	       int (*cmp)(void *priv, struct list_head *a,
			  struct list_head *b));
#ifdef __cplusplus
}
#endif

#endif /* PFS_LIST_SORT_H */
