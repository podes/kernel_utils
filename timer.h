#ifndef PFS_LIB_TIMER_H
#define PFS_LIB_TIMER_H

#include "llist.h"
#include "offset_of.h"

#ifdef __cplusplus
extern "C" {
#endif

struct timer_list;
typedef void (*timer_func_t)(struct timer_list *);

struct timer_list {
	struct list_head entry;
	long expires;
	timer_func_t function;
	unsigned long flags;
};

#define __TIMER_INITIALIZER(func, exp) {{0, 0}, 0, func, 0}

#define from_timer(var, callback_timer, timer_fieldname) \
    container_of(callback_timer, __typeof__(*var), timer_fieldname)

void __util_init_timer(struct timer_list *timer, timer_func_t func, unsigned long flags);
#define util_init_timer __util_init_timer

static inline int
timer_pending(const struct timer_list * timer)
{
    return timer->entry.next != NULL;
}
#define timer_setup(timer, callback, flags)         \
    __util_init_timer((timer), (callback), (flags))

int  util_del_timer(struct timer_list * timer);
int  util_del_timer_sync(struct timer_list *timer);
int  util_try_to_del_timer_sync(struct timer_list *timer);
int  util_mod_timer(struct timer_list *timer, unsigned long expires);
int  util_mod_timer_pending(struct timer_list *timer, unsigned long expires);
void util_add_timer(struct timer_list *timer);

#ifdef __cplusplus
}
#endif

#endif
