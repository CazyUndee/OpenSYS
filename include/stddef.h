/*
 * stddef.h - Standard definitions for x86_64
 */

#ifndef _STDDEF_H
#define _STDDEF_H

#define NULL ((void*)0)

typedef unsigned long size_t;
typedef long ptrdiff_t;

/* Offset macro */
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
