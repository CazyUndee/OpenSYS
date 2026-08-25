/*
 * stddef.h - Standard definitions for x86_64
 * 
 * Safe for host compilation: when building tests on the host,
 * the compiler already provides these types via stddef.h.
 * We only define them when compiling for the freestanding kernel.
 */

#ifndef _PLAN0_STDDEF_H
#define _PLAN0_STDDEF_H

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
/* Hosted compiler (tests, CI) — use the compiler-provided stddef */
#include_next <stddef.h>
#else
/* Freestanding kernel — provide our own definitions */
#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
typedef long ptrdiff_t;
#endif

#endif /* __STDC_HOSTED__ */

/* Offset macro — always safe */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#endif /* _PLAN0_STDDEF_H */
