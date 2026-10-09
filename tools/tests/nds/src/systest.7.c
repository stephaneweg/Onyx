// systest.7.c -- the system test's ARM7 side: echoes the ARM9's FIFO words + 1 (its own FIFO interrupt),
// raises the ARM9's sync interrupt, reads the firmware (the user's name), the touch screen and the clock.
// RESULTS[32 + i]: what it read.
#include "hw.h"
#define IRQ_HANDLER	R32 (0x0380FFFC)
#define IRQ_FLAGS	R32 (0x0380FFF8)
#define SPICNT		R16 (0x040001C0)
#define SPIDATA		R8 (0x040001C2)
#define RTC		R8 (0x04000138)

static volatile u32 echoed;
__attribute__ ((target ("arm"))) void irq (void)
{
	u32 f = IE & IF;
	IF = f;
	if (f & (1 << 18)) while (!(IPCFIFOCNT & 0x100)) { u32 v = IPCFIFORECV; IPCFIFOSEND = v + 1; echoed++; }
	IRQ_FLAGS |= f;
}
static inline void swi_vblank (void) { asm volatile ("swi #0x050000" ::: "r0", "r1", "r2", "r3", "memory"); }

static u8 spi (u8 v, int dev, int hold)
{
	SPICNT = (u16) (0x8000 | (dev << 8) | (hold ? 0x800 : 0));
	SPIDATA = v;
	while (SPICNT & 0x80) ;
	return SPIDATA;
}

static void rtcBit (int b) { RTC = (u8) (0x10 | 0x04 | (b & 1)); RTC = (u8) (0x10 | 0x04 | 0x02 | (b & 1)); }
static int rtcIn (void) { RTC = 0x04; RTC = 0x06; return RTC & 1; }

int main (void)
{
	IRQ_HANDLER = (u32) irq;
	IE = (1 << 18) | 1;
	IF = ~0u;
	DISPSTAT = 8;
	IPCFIFOCNT = 0x8000 | 0x4000 | 8 | 0x400;
	IME = 1;
	asm volatile ("mrs r0, cpsr; bic r0, r0, #0x80; msr cpsr_c, r0" ::: "r0");
	// the sync: our output 5, and the ARM9's interrupt
	for (int i = 0; i < 3; i++) swi_vblank ();
	IPCSYNC = 0x2000 | 0x500;
	// the firmware: the user's name (UTF-16) at 0x3FE06
	spi (0x03, 1, 1); spi (0x03, 1, 1); spi (0xFE, 1, 1); spi (0x06, 1, 1);
	u32 nm = 0;
	for (int i = 0; i < 4; i++) { u8 lo = spi (0, 1, 1); spi (0, 1, i < 3); nm |= (u32) lo << (i * 8); }
	RESULTS[32] = nm;
	// the touch screen: X (channel 5) and Y (channel 1), 12 bits
	spi (0xD4, 2, 1); u32 xh = spi (0, 2, 1), xl = spi (0, 2, 0);
	spi (0x94, 2, 1); u32 yh = spi (0, 2, 1), yl = spi (0, 2, 0);
	RESULTS[33] = (((xh << 8) | xl) >> 3) | ((((yh << 8) | yl) >> 3) << 16);
	RESULTS[34] = R16 (0x04000136);
	// the clock: the date and time (command 0x65: 0110 010 1, sent bit 0 first)
	RTC = 0x04 | 0x02; RTC = 0x10 | 0x04 | 0x02;
	for (int i = 0; i < 8; i++) rtcBit ((0x65 >> i) & 1);
	u32 dt[7];
	for (int b = 0; b < 7; b++) { u32 v = 0; for (int i = 0; i < 8; i++) v |= (u32) rtcIn () << i; dt[b] = v; }
	RTC = 0;
	RESULTS[35] = dt[0] | (dt[1] << 8) | (dt[2] << 16);
	RESULTS[36] = dt[4] | (dt[5] << 8) | (dt[6] << 16);
	// the echo went on in the interrupt
	for (int i = 0; i < 25; i++) swi_vblank ();
	RESULTS[37] = echoed;
	RESULTS[2] = DONE;
	for (;;) swi_vblank ();
	return 0;
}
