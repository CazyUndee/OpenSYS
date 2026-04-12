#ifndef _STDDEF_H
#define _STDDEF_H

#define NULL ((void*)0)

#ifndef __SIZE_TYPE__
typedef unsigned int size_t;
#endif

typedef signed int ptrdiff_t;

#endif
