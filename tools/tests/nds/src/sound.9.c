// sound.9.c -- the ARM9 of the sound test: nothing to do.
#include "hw.h"
int main (void) { for (;;) asm volatile ("mcr p15, 0, r0, c7, c0, 4"); return 0; }
