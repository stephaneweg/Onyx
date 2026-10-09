// ccheck_main.c -- the PC reference of ccheck.c: prints its 12 words.
#include <stdio.h>
#include <stdint.h>
void ccheck (volatile uint32_t *out);
int main (void) { volatile uint32_t o[16] = { 0 }; ccheck (o); for (int i = 0; i < 12; i++) printf ("%08x\n", o[i]); return 0; }
