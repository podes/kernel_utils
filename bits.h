#ifndef BITS_6471bf20_H
#define BITS_6471bf20_H

#ifndef BITS_PER_BYTE
#define BITS_PER_BYTE       8
#endif
#ifndef BITS_PER_LONG
#define BITS_PER_LONG       (8 * sizeof(long))
#endif
#ifndef BITS_PER_LONG_LONG
#define BITS_PER_LONG_LONG  (8 * sizeof(long long))
#endif
#ifndef BIT_ULL
#define BIT_ULL(nr)         (1ULL << (nr))
#endif
#ifndef BIT_MASK
#define BIT_MASK(nr)        (1UL << ((nr) % BITS_PER_LONG))
#endif
#ifndef BIT_WORD
#define BIT_WORD(nr)        ((nr) / BITS_PER_LONG)
#endif
#ifndef BIT_ULL_MASK
#define BIT_ULL_MASK(nr)    (1ULL << ((nr) % BITS_PER_LONG_LONG))
#endif
#ifndef BIT_ULL_WORD
#define BIT_ULL_WORD(nr)    ((nr) / BITS_PER_LONG_LONG)
#endif

#endif
