// systest.9.c -- the system test's ARM9 side: interrupts through the BIOS, IntrWait, the timers, the DMA,
// the divider / square root, the IPC with the ARM7, the cartridge's reads (polled and by DMA), the chip ID,
// the save chip (a Flash page written and read back), the BIOS calls. RESULTS[3 + i]: 1 passed, else the value.
#include "hw.h"

#define DTCM_IRQ_HANDLER	R32 (0x03003FFC)
#define DTCM_IRQ_FLAGS		R32 (0x03003FF8)
#define AUXSPICNT	R16 (0x040001A0)
#define AUXSPIDATA	R8 (0x040001A2)
#define ROMCTRL		R32 (0x040001A4)
#define CARDCMD(i)	R8 (0x040001A8 + (i))
#define CARDDATA	R32 (0x04100010)

static volatile u32 vblanks, timers, fifoIrqs, syncIrqs, cardIrqs;
static int item = 3;
static void check (int ok, u32 val) { RESULTS[item++] = ok ? 1 : (val ? val : 0xBAD); }

__attribute__ ((target ("arm"))) void irq (void)
{
	u32 f = IE & IF;
	IF = f;
	if (f & 1) vblanks++;
	if (f & 8) timers++;
	if (f & (1 << 18)) { while (!(IPCFIFOCNT & 0x100)) { (void) IPCFIFORECV; fifoIrqs++; } }
	if (f & (1 << 16)) syncIrqs++;
	if (f & (1 << 19)) cardIrqs++;
	DTCM_IRQ_FLAGS |= f;
}

static inline u32 swi_div (s32 a, s32 b) { register u32 r0 asm ("r0") = (u32) a, r1 asm ("r1") = (u32) b; asm volatile ("swi #0x090000" : "+r" (r0), "+r" (r1) :: "r2", "r3", "memory"); return r0; }
static inline u32 swi_sqrt (u32 a) { register u32 r0 asm ("r0") = a; asm volatile ("swi #0x0D0000" : "+r" (r0) :: "r1", "r2", "r3", "memory"); return r0; }
static inline void swi_vblank (void) { asm volatile ("swi #0x050000" ::: "r0", "r1", "r2", "r3", "memory"); }
static inline void swi_intrwait (u32 discard, u32 mask) { register u32 r0 asm ("r0") = discard, r1 asm ("r1") = mask; asm volatile ("swi #0x040000" : "+r" (r0), "+r" (r1) :: "r2", "r3", "memory"); }
static inline void swi_cpuset (const void *s, void *d, u32 c) { register u32 r0 asm ("r0") = (u32) s, r1 asm ("r1") = (u32) d, r2 asm ("r2") = c; asm volatile ("swi #0x0B0000" : "+r" (r0), "+r" (r1), "+r" (r2) :: "r3", "memory"); }
static inline void swi_lz77 (const void *s, void *d) { register u32 r0 asm ("r0") = (u32) s, r1 asm ("r1") = (u32) d; asm volatile ("swi #0x110000" : "+r" (r0), "+r" (r1) :: "r2", "r3", "memory"); }
static inline u32 swi_crc16 (u32 crc, const void *p, u32 n) { register u32 r0 asm ("r0") = crc, r1 asm ("r1") = (u32) p, r2 asm ("r2") = n; asm volatile ("swi #0x0E0000" : "+r" (r0), "+r" (r1), "+r" (r2) :: "r3", "memory"); return r0; }

static u8 blob (u32 i) { return (u8) ((i * 7 + (i >> 8) * 13) & 0xFF); }

static void cardCmd (u8 c0, u32 addr)
{
	CARDCMD (0) = c0; CARDCMD (1) = (u8) (addr >> 24); CARDCMD (2) = (u8) (addr >> 16); CARDCMD (3) = (u8) (addr >> 8);
	CARDCMD (4) = (u8) addr; CARDCMD (5) = 0; CARDCMD (6) = 0; CARDCMD (7) = 0;
}

static u8 spi (u8 v) { AUXSPIDATA = v; while (AUXSPICNT & 0x80) ; return AUXSPIDATA; }

static u32 buf[0x200 / 4];
static const u8 lz[] = { 0x10, 12, 0, 0, 0x00, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 0x80, 0x00, 0x03, 'Z', 0 };

int main (void)
{
	DTCM_IRQ_HANDLER = (u32) irq;
	IME = 0;
	IE = 1 | 8 | (1 << 16) | (1 << 18) | (1 << 19);
	IF = ~0u;
	DISPSTAT = 8;							// the VBlank interrupt
	IPCSYNC = 0x4000;						// the sync interrupt
	IPCFIFOCNT = 0x8000 | 0x4000 | 8 | 0x400;			// on, errors acknowledged, cleared, recv IRQ
	IME = 1;
	asm volatile ("mrs r0, cpsr; bic r0, r0, #0x80; msr cpsr_c, r0" ::: "r0");
	// 0: VBlank through VBlankIntrWait
	swi_vblank (); u32 v0 = vblanks; swi_vblank (); swi_vblank ();
	check (vblanks - v0 == 2 && VCOUNT >= 192 && VCOUNT < 194, (vblanks - v0) | (VCOUNT << 16));
	// 1: a timer's interrupt through IntrWait
	TM_CNT_L (0) = 0x10000 - 1000; TM_CNT_H (0) = 0xC0;		// prescaler 1, IRQ, on
	swi_intrwait (1, 8); swi_intrwait (1, 8);
	TM_CNT_H (0) = 0;
	check (timers >= 2, timers);
	// 2: a timer counts (prescaler 64: ~ 1 a 2 microseconds); cascade
	TM_CNT_L (1) = 0; TM_CNT_H (1) = 0x81; TM_CNT_L (2) = 0; TM_CNT_H (2) = 0x84;
	u32 t0 = TM_CNT_L (1);
	for (volatile int i = 0; i < 2000; i++) ;
	u32 t1 = TM_CNT_L (1);
	check (t1 > t0 && t1 - t0 < 5000, t1 - t0);
	TM_CNT_H (1) = 0; TM_CNT_H (2) = 0;
	// 3: DMA copies (32 and 16 bits) and the fill
	static u32 src[64], dst[64];
	for (int i = 0; i < 64; i++) { src[i] = (u32) i * 0x01010101u; dst[i] = 0; }
	DMA_SAD (3) = (u32) src; DMA_DAD (3) = (u32) dst; DMA_CNT (3) = 0x84000000 | 64;
	int ok = 1; for (int i = 0; i < 64; i++) if (dst[i] != src[i]) ok = 0;
	R32 (0x040000EC) = 0x5A5A1234;
	DMA_SAD (3) = 0x040000EC; DMA_DAD (3) = (u32) dst; DMA_CNT (3) = 0x85000000 | 32;
	for (int i = 0; i < 32; i++) if (dst[i] != 0x5A5A1234) ok = 0;
	DMA_SAD (3) = (u32) src; DMA_DAD (3) = (u32) dst; DMA_CNT (3) = 0x80000000 | 8;	// 16-bit, 8 halfwords
	for (int i = 0; i < 4; i++) if (dst[i] != src[i]) ok = 0;
	if (dst[4] != 0x5A5A1234) ok = 0;
	check (ok, 0);
	// 4: the divider and the square root
	R16 (0x04000280) = 0; R32 (0x04000290) = (u32) -1000; R32 (0x04000298) = 7;
	while (R16 (0x04000280) & 0x8000) ;
	ok = (s32) R32 (0x040002A0) == -142 && (s32) R32 (0x040002A8) == -6;
	R16 (0x04000280) = 2; R32 (0x04000290) = 0; R32 (0x04000294) = 1; R32 (0x04000298) = 3; R32 (0x0400029C) = 0;
	if (R32 (0x040002A0) != 0x55555555 || R32 (0x040002A8) != 1) ok = 0;
	R16 (0x040002B0) = 1; R32 (0x040002B8) = 0; R32 (0x040002BC) = 1;	// sqrt (2^32) = 65536
	if (R32 (0x040002B4) != 65536) ok = 0;
	check (ok, R32 (0x040002A0));
	// 5: the IPC: the ARM7 sends back each word + 1 (received by the FIFO's interrupt)
	u32 before = fifoIrqs;
	for (u32 i = 0; i < 8; i++) IPCFIFOSEND = 100 + i;
	for (int i = 0; i < 20; i++) swi_vblank ();
	check (fifoIrqs - before == 8, fifoIrqs - before);
	// 6: the ARM7 raised the sync interrupt
	check (syncIrqs >= 1 && (IPCSYNC & 15) == 5, syncIrqs | ((u32) (IPCSYNC & 15) << 8));
	// 7: the cartridge: 0x200 bytes at 0x10000, polled
	AUXSPICNT = 0xC000;					// the slot on, ROM mode, transfer IRQ
	cardCmd (0xB7, 0x10000);
	ROMCTRL = 0xA1586000;					// start, 0x200 bytes
	int n = 0; ok = 1;
	while (ROMCTRL & 0x80000000) { if (ROMCTRL & 0x00800000) { u32 w = CARDDATA; if (n < 128) buf[n] = w; n++; } }
	for (int i = 0; i < 0x200; i++) if (((u8 *) buf)[i] != blob (i)) ok = 0;
	check (ok && n == 128, n);
	// 8: the same by DMA (timing: the card), at 0x10200, with the transfer-done interrupt
	for (int i = 0; i < 128; i++) buf[i] = 0;
	u32 ci = cardIrqs;
	DMA_SAD (0) = 0x04100010; DMA_DAD (0) = (u32) buf; DMA_CNT (0) = 0x80000000 | (5 << 27) | (1 << 26) | (2 << 23) | 128;
	cardCmd (0xB7, 0x10200);
	ROMCTRL = 0xA1586000;
	while (ROMCTRL & 0x80000000) ;
	ok = 1; for (int i = 0; i < 0x200; i++) if (((u8 *) buf)[i] != blob (0x200 + i)) ok = 0;
	check (ok && cardIrqs > ci, cardIrqs - ci);
	// 9: the chip ID
	cardCmd (0xB8, 0); ROMCTRL = 0xA7586000;
	u32 id = 0; while (ROMCTRL & 0x80000000) if (ROMCTRL & 0x00800000) id = CARDDATA;
	check (id == R32 (0x027FF800) && (id & 0xFF) == 0xC2, id);
	// 10: the save: a Flash page written, then read back
	AUXSPICNT = 0xA040;					// the slot on, SPI mode, chip select held
	spi (0x06); AUXSPICNT = 0xA000; spi (0);		// write enable
	AUXSPICNT = 0xA040; spi (0x0A); spi (0); spi (1); spi (0);
	for (int i = 0; i < 255; i++) spi ((u8) (i ^ 0x5A));
	AUXSPICNT = 0xA000; spi (255 ^ 0x5A);
	AUXSPICNT = 0xA040; spi (0x03); spi (0); spi (1); spi (0x10);
	ok = 1; for (int i = 0x10; i < 0x1F; i++) if (spi (0) != (u8) (i ^ 0x5A)) ok = 0;
	AUXSPICNT = 0xA000; spi (0);
	check (ok, 0);
	// 11: the BIOS: Div, Sqrt, CpuSet, LZ77, CRC16
	ok = swi_div (-100, 7) == (u32) -14 && swi_sqrt (1000000) == 1000;
	static u32 cs[16];
	swi_cpuset (src, cs, 16 | (1 << 26));
	for (int i = 0; i < 16; i++) if (cs[i] != src[i]) ok = 0;
	static u8 out[16];
	swi_lz77 (lz, out);
	const char *want = "ABCDEFGHEFGZ";
	for (int i = 0; i < 12; i++) if (out[i] != (u8) want[i]) ok = 0;
	static const u16 digits[4] __attribute__ ((aligned (4))) = { 0x3231, 0x3433, 0x3635, 0x3837 };	// "12345678"
	u32 crc = swi_crc16 (0xFFFF, digits, 8);
	if (crc != 0x37DD) ok = 0;	// (CRC-16/MODBUS of "12345678")
	check (ok, out[8] | (out[9] << 8) | (crc << 16));
	// the ARM7's results came through the main memory; done
	while (RESULTS[2] != DONE) ;
	RESULTS[1] = (u32) item;
	RESULTS[0] = DONE;
	for (;;) swi_vblank ();
	return 0;
}
