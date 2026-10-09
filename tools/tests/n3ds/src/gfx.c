/*
 * gfx.c -- the 3DS core's second test program (phase T1): the services. It connects to srv:, asks for gsp::Gpu,
 * registers for the graphics interrupts, maps the shared page, draws a picture on both screens in framebuffers
 * of its own (linear memory), shows them, waits for VBlanks, has the GPU fill memory through the command queue;
 * and the kernel's timers and shared memory blocks. It checks itself ("<n> checks, 0 failed"); the pictures it
 * leaves are compared by tools/tests/run_n3ds_test.sh (n3ds/expect/gfx.crc).
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include "sys.h"

#define TICKS_PER_MS	268111u
#define TICKS_PER_FRAME	4481136u
#define FOREVER		(-1ll)
#define TOP_W		400
#define BOTTOM_W	320
#define SCREEN_H	240
#define SHARED_AT	0x10002000u

static Handle s_srv, s_gsp, s_irqEvent, s_sharedHandle;
static u8 *s_shared;
static u8 *s_fb[2];

/* ---- srv: ---- */
static Result getService (Handle *out, const char *name)
{
	u32 *cmd = ipcBuffer ();
	int n = 0; while (name[n]) n++;
	cmd[0] = IPC_HEADER (0x05, 4, 0);
	cmd[1] = 0; cmd[2] = 0;
	for (int i = 0; i < n && i < 8; i++) ((char *) &cmd[1])[i] = name[i];
	cmd[3] = (u32) n; cmd[4] = 0;
	Result r = svcSendSyncRequest (s_srv);
	if (r) return r;
	if (cmd[1] == 0) *out = cmd[3];
	return cmd[1];
}

static void testSrv (void)
{
	Handle h = 0;
	section ("srv:");
	check ("a port that does not exist is refused", svcConnectToPort (&h, "nope:") != 0);
	checkEq ("connect to srv:", svcConnectToPort (&s_srv, "srv:"), 0);
	u32 *cmd = ipcBuffer ();
	cmd[0] = IPC_HEADER (0x01, 0, 2); cmd[1] = 0x20; cmd[2] = 0;	/* RegisterClient (the process id, translated) */
	checkEq ("send a request", svcSendSyncRequest (s_srv), 0);
	checkEq ("RegisterClient", cmd[1], 0);
	checkEq ("... its answer's header", cmd[0], IPC_HEADER (0x01, 1, 0));
	check ("a service that does not exist is refused", getService (&h, "xyz:Q") != 0);
	checkEq ("gsp::Gpu", getService (&s_gsp, "gsp::Gpu"), 0);
	check ("a request on a handle that is no session is refused", svcSendSyncRequest (0x12345) == RES_INVALID_HANDLE);
}

/* ---- gsp::Gpu ---- */
static Result gspSimple (u32 header)
{
	u32 *cmd = ipcBuffer ();
	cmd[0] = header; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
	Result r = svcSendSyncRequest (s_gsp);
	return r ? r : cmd[1];
}

/* the next interrupt of the queue, -1 when it is empty */
static int popInterrupt (void)
{
	u8 *q = s_shared;
	if (!q[1]) return -1;
	int id = q[0xC + q[0]];
	q[0] = (u8) ((q[0] + 1) % 0x34); q[1]--;
	return id;
}
/* waits until the interrupt `want` comes; what else came is counted in seen[] */
static int s_seen[8];
static void waitInterrupt (int want)
{
	for (;;)
	{
		int id;
		while ((id = popInterrupt ()) >= 0) { if (id < 8) s_seen[id]++; if (id == want) return; }
		svcWaitSynchronization (s_irqEvent, FOREVER);
	}
}

static void pixel (int screen, int x, int y, u32 rgb)
{
	u8 *p = s_fb[screen] + (x * SCREEN_H + (SCREEN_H - 1 - y)) * 3;	/* columns, from the bottom; blue first */
	p[0] = (u8) rgb; p[1] = (u8) (rgb >> 8); p[2] = (u8) (rgb >> 16);
}
static void rect (int screen, int x0, int y0, int w, int h, u32 rgb)
{
	for (int x = x0; x < x0 + w; x++) for (int y = y0; y < y0 + h; y++) pixel (screen, x, y, rgb);
}
static void frame (int screen, int w, u32 rgb)
{
	for (int x = 0; x < w; x++) { pixel (screen, x, 0, rgb); pixel (screen, x, SCREEN_H - 1, rgb); }
	for (int y = 0; y < SCREEN_H; y++) { pixel (screen, 0, y, rgb); pixel (screen, w - 1, y, rgb); }
}

static void present (void)
{
	for (int sc = 0; sc < 2; sc++)
	{
		u8 *u = s_shared + 0x200 + sc * 0x40;
		u32 *info = (u32 *) (u + 4);
		info[0] = 0; info[1] = (u32) s_fb[sc]; info[2] = (u32) s_fb[sc]; info[3] = SCREEN_H * 3; info[4] = 1; info[5] = 0; info[6] = 0;
		u[0] = 0; u[1] = 1;
	}
}

static void testGsp (void)
{
	u32 *cmd = ipcBuffer ();
	u32 a = 0;
	section ("gsp::Gpu");
	cmd[0] = IPC_HEADER (0x16, 1, 2); cmd[1] = 0; cmd[2] = 0; cmd[3] = 0xFFFF8001;	/* AcquireRight */
	svcSendSyncRequest (s_gsp);
	checkEq ("AcquireRight", cmd[1], 0);
	svcCreateEvent (&s_irqEvent, 0);
	cmd[0] = IPC_HEADER (0x13, 1, 2); cmd[1] = 1; cmd[2] = 0; cmd[3] = s_irqEvent;	/* RegisterInterruptRelayQueue */
	checkEq ("register for the interrupts", svcSendSyncRequest (s_gsp), 0);
	checkEq ("... accepted", cmd[1], 0);
	checkEq ("... the thread's index", cmd[2], 0);
	s_sharedHandle = cmd[4];
	checkEq ("map the shared page", svcMapMemoryBlock (s_sharedHandle, SHARED_AT, MEMPERM_RW, 0x10000000), 0);
	s_shared = (u8 *) SHARED_AT;
	check ("... mapping it over itself is refused", svcMapMemoryBlock (s_sharedHandle, SHARED_AT, MEMPERM_RW, 0x10000000) != 0);

	checkEq ("linear memory for the top screen", svcControlMemory (&a, 0, 0, 0x47000, MEMOP_ALLOC | MEMOP_LINEAR, MEMPERM_RW), 0);
	s_fb[0] = (u8 *) a;
	checkEq ("... and the bottom one", svcControlMemory (&a, 0, 0, 0x39000, MEMOP_ALLOC | MEMOP_LINEAR, MEMPERM_RW), 0);
	s_fb[1] = (u8 *) a;

	/* the top screen: a gradient, colour bars, a white frame */
	for (int x = 0; x < TOP_W; x++) for (int y = 0; y < SCREEN_H; y++)
		pixel (0, x, y, (u32) (x * 255 / (TOP_W - 1)) << 16 | (u32) (y * 255 / (SCREEN_H - 1)) << 8 | 0x40);
	static const u32 BARS[8] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };
	for (int i = 0; i < 8; i++) rect (0, 40 + i * 40, 20, 40, 60, BARS[i]);
	frame (0, TOP_W, 0xFFFFFF);
	/* the bottom screen: a checkerboard */
	for (int x = 0; x < BOTTOM_W; x++) for (int y = 0; y < SCREEN_H; y++)
		pixel (1, x, y, ((x >> 4) + (y >> 4)) & 1 ? 0x303060 : 0xC0C0E0);
	present ();
	check ("the framebuffers are marked to be shown", s_shared[0x201] == 1 && s_shared[0x241] == 1);

	waitInterrupt (2);							/* PDC0: the top screen's VBlank */
	u64 t0 = svcGetSystemTick ();
	check ("at the VBlank the framebuffers are taken", s_shared[0x201] == 0 && s_shared[0x241] == 0);
	s_seen[2] = s_seen[3] = 0;
	waitInterrupt (2); waitInterrupt (2);
	u32 dt = (u32) (svcGetSystemTick () - t0);
	check ("two VBlanks later, two frames have passed", dt > TICKS_PER_FRAME * 2 - TICKS_PER_FRAME / 4 && dt < TICKS_PER_FRAME * 2 + TICKS_PER_FRAME / 4);
	check ("the bottom screen's VBlank comes too", s_seen[3] >= 1);

	/* the GPU fills memory: the bottom screen's columns 100 to 219, in orange (24 bits a pixel) */
	u8 *q = s_shared + 0x800;
	u32 *c = (u32 *) (q + 0x20 + ((q[0] + q[1]) % 15) * 0x20);
	c[0] = 2;
	c[1] = (u32) s_fb[1] + 100 * SCREEN_H * 3; c[2] = 0xFF8000; c[3] = (u32) s_fb[1] + 220 * SCREEN_H * 3;
	c[4] = 0; c[5] = 0; c[6] = 0; c[7] = 0x0101;				/* start, 24 bits */
	q[1]++;
	s_seen[0] = 0;
	checkEq ("give the GPU its command queue", gspSimple (IPC_HEADER (0x0C, 0, 0)), 0);
	checkEq ("... the queue is emptied", q[1], 0);
	waitInterrupt (0);							/* PSC0: the fill is done */
	check ("the fill's interrupt came", s_seen[0] == 1);
	u8 *p = s_fb[1] + 150 * SCREEN_H * 3;
	check ("the memory is filled", p[0] == 0x00 && p[1] == 0x80 && p[2] == 0xFF && p[SCREEN_H * 3 - 1] == 0xFF);
	check ("... not past its end", s_fb[1][220 * SCREEN_H * 3 + 1] != 0x80);
	check ("... nor before its start", s_fb[1][100 * SCREEN_H * 3 - 2] != 0x80);
	rect (1, 120, 100, 80, 40, 0x2020A0);					/* a blue box on the orange */
	frame (1, BOTTOM_W, 0xFFFFFF);
	present ();
	waitInterrupt (2);
	check ("a command the service does not know is answered", gspSimple (IPC_HEADER (0x7E, 0, 0)) == 0);
}

/* ---- timers, shared memory blocks ---- */
static void testTimers (void)
{
	Handle tm = 0;
	section ("timers");
	checkEq ("a timer", svcCreateTimer (&tm, 0), 0);
	check ("not set: no wait ends", svcWaitSynchronization (tm, 0) == RES_TIMEOUT);
	u64 t0 = svcGetSystemTick ();
	checkEq ("set it for 3 ms", svcSetTimer (tm, 3 * NS_MS, 0), 0);
	checkEq ("wait for it", svcWaitSynchronization (tm, 100 * NS_MS), 0);
	u32 dt = (u32) (svcGetSystemTick () - t0);
	check ("... 3 ms later", dt >= 3 * TICKS_PER_MS && dt < 5 * TICKS_PER_MS);
	check ("... once", svcWaitSynchronization (tm, 5 * NS_MS) == RES_TIMEOUT);
	t0 = svcGetSystemTick ();
	checkEq ("every 2 ms", svcSetTimer (tm, 2 * NS_MS, 2 * NS_MS), 0);
	for (int i = 0; i < 4; i++) svcWaitSynchronization (tm, FOREVER);
	dt = (u32) (svcGetSystemTick () - t0);
	check ("... four times in 8 ms", dt >= 8 * TICKS_PER_MS && dt < 10 * TICKS_PER_MS);
	checkEq ("cancel it", svcCancelTimer (tm), 0);
	svcClearTimer (tm);
	check ("... it no longer comes", svcWaitSynchronization (tm, 5 * NS_MS) == RES_TIMEOUT);
	checkEq ("close it", svcCloseHandle (tm), 0);

	Handle blk = 0;
	section ("shared memory");
	checkEq ("a memory block", svcCreateMemoryBlock (&blk, 0, 0x2000, MEMPERM_RW, MEMPERM_RW), 0);
	checkEq ("map it", svcMapMemoryBlock (blk, 0x10100000, MEMPERM_RW, MEMPERM_RW), 0);
	checkEq ("... and again elsewhere", svcMapMemoryBlock (blk, 0x10200000, MEMPERM_RW, MEMPERM_RW), 0);
	volatile u32 *one = (volatile u32 *) 0x10101FFC, *two = (volatile u32 *) 0x10201FFC;	/* (volatile: the compiler */
	*one = 0xFEEDBEEF;								/* cannot know they are one word) */
	checkEq ("both show the same memory", *two, 0xFEEDBEEF);
	checkEq ("unmap one", svcUnmapMemoryBlock (blk, 0x10200000), 0);
	checkEq ("... the other stays", *one, 0xFEEDBEEF);
	check ("unmapping what is not it is refused", svcUnmapMemoryBlock (blk, 0x08000000) != 0);
	svcUnmapMemoryBlock (blk, 0x10100000);
	svcCloseHandle (blk);
}

int main (void)
{
	print ("3DS core: services test\n");
	testSrv ();
	testGsp ();
	testTimers ();
	summary ();
	return 0;
}
