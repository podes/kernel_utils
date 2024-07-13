#ifndef _PFS_LIB_QUEUE_PATCH_H_
#define _PFS_LIB_QUEUE_PATCH_H_

#ifndef TAILQ_FOREACH_SAFE

#define TAILQ_FOREACH_SAFE(var, head, field, tvar)          \
    for ((var) = TAILQ_FIRST((head));               \
        (var) && ((tvar) = TAILQ_NEXT((var), field), 1);        \
        (var) = (tvar))

#endif

#endif /* _PFS_LIB_QUEUE_PATCH_H_ */
