//
// n3ds/n3ds_gsp.cpp -- gsp::Gpu, the graphics service, and the two screens. A program registers an event and gets
// a page of shared memory that holds, for its thread: the queue of the interrupts it is told (PSC0 / PSC1: a
// memory fill done; PDC0 / PDC1: the top / bottom screen's VBlank; PPF: a transfer done; P3D: a command list
// done), the framebuffers it wants shown from the next VBlank, and the queue of the commands it gives the GPU
// (GX: memory fills, display transfers, command lists).
//
// This slice: the framebuffers a program draws itself (the picture of a screen is read from the program's
// memory: a framebuffer is the screen turned a quarter, column by column from the bottom), memory fills, DMA.
// The PICA200's command lists and the display transfers that show what it rendered come with phase T2: they
// are counted and their interrupt is given, nothing is drawn.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "n3ds/n3ds.h"

namespace n3ds {

// The shared page's layout (one program thread: its index is 0).
enum
{
	SHM_SIZE      = 0x1000,
	SHM_IRQ       = 0x000,			// u8 first, u8 count, u8 error, -, 8 bytes, then the interrupts' numbers
	SHM_IRQ_SLOTS = 0x34,
	SHM_FB        = 0x200,			// per screen (0x40): u8 index, u8 dirty, -, -, two framebuffer infos of 0x1C
	SHM_CMD       = 0x800,			// u8 first, u8 count, -, ...; from +0x20 the commands, 0x20 bytes each
	SHM_CMD_SLOTS = 15,
};

u32 Machine::virtToPhys (u32 va)
{
	if (va >= VA_LINEAR && va < VA_LINEAR_END) return va - VA_LINEAR + PA_FCRAM;
	if (va >= VA_VRAM && va < VA_VRAM + VRAM_SIZE) return va - VA_VRAM + PA_VRAM;
	return 0;
}

u8 *Machine::physPtr (u32 pa, u32 size) const
{
	if (pa >= PA_FCRAM && pa - PA_FCRAM <= FCRAM_SIZE && size <= FCRAM_SIZE - (pa - PA_FCRAM)) return mem.fcram + (pa - PA_FCRAM);
	if (pa >= PA_VRAM && pa - PA_VRAM <= VRAM_SIZE && size <= VRAM_SIZE - (pa - PA_VRAM)) return mem.vram + (pa - PA_VRAM);
	return 0;
}

void Machine::gspInterrupt (int id)
{
	if (!gsp.shared) return;
	u8 *q = gsp.shared->host + SHM_IRQ;
	if (q[1] < SHM_IRQ_SLOTS) { q[0xC + (q[0] + q[1]) % SHM_IRQ_SLOTS] = (u8) id; q[1]++; }
	else q[2] = 1;								// (the program does not take them: lost)
	if (gsp.irq) { gsp.irq->signaled = true; wakeWaiters (gsp.irq); }
}

// The frame's end: the framebuffers the program asked for are shown, and it is told both screens' VBlank.
void Machine::vblank ()
{
	gsp.frames++;
	hidUpdate (this);
	if (gsp.shared)
		for (int sc = 0; sc < 2; sc++)
		{
			u8 *u = gsp.shared->host + SHM_FB + sc * 0x40;
			if (!u[1]) continue;
			u32 info[7];
			memcpy (info, u + 4 + (u[0] & 1) * 0x1C, sizeof info);
			Framebuffer &fb = gsp.fb[sc];
			fb.active = info[0]; fb.left = info[1]; fb.right = info[2]; fb.stride = info[3]; fb.format = info[4]; fb.select = info[5]; fb.unknown = info[6];
			fb.set = true;
			u[1] = 0;
			if (sc == 0 && fb.right && fb.right != fb.left)		// (the top screen's two eyes: remembered, see monoOnly)
			{
				bool known = false;
				for (int i = 0; i < gsp.eyePairCount; i++) if (gsp.eyePairs[i][0] == fb.left && gsp.eyePairs[i][1] == fb.right) known = true;
				if (!known) { const int i = gsp.eyePairCount < 4 ? gsp.eyePairCount++ : 0; gsp.eyePairs[i][0] = fb.left; gsp.eyePairs[i][1] = fb.right; }
			}
		}
	gspInterrupt (GSP_PDC0);
	gspInterrupt (GSP_PDC1);
}

// The GPU's work under way is ended, and the interrupt of the list it belonged to raised.
void Machine::gpuSync ()
{
	picaSync (this);
	if (gsp.listPending) { gsp.listPending = false; gspInterrupt (GSP_P3D); }
}

static u64 micros () { struct timeval tv; gettimeofday (&tv, 0); return (u64) tv.tv_sec * 1000000ull + (u64) tv.tv_usec; }

// One command of the GX queue (8 words; addresses are the program's).
static void gxCommand (Machine *m, const u32 *c)
{
	m->gpuSync ();								// (the commands follow each other: the list before is over first)
	if (m->traceGpu) fprintf (stderr, "gx %08x %08x %08x %08x %08x %08x %08x %08x%c", (unsigned) c[0], (unsigned) c[1], (unsigned) c[2], (unsigned) c[3], (unsigned) c[4], (unsigned) c[5], (unsigned) c[6], (unsigned) c[7], 10);
	switch (c[0] & 0xFF)
	{
	case 0:									// DMA (source, destination, size)
	{
		static u8 buf[4096];
		u32 src = c[1], dst = c[2], left = c[3];
		while (left) { u32 k = left < sizeof buf ? left : (u32) sizeof buf; if (!m->mem.read (src, buf, k) || !m->mem.write (dst, buf, k)) break; src += k; dst += k; left -= k; }
		m->gspInterrupt (GSP_DMA);
		break;
	}
	case 1:									// a PICA200 command list (address, size)
		m->gsp.cmdLists++;
		{ const u64 t = micros (); picaCommandList (m, c[1], c[2]); m->gsp.usLists += micros () - t; }
		if (picaBusy (m)) m->gsp.listPending = true;			// (its triangles are drawn aside: the interrupt at their end)
		else m->gspInterrupt (GSP_P3D);
		break;
	case 2:									// memory fill: two areas (start, value, end), their controls
		for (int k = 0; k < 2; k++)
		{
			const u64 t0 = micros ();
			const u32 start = c[1 + k * 3], value = c[2 + k * 3], end = c[3 + k * 3], ctl = c[7] >> (k * 16) & 0xFFFF;
			if (!start) continue;
			const int width = (ctl >> 8 & 3) == 2 ? 4 : (ctl >> 8 & 3) == 1 ? 3 : 2;	// 32, 24 or 16 bits a value
			// (the area is linear memory or VRAM: one piece in the host -- filled there; else through the page table)
			if (end > start) picaBeforeFill (m, Machine::virtToPhys (start), Machine::virtToPhys (start) + (end - start));
			u8 *host = end > start ? m->physPtr (Machine::virtToPhys (start), end - start) : 0;
			if (host)
			{
				const u32 count = (end - start) / (u32) width;
				if (width == 4) { u32 *q = (u32 *) host; for (u32 i = 0; i < count; i++) q[i] = value; }
				else if (width == 2) { u16 *q = (u16 *) host; const u16 v16 = (u16) value; for (u32 i = 0; i < count; i++) q[i] = v16; }
				else for (u32 i = 0; i < count; i++) { host[i * 3] = (u8) value; host[i * 3 + 1] = (u8) (value >> 8); host[i * 3 + 2] = (u8) (value >> 16); }
			}
			else for (u32 a = start; a + (u32) width <= end && a >= start; a += (u32) width)
				if (!m->mem.write (a, &value, (u32) width)) break;
			m->gsp.fills++;
			m->gsp.usFills += micros () - t0;
			m->gspInterrupt (k ? GSP_PSC1 : GSP_PSC0);
		}
		break;
	case 3:									// display transfer (what the GPU rendered -> a framebuffer)
	{
		m->gsp.transfers++;
		// The right eye's picture is never shown by a host with one picture a screen (monoOnly). A program that
		// renders an eye in a buffer S, sends it to the left framebuffer, renders the other eye in S again and
		// sends it to the right one -- seen two frames in a row -- has its draws into S left out between the two
		// transfers (n3ds_pica.cpp's draw), and the second transfer too. A frame that does otherwise ends that
		// (one picture may be wrong then); after three such frames it is not tried any more.
		bool skip = false;
		if (m->monoOnly && m->gsp.eyeBroken < 3)
		{
			auto &g = m->gsp;
			const u32 src = c[1], dst = c[2];
			int side = 0;
			for (int i = 0; i < g.eyePairCount; i++) { if (dst == g.eyePairs[i][0]) side = 1; else if (dst == g.eyePairs[i][1]) side = 2; }
			if (side == 1)
			{
				if (src != g.eyeSrc) g.eyeSeen = 0;
				else if (g.eyeSkip) { g.eyeSeen = 0; g.eyeBroken++; }	// (no right eye came: its draws were this picture's)
				g.eyeSrc = src; g.eyeLastSide = 1;
				g.eyeSkip = g.eyeSeen >= 2;
			}
			else if (src == g.eyeSrc)
			{
				if (side == 2 && g.eyeLastSide == 1) { skip = g.eyeSkip; if (g.eyeSeen < 2) g.eyeSeen++; }
				else { if (g.eyeSkip) g.eyeBroken++; g.eyeSeen = 0; }
				g.eyeSkip = false; g.eyeLastSide = side;
			}
		}
		if (!skip) { const u64 t = micros (); picaDisplayTransfer (m, c); m->gsp.usTransfers += micros () - t; }
		m->gspInterrupt (GSP_PPF);
	}
		break;
	case 4:									// texture copy
		m->note ("a texture copy");
		m->gsp.transfers++;
		m->gspInterrupt (GSP_PPF);
		break;
	case 5:									// flush cache regions
		break;
	default:
		m->note ("GX command %02x", (unsigned) (c[0] & 0xFF));
		break;
	}
}

void gspRequest (Machine *m, Session *, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	switch (id)
	{
	case 0x01:								// WriteHWRegs (offset, size, the data): the LCD's registers -- nothing kept
	case 0x02:								// WriteHWRegsWithMask
	case 0x08:								// FlushDataCache
	case 0x0B:								// SetLcdForceBlack
	case 0x10:								// SetAxiConfigQoSMode
	case 0x16:								// AcquireRight
	case 0x17:								// ReleaseRight
	case 0x19:								// SaveVramSysArea
	case 0x1A:								// RestoreVramSysArea
	case 0x1E:								// SetInternalPriorities
	case 0x1F:								// StoreDataCache
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x04:								// ReadHWRegs (offset, size) into the buffer the thread gave: zeros
	{
		const u32 size = cmd[2];
		const u32 *stat = cmd + 0x40;					// (the static buffers' table follows the command buffer)
		if (size <= 0x100 && (stat[0] & 0xF) == 2) m->mem.fill (stat[1], 0, size);
		cmd[0] = ipcHeader (id, 1, 2); cmd[1] = RES_OK;
		break;
	}
	case 0x05:								// SetBufferSwap (screen, a framebuffer info): shown at once
	{
		if (cmd[1] < 2)
		{
			Machine::Framebuffer &fb = m->gsp.fb[cmd[1]];
			fb.active = cmd[2]; fb.left = cmd[3]; fb.right = cmd[4]; fb.stride = cmd[5]; fb.format = cmd[6]; fb.select = cmd[7]; fb.unknown = cmd[8];
			fb.set = true;
		}
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	}
	case 0x0C:								// TriggerCmdReqQueue: the commands the program queued
	{
		if (m->gsp.shared)
		{
			u8 *q = m->gsp.shared->host + SHM_CMD;
			u32 first = q[0] % SHM_CMD_SLOTS, count = q[1];
			if (count > SHM_CMD_SLOTS) count = SHM_CMD_SLOTS;
			for (u32 i = 0; i < count; i++)
			{
				u32 c[8];
				memcpy (c, q + 0x20 + (first + i) % SHM_CMD_SLOTS * 0x20, sizeof c);
				gxCommand (m, c);
			}
			q[0] = (u8) ((first + count) % SHM_CMD_SLOTS); q[1] = 0;
		}
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	}
	case 0x13:								// RegisterInterruptRelayQueue (flags, -, an event) -> thread index, the shared memory
	{
		Event *ev = (Event *) m->handleGet (cmd[3], OBJ_EVENT);
		if (!ev) { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_INVALID_HANDLE; break; }
		if (!m->gsp.shared)
		{
			u8 *host = m->mem.allocTop (SHM_SIZE);
			if (!host) { cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OUT_OF_MEMORY; break; }
			m->gsp.shared = new SharedMem (host, SHM_SIZE);
		}
		memset (m->gsp.shared->host, 0, SHM_SIZE);
		ev->refs++;
		m->release (m->gsp.irq);
		m->gsp.irq = ev;
		m->gsp.shared->refs++;
		u32 h = m->handleNew (m->gsp.shared);
		cmd[0] = ipcHeader (id, 2, 2); cmd[1] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; cmd[2] = 0; cmd[3] = 0; cmd[4] = h;
		break;
	}
	case 0x14:								// UnregisterInterruptRelayQueue
		m->release (m->gsp.irq); m->gsp.irq = 0;
		cmd[0] = ipcHeader (id, 1, 0); cmd[1] = RES_OK;
		break;
	case 0x18:								// ImportDisplayCaptureInfo -> each screen's buffers, format, stride
		cmd[0] = ipcHeader (id, 9, 0); cmd[1] = RES_OK;
		for (int i = 0; i < 2; i++)
		{
			const Machine::Framebuffer &f = m->gsp.fb[i];
			cmd[2 + i * 4] = f.left; cmd[3 + i * 4] = f.right; cmd[4 + i * 4] = f.format & 7; cmd[5 + i * 4] = f.stride;
		}
		break;
	default:
		ipcStub (m, "gsp::Gpu", cmd);
		break;
	}
}

// The picture of a screen, from the framebuffer shown. Formats: 0 RGBA8, 1 BGR8, 2 RGB565, 3 RGB5A1, 4 RGBA4.
void Machine::screenImage (int screen, u32 *dst) const
{
	const int w = screen == SCREEN_TOP ? TOP_W : BOTTOM_W;
	const Framebuffer &fb = gsp.fb[screen & 1];
	const u32 fmt = fb.format & 7;
	const u32 bpp = fmt == 0 ? 4 : fmt == 1 ? 3 : 2;
	if (!fb.set || !fb.left || fmt > 4 || fb.stride < SCREEN_H * bpp || fb.stride > 4096) { memset (dst, 0, (size_t) w * SCREEN_H * 4); return; }
	u8 col[SCREEN_H * 4];
	for (int x = 0; x < w; x++)
	{
		if (!mem.read (fb.left + (u32) x * fb.stride, col, SCREEN_H * bpp)) memset (col, 0, sizeof col);
		for (int y = 0; y < SCREEN_H; y++)
		{
			const u8 *p = col + (u32) (SCREEN_H - 1 - y) * bpp;
			u32 r, g, b;
			switch (fmt)
			{
			case 0: r = p[3]; g = p[2]; b = p[1]; break;
			case 1: r = p[2]; g = p[1]; b = p[0]; break;
			case 2: { u32 v = (u32) (p[0] | p[1] << 8); r = v >> 11; g = v >> 5 & 63; b = v & 31; r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2; break; }
			case 3: { u32 v = (u32) (p[0] | p[1] << 8); r = v >> 11; g = v >> 6 & 31; b = v >> 1 & 31; r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2; break; }
			default: { u32 v = (u32) (p[0] | p[1] << 8); r = (v >> 12) * 17; g = (v >> 8 & 15) * 17; b = (v >> 4 & 15) * 17; break; }
			}
			dst[y * w + x] = r << 16 | g << 8 | b;
		}
	}
}

}
