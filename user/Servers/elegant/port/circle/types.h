// (Elegant's stand-in for Circle's types.h: the window manager's sources are the kernel's, built for a user process)
#ifndef _circle_types_h
#define _circle_types_h
#include <stddef.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long u64;
typedef signed char s8; typedef signed short s16; typedef signed int s32; typedef signed long s64;
typedef long intptr; typedef unsigned long uintptr;
typedef bool boolean;
#define FALSE false
#define TRUE true
#endif
