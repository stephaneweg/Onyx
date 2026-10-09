// hw.h -- the registers and helpers the core's test programs use (no library).
#ifndef HW_H
#define HW_H
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
typedef signed char s8; typedef short s16; typedef int s32; typedef long long s64;
#define R8(a)	(*(volatile u8 *) (a))
#define R16(a)	(*(volatile u16 *) (a))
#define R32(a)	(*(volatile u32 *) (a))
#define DISPCNT		R32 (0x04000000)
#define DISPSTAT	R16 (0x04000004)
#define VCOUNT		R16 (0x04000006)
#define BGCNT(n)	R16 (0x04000008 + (n) * 2)
#define BGHOFS(n)	R16 (0x04000010 + (n) * 4)
#define BGVOFS(n)	R16 (0x04000012 + (n) * 4)
#define DISPCNT_B	R32 (0x04001000)
#define BGCNT_B(n)	R16 (0x04001008 + (n) * 2)
#define POWCNT1		R16 (0x04000304)
#define VRAMCNT(n)	R8 (0x04000240 + (n))
#define WRAMCNT		R8 (0x04000247)
#define VRAMCNT_H	R8 (0x04000248)
#define VRAMCNT_I	R8 (0x04000249)
#define IME		R32 (0x04000208)
#define IE		R32 (0x04000210)
#define IF		R32 (0x04000214)
#define IPCSYNC		R16 (0x04000180)
#define IPCFIFOCNT	R16 (0x04000184)
#define IPCFIFOSEND	R32 (0x04000188)
#define IPCFIFORECV	R32 (0x04100000)
#define TM_CNT_L(n)	R16 (0x04000100 + (n) * 4)
#define TM_CNT_H(n)	R16 (0x04000102 + (n) * 4)
#define DMA_SAD(n)	R32 (0x040000B0 + (n) * 12)
#define DMA_DAD(n)	R32 (0x040000B4 + (n) * 12)
#define DMA_CNT(n)	R32 (0x040000B8 + (n) * 12)
#define KEYINPUT	R16 (0x04000130)
#define PAL		((volatile u16 *) 0x05000000)
#define PAL_B		((volatile u16 *) 0x05000400)
#define OAM		((volatile u16 *) 0x07000000)
#define VRAM_A		((volatile u16 *) 0x06800000)
#define BG_VRAM		((volatile u16 *) 0x06000000)
#define BG_VRAM_B	((volatile u16 *) 0x06200000)
#define OBJ_VRAM	((volatile u16 *) 0x06400000)
#define RESULTS		((volatile u32 *) 0x02300000)	// [0] done magic, [1] passed, [2] failed, [3...] values
#define DONE		0x7E57D0E5
#define RGB(r, g, b)	((u16) ((r) | ((g) << 5) | ((b) << 10)))
static inline void irq_off (void) {}
#endif
