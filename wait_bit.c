#include "wait_bit.h"
#include "llist.h"
#include "atomic.h"
#include "bitops.h"
#include "hash.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>

#define HASH_SIZE   1024
#define HASH_BITS   10

struct bucket {
    struct list_head list;
    pthread_mutex_t  lock;
};

struct waiter {
    struct list_head link;
    sem_t sem;
    unsigned long *addr;
    int bit;
};

static struct bucket hash_tbl[HASH_SIZE];

static void  __attribute__((constructor))
init_table(void)
{
    int i;

    for (i = 0; i < HASH_SIZE; ++i) {
        pthread_mutex_init(&hash_tbl[i].lock, NULL);
        INIT_LIST_HEAD(&hash_tbl[i].list);
    }
}

static void
waiter_init(struct waiter *w, unsigned long *addr, int bit) 
{
    INIT_LIST_HEAD(&w->link);
    sem_init(&w->sem, 0, 0);
    w->addr = addr;
    w->bit = bit;
}

static void
waiter_fini(struct waiter *w)
{
    sem_destroy(&w->sem);
}

static inline unsigned
hash_idx(void *addr)
{
    return hash_32((uintptr_t)addr, HASH_BITS);
}

int
util_wait_on_bit(unsigned long *word, int bit, unsigned mode)
{
    int ret;
    int idx = hash_idx(word);

    if (!test_bit(bit, word))
        return 0;

    struct waiter w;
    waiter_init(&w, word, bit);

    pthread_mutex_lock(&hash_tbl[idx].lock);
    if (!test_bit(bit, word)) {
        pthread_mutex_unlock(&hash_tbl[idx].lock);
        waiter_fini(&w);
        return 0;
    }

    list_add(&w.link, &hash_tbl[idx].list);
    pthread_mutex_unlock(&hash_tbl[idx].lock);

wait:
    ret = sem_wait(&w.sem);
    if (ret == 0 && !test_bit(bit, word)) {
        waiter_fini(&w);
        return 0;
    }

    pthread_mutex_lock(&hash_tbl[idx].lock);
    if (test_bit(bit, word)) {
        if (list_empty(&w.link)) {
            list_add(&w.link, &hash_tbl[idx].list);
        }
        pthread_mutex_unlock(&hash_tbl[idx].lock);
        goto wait;
    }

    list_del_init(&w.link);
    pthread_mutex_unlock(&hash_tbl[idx].lock);

    waiter_fini(&w);
    return 0;
}

void
util_wake_up_bit(void *word, int bit)
{
    struct waiter *w, *savenext;
    int idx = hash_idx(word);

    pthread_mutex_lock(&hash_tbl[idx].lock);
    list_for_each_entry_safe(w, savenext, &hash_tbl[idx].list, link) {
        if (w->addr == word && w->bit == bit) {
            list_del_init(&w->link);
            sem_post(&w->sem); // must be last func to call
        }
    }
    pthread_mutex_unlock(&hash_tbl[idx].lock);
}
