// sound.7.c -- the sound test (the ARM7): channel 8 a PSG square of 440 Hz (duty 4/8) on the left, channel 0
// a 16-bit PCM sine of 1000 Hz looped on the right, channel 14 the noise quietly in the middle; the master on.
#include "hw.h"
#define SCNT(c)	R32 (0x04000400 + (c) * 16)
#define SSAD(c)	R32 (0x04000404 + (c) * 16)
#define STMR(c)	R16 (0x04000408 + (c) * 16)
#define SPNT(c)	R16 (0x0400040A + (c) * 16)
#define SLEN(c)	R32 (0x0400040C + (c) * 16)
static s16 wave[32];
int main (void)
{
	// a sine by a rotation (no tables): 32 samples a period
	s32 x = 30000 << 8, y = 0;
	for (int i = 0; i < 32; i++) { wave[i] = (s16) (y >> 8); s32 nx = x - (s32) ((s64) y * 6393 >> 15) - (s32) ((s64) x * 625 >> 15), ny = y + (s32) ((s64) x * 6393 >> 15) - (s32) ((s64) y * 625 >> 15); x = nx; y = ny; }
	R16 (0x04000304) = 1;					// POWCNT2: the speakers
	R16 (0x04000500) = 0x8000 | 127;			// the master
	// PSG: the frequency is 33513982 / 2 / (0x10000 - tmr) / 8
	STMR (8) = (u16) (0x10000 - 33513982 / 2 / 8 / 440);
	SCNT (8) = 0x80000000u | (3u << 29) | (3u << 24) | (0 << 16) | 100;
	// PCM16: 32 samples a period of 1000 Hz: 32000 samples a second
	SSAD (0) = (u32) wave; SPNT (0) = 0; SLEN (0) = 16;
	STMR (0) = (u16) (0x10000 - 33513982 / 2 / 32000);
	SCNT (0) = 0x80000000u | (1u << 29) | (1u << 27) | (127 << 16) | 100;
	STMR (14) = (u16) (0x10000 - 33513982 / 2 / 8000);
	SCNT (14) = 0x80000000u | (3u << 29) | (64 << 16) | 8;
	RESULTS[0] = DONE;
	for (;;) asm volatile ("swi #0x060000");
	return 0;
}
