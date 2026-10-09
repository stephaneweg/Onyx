// cpu7.c -- the CPU test's ARM7 side: the C kernels in ARM and in Thumb.
#include "hw.h"
void ccheck_a7 (volatile u32 *out);
void ccheck_t7 (volatile u32 *out);
void *memcpy (void *d, const void *s, unsigned n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset (void *d, int c, unsigned n) { u8 *a = d; while (n--) *a++ = (u8) c; return d; }
int main (void)
{
	ccheck_a7 (RESULTS + 64);
	ccheck_t7 (RESULTS + 80);
	RESULTS[2] = DONE;
	return 0;
}
