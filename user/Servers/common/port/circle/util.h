// (Elegant's stand-in for Circle's util.h: the memory primitives, AppKit's at link time)
#ifndef _circle_util_h
#define _circle_util_h
#include <stddef.h>
extern "C" {
void *memset (void *dst, int c, size_t n);
void *memcpy (void *dst, const void *src, size_t n);
void *memmove (void *dst, const void *src, size_t n);
}
#endif
