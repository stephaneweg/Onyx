// gfx2d.7.c -- the ARM7 of the picture tests: nothing to do.
#include "hw.h"
int main (void) { for (;;) asm volatile ("swi #0x060000"); return 0; }
