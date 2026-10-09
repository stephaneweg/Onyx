// bench.9.c -- the processors' speed: the C kernels over and over on the ARM9 (ARM and Thumb in turns),
// the count of rounds in RESULTS[4] (the ARM7's in RESULTS[5]).
#include "hw.h"
void ccheck_a9 (volatile u32 *out);
void ccheck_t9 (volatile u32 *out);
void *memcpy (void *d, const void *s, unsigned n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset (void *d, int c, unsigned n) { u8 *a = d; while (n--) *a++ = (u8) c; return d; }
int main (void) { RESULTS[0] = DONE; for (;;) { ccheck_a9 (RESULTS + 16); ccheck_t9 (RESULTS + 32); RESULTS[4]++; } return 0; }
