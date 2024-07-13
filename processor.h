#ifndef PFS_LIB_PROCESSOR_H
#define PFS_LIB_PROCESSOR_H

#include "compiler.h"

#if defined(__x86_64__) || defined(__i386__)

/* REP NOP (PAUSE) is a good thing to insert into busy-wait loops. */
static __always_inline void rep_nop(void)
{
    __asm__ __volatile__("rep;nop": : :"memory");
}

static __always_inline void cpu_relax(void)
{
    rep_nop();
}
#else

#error "unknown cpu"

#endif

#endif /* PFS_LIB_PROCESSOR_H */
