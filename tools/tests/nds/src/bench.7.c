// bench.7.c -- the ARM7 of the speed test: the C kernels in Thumb, over and over.
#include "hw.h"
void ccheck_t7 (volatile u32 *out);
void *memcpy (void *d, const void *s, unsigned n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset (void *d, int c, unsigned n) { u8 *a = d; while (n--) *a++ = (u8) c; return d; }
int main (void) { for (;;) { ccheck_t7 (RESULTS + 64); RESULTS[5]++; } return 0; }
