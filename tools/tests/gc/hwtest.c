/*
 * hwtest.c -- a bare-metal GameCube program for the emulator's hardware (gctest dol): the video
 * (8 colour bars in the framebuffer, shown by the VI), the VI's display interrupt (a handler at
 * 0x500 counts them), the GX FIFO through the write-gather pipe (a "draw done" and a token the
 * PE must report). Its results at 0x80700000. Built with powerpc-linux-gnu-gcc, elf2dol.py.
 */
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
#define R32(a) (*(volatile u32 *) (a))
#define R16(a) (*(volatile u16 *) (a))
#define R8(a) (*(volatile u8 *) (a))

extern void irq_handler (void), irq_handler_end (void);
volatile u32 vi_count;

__asm__ (
	".globl _start\n_start:\n"
	"	lis 1, 0x8170\n"
	"	bl main\n"
	"1:	b 1b\n"
	/* the external interrupt: clear DI0's INT, count; r3 / r4 / CR kept in the SPRGs */
	".globl irq_handler\nirq_handler:\n"
	"	mtsprg 0, 3\n	mtsprg 1, 4\n	mfcr 3\n	mtsprg 2, 3\n"
	"	mfmsr 3\n	ori 3, 3, 0x10\n	mtmsr 3\n	isync\n"	/* (the data translation back on, as an OS does) */
	"	lis 3, 0xCC00\n	ori 3, 3, 0x2030\n	lhz 4, 0(3)\n	andi. 4, 4, 0x7FFF\n	sth 4, 0(3)\n"
	"	lis 3, vi_count@ha\n	lwz 4, vi_count@l(3)\n	addi 4, 4, 1\n	stw 4, vi_count@l(3)\n"
	"	mfsprg 3, 2\n	mtcr 3\n	mfsprg 3, 0\n	mfsprg 4, 1\n	rfi\n"
	".globl irq_handler_end\nirq_handler_end:\n");

static const u32 bars[8] = { 0xEB80EB80, 0xA28EA22C, 0x832C839C, 0x703A7048, 0x54C654B8, 0x41D44164, 0x237223D4, 0x10801080 };

int main (void)
{
	/* the handler at 0x80000500 */
	u32 *src = (u32 *) irq_handler, *dst = (u32 *) 0x80000500;
	while (src < (u32 *) irq_handler_end) *dst++ = *src++;
	/* the framebuffer at 0x00400000: 640 x 480, interlaced (the bottom field one line down) */
	for (int y = 0; y < 480; y++)
		for (int x = 0; x < 320; x++)
			R32 (0xC0400000 + (u32) (y * 1280 + x * 4)) = bars[x / 40];
	R16 (0xCC002000) = (240 << 4) | 6;			/* VTR: 240 lines a field */
	R32 (0xCC00201C) = 0x10000000 | (0x00400000 >> 5);	/* TFBL */
	R32 (0xCC002024) = 0x10000000 | ((0x00400000 + 1280) >> 5);	/* BFBL */
	R16 (0xCC002048) = (40 << 8) | 80;			/* HSW: 40 reads a line, a stride of 2 lines */
	R32 (0xCC002030) = 0x10000000 | (200 << 16);		/* DI0: at line 200, enabled */
	R16 (0xCC002002) = 1;					/* DCR: on, NTSC */
	R32 (0xCC003004) = 0x100;				/* PI: the VI interrupt */
	/* the GX FIFO at 0x00600000 (64 KB), linked */
	R32 (0xCC00300C) = 0x00600000; R32 (0xCC003010) = 0x00610000; R32 (0xCC003014) = 0x00600000;
	R16 (0xCC000020) = 0x0000; R16 (0xCC000022) = 0x0060; R16 (0xCC000024) = 0x0000; R16 (0xCC000026) = 0x0061;
	R16 (0xCC000034) = 0x0000; R16 (0xCC000036) = 0x0060; R16 (0xCC000038) = 0x0000; R16 (0xCC00003A) = 0x0060;
	R16 (0xCC001000 + 0x0A) = 3;				/* PE: token / finish interrupts on */
	R16 (0xCC000002) = 0x11;				/* CP: GP read on, linked */
	R8 (0xCC008000) = 0x61; R32 (0xCC008000) = 0x48001234;	/* a token (+ its interrupt) */
	R8 (0xCC008000) = 0x61; R32 (0xCC008000) = 0x45000002;	/* draw done */
	for (int i = 0; i < 22; i++) R8 (0xCC008000) = 0;	/* NOPs: a whole 32-byte burst */
	/* the interrupts on */
	u32 msr; __asm__ volatile ("mfmsr %0" : "=r" (msr)); msr |= 0x8000; __asm__ volatile ("mtmsr %0" : : "r" (msr));
	while (vi_count < 30) ;
	R32 (0x80700000) = vi_count;
	R32 (0x80700004) = R16 (0xCC00100A);			/* PE status: token 4, finish 8 */
	R32 (0x80700008) = R16 (0xCC00100E);			/* the token */
	R32 (0x8070000C) = R32 (0xCC003000);			/* PI causes */
	R32 (0x80700010) = 0x600D600D;
	for (;;) ;
}
