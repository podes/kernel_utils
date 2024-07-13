#ifndef _PFS_LIB_BUG_H_
#define _PFS_LIB_BUG_H_

#include "compiler.h"
#include <stdio.h>
#include <stdlib.h>

#define __WARN_printf(arg...)   do { fprintf(stderr, arg); } while (0)

#define WARN(condition, format...) ({       \
    int __ret_warn_on = !!(condition);  \
    if (unlikely(__ret_warn_on))        \
        __WARN_printf(format);      \
    unlikely(__ret_warn_on);        \
})

#define WARN_ON(condition) ({                   \
    int __ret_warn_on = !!(condition);          \
    if (unlikely(__ret_warn_on))                \
        __WARN_printf("assertion failed at %s:%d\n",    \
                __FILE__, __LINE__);        \
    unlikely(__ret_warn_on);                \
})

#define WARN_ON_ONCE(condition) ({          \
    static int __warned;                \
    int __ret_warn_once = !!(condition);        \
                            \
    if (unlikely(__ret_warn_once && !__warned)) {   \
        __warned = true;            \
        WARN_ON(1);             \
    }                       \
    unlikely(__ret_warn_once);          \
})

#define WARN_ONCE(condition, format...) ({  \
    static int __warned;            \
    int __ret_warn_once = !!(condition);    \
                        \
    if (unlikely(__ret_warn_once))      \
        if (WARN(!__warned, format))    \
            __warned = 1;       \
    unlikely(__ret_warn_once);      \
})

#define BUG() do { \
    fprintf(stderr, "BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
    abort(); \
} while (0)

#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while (0)

#endif /* _PFS_LIB_BUG_H_ */
