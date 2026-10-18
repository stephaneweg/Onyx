// seams.7.c -- the ARM7 of the seams test: nothing to do (waits)
#include "hw.h"
int main (void) { for (;;) asm volatile ("swi #0x060000"); return 0; }
