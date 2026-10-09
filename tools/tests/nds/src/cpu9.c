// cpu9.c -- the CPU test's ARM9 side: the C kernels in ARM and in Thumb, the ARMv5 checks.
#include "hw.h"
void ccheck_a9 (volatile u32 *out);
void ccheck_t9 (volatile u32 *out);
void v5tests (volatile u32 *out);
void *memcpy (void *d, const void *s, unsigned n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset (void *d, int c, unsigned n) { u8 *a = d; while (n--) *a++ = (u8) c; return d; }
int main (void)
{
	ccheck_a9 (RESULTS + 16);
	ccheck_t9 (RESULTS + 32);
	v5tests (RESULTS + 48);
	RESULTS[1] = DONE;
	while (RESULTS[2] != DONE) ;
	RESULTS[0] = DONE;
	return 0;
}
