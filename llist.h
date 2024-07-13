/* Copyright (C) 2002, 2009 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#ifndef __LIST_H__
#define __LIST_H__	1

#include <stddef.h>

#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


/* The definitions of this file are adopted from those which can be
   found in the Linux kernel headers to enable people familiar with
   the latter find their way in these sources as well.  */


/* Basic type for the double-link list.  */
struct list_head
{
  struct list_head *next;
  struct list_head *prev;
};

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

typedef struct list_head list_t;

/* Define a variable with the head and tail of the list.  */
#define LLIST_HEAD(name) \
  list_t name = { &(name), &(name) }

#define LLIST_HEAD_VALUE(name) \
  { &(name), &(name) }

/* Initialize a new list head.  */
#define INIT_LIST_HEAD(ptr) \
  (ptr)->next = (ptr)->prev = (ptr)


/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_add(struct list_head *newp,
			      struct list_head *prev,
			      struct list_head *next)
{
	next->prev = newp;
	newp->next = next;
	newp->prev = prev;
	WRITE_ONCE(prev->next, newp);
}

/* Add new element at the head of the list.  */
static inline void
list_add (list_t *newp, list_t *head)
{
    newp->next = head->next;
    newp->prev = head;
    head->next->prev = newp;
    head->next = newp;
}

static inline void
list_add_tail (list_t *newp, list_t *head)
{
  newp->prev = head->prev;
  newp->next = head;
  head->prev->next = newp;
  head->prev = newp;
}

/* Remove element from list.  */
static inline void
list_del (list_t *elem)
{
  elem->next->prev = elem->prev;
  elem->prev->next = elem->next;

  elem->next = 0;
  elem->prev = 0;
}

static inline void
list_del_init (list_t *elem)
{
  elem->next->prev = elem->prev;
  elem->prev->next = elem->next;
  INIT_LIST_HEAD(elem);
}


static inline list_t *
list_next (list_t *elem)
{
  return elem->next;
}

static inline list_t *
list_prev (list_t *elem)
{
  return elem->prev;
}

static inline void
list_move_tail(struct list_head *list,
               struct list_head *head)
{
    list_del_init(list);
    list_add_tail(list, head);
}

static inline void
list_move(struct list_head *list, struct list_head *head)
{
    list_del_init(list);
    list_add(list, head);
}

static inline int list_empty(const struct list_head *head)
{
    return head->next == head;
}

static inline void __list_splice(const struct list_head *list,
				 struct list_head *prev,
				 struct list_head *next)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/* Join two lists.  */
static inline void list_splice(const struct list_head *list,
				struct list_head *head)
{
	if (!list_empty(list))
		__list_splice(list, head, head->next);
}

/**
 * list_splice_tail - join two lists, each list being a queue
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice_tail(struct list_head *list,
				struct list_head *head)
{
	if (!list_empty(list))
		__list_splice(list, head->prev, head);
}

/**
 * list_splice_init - join two lists and reinitialise the emptied list.
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * The list at @list is reinitialised
 */
static inline void list_splice_init(struct list_head *list,
				    struct list_head *head)
{
	if (!list_empty(list)) {
		__list_splice(list, head, head->next);
		INIT_LIST_HEAD(list);
	}
}

/**
 * list_splice_tail_init - join two lists and reinitialise the emptied list
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * Each of the lists is a queue.
 * The list at @list is reinitialised
 */
static inline void list_splice_tail_init(struct list_head *list,
					 struct list_head *head)
{
	if (!list_empty(list)) {
		__list_splice(list, head->prev, head);
		INIT_LIST_HEAD(list);
	}
}



/* Get typed element from list at a given position.  */
#define list_entry(ptr, type, member) \
  ((type *) ((char *) (ptr) - (unsigned long) (&((type *) 0)->member)))


#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member) \
    list_entry((ptr)->prev, type, member)

#define list_next_entry(pos, member) \
    list_entry((pos)->member.next, __typeof__(*(pos)), member)

#define list_prev_entry(pos, member) \
    list_entry((pos)->member.prev, __typeof__(*(pos)), member)

/* Iterate forward over the elements of the list.  */
#define list_for_each(pos, head) \
  for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)              \
    for (pos = list_first_entry(head, __typeof__(*pos), member);    \
         &pos->member != (head);                    \
         pos = list_next_entry(pos, member))


/* Iterate forwards over the elements list.  The list elements can be
   removed from the list while doing this.  */
#define list_for_each_safe(pos, p, head) \
  for (pos = (head)->next, p = pos->next; \
       pos != (head); \
       pos = p, p = pos->next)

#define list_for_each_entry_safe(pos, p, head, member)      \
  for (pos = list_first_entry(head, __typeof__(*pos), member),  \
          p = list_next_entry(pos, member);                 \
       &pos->member != (head);                              \
       pos = p, p = list_next_entry(pos, member))

#define list_for_each_entry_safe_from(pos, n, head, member)             \
    for (n = list_next_entry(pos, member);                  \
         &pos->member != (head);                        \
         pos = n, n = list_next_entry(n, member))

/* Iterate forward over the elements of the list.  */
#define list_for_each_prev(pos, head) \
  for (pos = (head)->prev; pos != (head); pos = pos->prev)


/* Iterate backwards over the elements list.  The list elements can be
   removed from the list while doing this.  */
#define list_for_each_prev_safe(pos, p, head) \
  for (pos = (head)->prev, p = pos->prev; \
       pos != (head); \
       pos = p, p = pos->prev)

#define HLIST_HEAD_INIT { .first = NULL }
#define HLIST_HEAD(name) struct hlist_head name = {  .first = NULL }
#define INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL)
static inline void INIT_HLIST_NODE(struct hlist_node *h)
{
	h->next = NULL;
	h->pprev = NULL;
}

static inline int hlist_unhashed(const struct hlist_node *h)
{
	return !h->pprev;
}

static inline int hlist_empty(const struct hlist_head *h)
{
	return !h->first;
}

static inline void __hlist_del(struct hlist_node *n)
{
	struct hlist_node *next = n->next;
	struct hlist_node **pprev = n->pprev;

	WRITE_ONCE(*pprev, next);
	if (next)
		next->pprev = pprev;
}

static inline void hlist_del(struct hlist_node *n)
{
	__hlist_del(n);
	n->next = 0;
	n->pprev = 0;
}

static inline void hlist_del_init(struct hlist_node *n)
{
	if (!hlist_unhashed(n)) {
		__hlist_del(n);
		INIT_HLIST_NODE(n);
	}
}

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
	struct hlist_node *first = h->first;
	n->next = first;
	if (first)
		first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}

/* next must be != NULL */
static inline void hlist_add_before(struct hlist_node *n,
					struct hlist_node *next)
{
	n->pprev = next->pprev;
	n->next = next;
	next->pprev = &n->next;
	*(n->pprev) = n;
}

static inline void hlist_add_behind(struct hlist_node *n,
				    struct hlist_node *prev)
{
	n->next = prev->next;
	prev->next = n;
	n->pprev = &prev->next;

	if (n->next)
		n->next->pprev  = &n->next;
}

/* after that we'll appear to be on some hlist and hlist_del will work */
static inline void hlist_add_fake(struct hlist_node *n)
{
	n->pprev = &n->next;
}

static inline int hlist_fake(struct hlist_node *h)
{
	return h->pprev == &h->next;
}

/*
 * Move a list from one list head to another. Fixup the pprev
 * reference of the first entry if it exists.
 */
static inline void hlist_move_list(struct hlist_head *old,
				   struct hlist_head *_new)
{
	_new->first = old->first;
	if (_new->first)
		_new->first->pprev = &_new->first;
	old->first = NULL;
}

#define hlist_entry(ptr, type, member) container_of(ptr,type,member)

#define hlist_for_each(pos, head) \
	for (pos = (head)->first; pos ; pos = pos->next)

#define hlist_for_each_safe(pos, n, head) \
	for (pos = (head)->first; pos && ({ n = pos->next; 1; }); \
	     pos = n)

#define hlist_entry_safe(ptr, type, member) \
	({ __typeof__(ptr) ____ptr = (ptr); \
	   ____ptr ? hlist_entry(____ptr, type, member) : NULL; \
	})

/**
 * hlist_for_each_entry	- iterate over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the hlist_node within the struct.
 */
#define hlist_for_each_entry(pos, head, member)				\
	for (pos = hlist_entry_safe((head)->first, __typeof__(*(pos)), member);\
	     pos;							\
	     pos = hlist_entry_safe((pos)->member.next, __typeof__(*(pos)), member))

/**
 * hlist_for_each_entry_continue - iterate over a hlist continuing after current point
 * @pos:	the type * to use as a loop cursor.
 * @member:	the name of the hlist_node within the struct.
 */
#define hlist_for_each_entry_continue(pos, member)			\
	for (pos = hlist_entry_safe((pos)->member.next, __typeof__(*(pos)), member);\
	     pos;							\
	     pos = hlist_entry_safe((pos)->member.next, __typeof__(*(pos)), member))

/**
 * hlist_for_each_entry_from - iterate over a hlist continuing from current point
 * @pos:	the type * to use as a loop cursor.
 * @member:	the name of the hlist_node within the struct.
 */
#define hlist_for_each_entry_from(pos, member)				\
	for (; pos;							\
	     pos = hlist_entry_safe((pos)->member.next, __typeof__(*(pos)), member))

/**
 * hlist_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		another &struct hlist_node to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the hlist_node within the struct.
 */
#define hlist_for_each_entry_safe(pos, n, head, member) 		\
	for (pos = hlist_entry_safe((head)->first, __typeof__(*pos), member);\
	     pos && ({ n = pos->member.next; 1; });			\
	     pos = hlist_entry_safe(n, __typeof__(*pos), member))

/**
 * list_del_range - deletes range of entries from list.
 * @begin: first element in the range to delete from the list.
 * @end: last element in the range to delete from the list.
 * Note: list_empty on the range of entries does not return true after this,
 * the entries is in an undefined state.
 */
static inline void list_del_range(struct list_head *begin,
				  struct list_head *end)
{
	begin->prev->next = end->next;
	end->next->prev = begin->prev;
}

/**
 * list_for_each_from	-	iterate over a list from one of its nodes
 * @pos:  the &struct list_head to use as a loop cursor, from where to start
 * @head: the head for your list.
 */
#define list_for_each_from(pos, head) \
	for (; pos != (head); pos = pos->next)


#ifdef __cplusplus
}
#endif

#endif	/* list.h */
