#ifndef _STDDEF_H
#define _STDDEF_H

#define NULL ((void*)0)

/* Use compiler built-in types for freestanding environment */
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

#endif
