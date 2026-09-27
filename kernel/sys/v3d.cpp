//
// v3d.cpp -- the V3D GPU (VideoCore VI, V3D 4.2) of the Raspberry Pi 4: kapi v52 gpu_info /
// gpu_draw. Depth-tested, Gouraud-shaded triangles rendered by the GPU's tile pipeline.
//
// Bring-up (first gpu_info / gpu_draw): the firmware powers the V3D domain and its clock
// (mailbox), then the reset is released (PM_GRAFX) and the async AXI bridges opened (ASB),
// as Linux does; the identity registers must then say V3D 4.x. No V3D MMU: the GPU gets
// physical addresses, all our buffers come from the low heap (< 1 GB).
//
// A draw: the vertices (NDC position + RGBA8) are copied into a GPU buffer; a binning
// control list (the viewport, depth test LESS, the shader state, one VERTEX_ARRAY_PRIMS)
// sorts the triangles into 64 x 64 tile lists; a rendering control list clears each tile,
// runs its list, stores the colours (raster RGBA8). The three shaders (coordinate, vertex,
// fragment: position + colour varyings) are the QPU code of Random06457's
// rpi4-gpu-bare-metal-examples (MIT), compiled from GLSL by Mesa. Completion is polled (the
// bin / render frame counters), with a time limit; the binner's out-of-memory requests are
// answered from an overflow pool. CPU caches: cleaned before the GPU reads, invalidated
// before we read what it wrote. The result is copied into the caller's pixels (0x00RRGGBB).
//
#include <kern/crashlog.h>
#include <kern/kapi_abi.h>
#include <kern/v3d_cl.h>
#include <kern/v3d_tiling.h>
#include <kern/v3d.h>
#include <kern/v3d_clip.h>
#include <kern/addrspace.h>
#include <circle/memio.h>
#include <circle/bcm2835.h>
#include <circle/string.h>
#include <circle/bcmpropertytags.h>
#include <circle/memory.h>
#include <circle/synchronize.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/synchronizationevent.h>
#include <circle/interrupt.h>
#include <circle/util.h>
#include <circle/types.h>

static const char From[] = "v3d";

// ---- registers --------------------------------------------------------------------------------------
#define V3D_HUB			(ARM_IO_BASE + 0xC00000)
#define V3D_HUB_IDENT0		(V3D_HUB + 0x08)
#define V3D_HUB_IDENT1		(V3D_HUB + 0x0C)
#define V3D_HUB_IDENT2		(V3D_HUB + 0x10)
#define V3D_HUB_INT_MSK_SET	(V3D_HUB + 0x60)
#define V3D_CORE0		(ARM_IO_BASE + 0xC04000)
#define V3D_CTL_IDENT0		(V3D_CORE0 + 0x000)
#define V3D_CTL_SLCACTL		(V3D_CORE0 + 0x024)
#define V3D_CTL_L2TCACTL	(V3D_CORE0 + 0x030)
#define V3D_CTL_L2TFLSTA	(V3D_CORE0 + 0x034)
#define V3D_CTL_L2TFLEND	(V3D_CORE0 + 0x038)
#define V3D_CTL_INT_STS		(V3D_CORE0 + 0x050)
#define V3D_CTL_INT_CLR		(V3D_CORE0 + 0x058)
#define V3D_CTL_INT_MSK_SET	(V3D_CORE0 + 0x060)
#define V3D_CTL_INT_MSK_CLR	(V3D_CORE0 + 0x064)
#define   V3D_INT_FRDONE	(1 << 0)		// render frame done
#define   V3D_INT_FLDONE	(1 << 1)		// bin (tile lists) done
#define   V3D_INT_OUTOMEM	(1 << 2)		// the binner wants memory
#define   V3D_INTS		(V3D_INT_FRDONE | V3D_INT_FLDONE | V3D_INT_OUTOMEM)
#define V3D_IRQ			GIC_SPI (74)		// (the device tree's v3d node: core and hub share it)
#define V3D_CLE_CT0CS		(V3D_CORE0 + 0x100)
#define V3D_CLE_CT1CS		(V3D_CORE0 + 0x104)
#define V3D_CLE_CT0CA		(V3D_CORE0 + 0x110)
#define V3D_CLE_CT1CA		(V3D_CORE0 + 0x114)
#define V3D_CLE_BFC		(V3D_CORE0 + 0x134)
#define V3D_CLE_RFC		(V3D_CORE0 + 0x138)
#define V3D_CLE_CT0QTS		(V3D_CORE0 + 0x15C)
#define V3D_CLE_CT0QBA		(V3D_CORE0 + 0x160)
#define V3D_CLE_CT1QBA		(V3D_CORE0 + 0x164)
#define V3D_CLE_CT0QEA		(V3D_CORE0 + 0x168)
#define V3D_CLE_CT1QEA		(V3D_CORE0 + 0x16C)
#define V3D_CLE_CT0QMA		(V3D_CORE0 + 0x170)
#define V3D_CLE_CT0QMS		(V3D_CORE0 + 0x174)
#define V3D_PTB_BPOA		(V3D_CORE0 + 0x308)
#define V3D_PTB_BPOS		(V3D_CORE0 + 0x30C)

#define PM_GRAFX		(ARM_IO_BASE + 0x10010C)
#define   PM_PASSWORD		0x5A000000
#define   PM_V3DRSTN		(1 << 6)
#define ASB_V3D_S_CTRL		(ARM_IO_BASE + 0xC11008)
#define ASB_V3D_M_CTRL		(ARM_IO_BASE + 0xC1100C)
#define   ASB_REQ_STOP		(1 << 0)
#define   ASB_ACK		(1 << 1)

#define PROPTAG_SET_CLOCK_STATE	0x00038001
#define CLOCK_ID_V3D		5
#define DOMAIN_ID_V3D		10

// ---- shaders (QPU code, from Random06457's example; see the header) ------------------------------------
// fragment: outColor = v_color
static const u64 FRAG_SHADER[] = {
	0x3D103186BB800000, 0x54003046BBC00000, 0x3D10A18605829000, 0x540030C6BBC80000,
	0x3D1121850582B000, 0x54003006BBD00000, 0x3D30618405828000, 0x54203086BBC40000,
	0x3C0021830582A000, 0x3C2031873583E185, 0x3C0031873583E103, 0x3C003186BB800000 };
// vertex: gl_Position = vec4 (position, 1.0); v_color = color
static const u64 VTX_SHADER[] = {
	0x3DE02187BC807000, 0x3DE02189BC807001, 0x3DE0218ABC807002, 0x3DE02183BC807003,
	0x3D823186BB800000, 0x3DE02184BC807004, 0x3C403186BB800000, 0x55E02005BCB871C5,
	0x3DE02186BC807006, 0x3C403181F5818000, 0x3DE02180F88370C4, 0x54403086BBB80240,
	0x3DE02180F8837105, 0x3DE02180F8837146, 0x54403003F5B9A280, 0x3DE02180F8837187,
	0x3DE02180F880F000, 0x3C00318405828000, 0x3DE02180F881F001, 0x3DE02180F8827002,
	0x3DE02180F8837203, 0x3C003186BB816000, 0x3C203186BB800000, 0x3C003186BB800000,
	0x3C003186BB800000 };
// coordinate (the binner's copy of the vertex shader: position only)
static const u64 COORD_SHADER[] = {
	0x3DE02184BC807000, 0x3DE02185BC807001, 0x3DE02183BC807002, 0x3DE02180F8837100,
	0x3DE02180F8837141, 0x3C403186BB800000, 0x3DE02180F88370C2, 0x3DE02180F882F003,
	0x3C403186BB800000, 0x54403006BBB80100, 0x54003081F5B98140, 0x3DE02180F880F004,
	0x3C003183F581A000, 0x3DE02180F881F005, 0x3C003186BB816000, 0x3C203186BB800000,
	0x3C003186BB800000, 0x3C003186BB800000 };

// the v53 shaders (hand-written QPU code: v3d_shaders.qasm, assembled by tools/qpu/qpuasm):
// VS_CLIP / CS_CLIP (the batch's matrix, the divide by w), FS_COLOR, FS_TEX (texture * colour)
#include "v3d_shaders.inc"

// ---- GPU memory: low heap, page aligned, physical = virtual ------------------------------------------------
struct TGpuBuf
{
	u8 *pRaw, *p; u32 nSize;
	u32 Bus (u32 off = 0) const { return (u32) (uintptr) (p + off); }
};

static boolean Alloc (TGpuBuf &b, u32 nSize)
{
	if (b.p != 0 && b.nSize >= nSize) return TRUE;
	if (b.pRaw != 0) CMemorySystem::HeapFree (b.pRaw);
	b.pRaw = (u8 *) CMemorySystem::HeapAllocate (nSize + 4096, HEAP_LOW);
	if (b.pRaw == 0) { b.p = 0; b.nSize = 0; return FALSE; }
	b.p = (u8 *) (((uintptr) b.pRaw + 4095) & ~(uintptr) 4095);
	b.nSize = nSize;
	if ((uintptr) b.p + nSize > 0x40000000) { CMemorySystem::HeapFree (b.pRaw); b.pRaw = b.p = 0; b.nSize = 0; return FALSE; }
	return TRUE;
}

// A control list being written.
class CList
{
public:
	CList (TGpuBuf &b) : m_b (b), m_n (0) {}
	template <typename T> CList &operator<< (const T &x)
	{
		if (m_n + sizeof (T) <= m_b.nSize) memcpy (m_b.p + m_n, &x, sizeof (T));
		m_n += sizeof (T);
		return *this;
	}
	void Align (u32 a) { while (m_n % a) *this << (u8) 0; }
	u32 Bus () const { return m_b.Bus (m_n); }		// where the next bytes go
	u32 Start () const { return m_b.Bus (); }
	u32 Size () const { return m_n; }
	boolean Overflow () const { return m_n > m_b.nSize; }
private:
	TGpuBuf &m_b; u32 m_n;
};

static TGpuBuf s_BCL, s_RCL, s_Ind, s_State, s_Verts, s_Target, s_TileAlloc, s_TileState, s_Overflow;
static int  s_nState = 0;			// 0 not tried, 1 up, -1 unavailable / failed
static char s_Info[96] = "not started";
static volatile boolean s_bBusy = FALSE;

#define MAX_W		2048
#define MAX_H		2048
#define OVERFLOW_CHUNK	(256 * 1024)
#define OVERFLOW_CHUNKS	16
#define TIMEOUT_US	500000

// s_State: the default attributes (0), the v52 shaders (256, 512, 1024), the v53 ones
#define STATE_VS_CLIP	2048
#define STATE_CS_CLIP	(STATE_VS_CLIP + 1024)
#define STATE_FS_COLOR	(STATE_CS_CLIP + 1024)
#define STATE_FS_TEX	(STATE_FS_COLOR + 512)
#define STATE_FS_COLOR_AT (STATE_FS_TEX + 512)
#define STATE_FS_TEX_AT	(STATE_FS_COLOR_AT + 512)
#define STATE_END	(STATE_FS_TEX_AT + 512)
static_assert (sizeof VS_CLIP <= 1024 && sizeof CS_CLIP <= 1024 && sizeof FS_COLOR <= 512 && sizeof FS_TEX <= 512
	       && sizeof FS_COLOR_AT <= 512 && sizeof FS_TEX_AT <= 512, "shader space");

static void Fmt (char *pOut, unsigned nCap, const char *pFmt, u32 a, u32 b, u32 c)
{
	CString s; s.Format (pFmt, a, b, c);
	unsigned i = 0; for (const char *q = s; *q && i + 1 < nCap; q++) pOut[i++] = *q;
	pOut[i] = 0;
}

// ---- bring-up ------------------------------------------------------------------------------------------------
static boolean WaitClear (uintptr nReg, u32 nBit)
{
	unsigned t0 = CTimer::Get ()->GetClockTicks ();
	while (read32 (nReg) & nBit)
		if (CTimer::Get ()->GetClockTicks () - t0 > 100000) return FALSE;
	return TRUE;
}

static boolean PowerOn (void)
{
	CBcmPropertyTags Tags;
	TPropertyTagDomainState Domain;
	Domain.nDomainId = DOMAIN_ID_V3D; Domain.nOn = DOMAIN_STATE_ON;
	if (!Tags.GetTag (PROPTAG_SET_DOMAIN_STATE, &Domain, sizeof Domain, 8))
		CLogger::Get ()->Write (From, LogWarning, "firmware: V3D power domain not confirmed");
	struct { TPropertyTag Tag; u32 nClockId; u32 nState; } PACKED Clock;
	Clock.nClockId = CLOCK_ID_V3D; Clock.nState = 1;
	if (!Tags.GetTag (PROPTAG_SET_CLOCK_STATE, &Clock, sizeof Clock, 8))
		CLogger::Get ()->Write (From, LogWarning, "firmware: V3D clock not enabled");
	TPropertyTagClockRate Rate;
	Rate.nClockId = CLOCK_ID_V3D; Rate.nRate = 0;
	u32 nMax = 0;
	if (Tags.GetTag (PROPTAG_GET_MAX_CLOCK_RATE, &Rate, sizeof Rate, 4)) nMax = Rate.nRate;
	if (nMax)
	{
		TPropertyTagClockRate Set; Set.nClockId = CLOCK_ID_V3D; Set.nRate = nMax;
		struct { TPropertyTagClockRate r; u32 skip; } PACKED S2 = { Set, 0 };
		Tags.GetTag (PROPTAG_SET_CLOCK_RATE, &S2, sizeof S2, 12);
	}
	CLogger::Get ()->Write (From, LogNotice, "V3D clock %u MHz (max)", nMax / 1000000);

	write32 (PM_GRAFX, PM_PASSWORD | (read32 (PM_GRAFX) & 0xFFFFFF) | PM_V3DRSTN);	// out of reset
	write32 (ASB_V3D_M_CTRL, PM_PASSWORD | (read32 (ASB_V3D_M_CTRL) & ~ASB_REQ_STOP & 0xFFFFFF));
	if (!WaitClear (ASB_V3D_M_CTRL, ASB_ACK)) { CLogger::Get ()->Write (From, LogWarning, "ASB master does not answer"); return FALSE; }
	write32 (ASB_V3D_S_CTRL, PM_PASSWORD | (read32 (ASB_V3D_S_CTRL) & ~ASB_REQ_STOP & 0xFFFFFF));
	if (!WaitClear (ASB_V3D_S_CTRL, ASB_ACK)) { CLogger::Get ()->Write (From, LogWarning, "ASB slave does not answer"); return FALSE; }
	return TRUE;
}

static void IrqOn (void);

static boolean Up (void)
{
	if (s_nState) return s_nState > 0;
	s_nState = -1;
	if (!PowerOn ()) { Fmt (s_Info, sizeof s_Info, "V3D: power-up failed", 0, 0, 0); return FALSE; }
	u32 nId0 = read32 (V3D_HUB_IDENT0), nId1 = read32 (V3D_HUB_IDENT1), nId2 = read32 (V3D_HUB_IDENT2);
	u32 nCtl = read32 (V3D_CTL_IDENT0);
	u32 nTver = nId1 & 0xF, nRev = (nId1 >> 4) & 0xF, nCores = (nId1 >> 8) & 0xF;
	CLogger::Get ()->Write (From, LogNotice, "HUB_IDENT0..2 %08X %08X %08X, CTL_IDENT0 %08X", nId0, nId1, nId2, nCtl);
	if (nTver != 4)
	{
		Fmt (s_Info, sizeof s_Info, "V3D: not found (IDENT1 %08X)", nId1, 0, 0);
		CLogger::Get ()->Write (From, LogWarning, "%s", s_Info);
		return FALSE;
	}
	boolean bOK = Alloc (s_BCL, 16 * 1024) && Alloc (s_RCL, 64 * 1024) && Alloc (s_Ind, 16 * 1024)
		   && Alloc (s_State, 16 * 1024) && Alloc (s_Overflow, OVERFLOW_CHUNK * OVERFLOW_CHUNKS);
	if (!bOK) { Fmt (s_Info, sizeof s_Info, "V3D: no memory below 1 GB", 0, 0, 0); return FALSE; }
	// the shaders + the default attribute values, once
	u8 *p = s_State.p;
	for (unsigned i = 0; i < 64; i++) { f32 v = (i & 3) == 3 ? 1.0f : 0.0f; memcpy (p + i * 4, &v, 4); }
	memcpy (p + 256, FRAG_SHADER, sizeof FRAG_SHADER);
	memcpy (p + 512, VTX_SHADER, sizeof VTX_SHADER);
	memcpy (p + 1024, COORD_SHADER, sizeof COORD_SHADER);
	memcpy (p + STATE_VS_CLIP, VS_CLIP, sizeof VS_CLIP);
	memcpy (p + STATE_CS_CLIP, CS_CLIP, sizeof CS_CLIP);
	memcpy (p + STATE_FS_COLOR, FS_COLOR, sizeof FS_COLOR);
	memcpy (p + STATE_FS_TEX, FS_TEX, sizeof FS_TEX);
	memcpy (p + STATE_FS_COLOR_AT, FS_COLOR_AT, sizeof FS_COLOR_AT);
	memcpy (p + STATE_FS_TEX_AT, FS_TEX_AT, sizeof FS_TEX_AT);
	CleanDataCacheRange ((uintptr) p, STATE_END);
	IrqOn ();
	Fmt (s_Info, sizeof s_Info, "V3D %u.%u (%u core)", nTver, nRev, nCores);
	CLogger::Get ()->Write (From, LogNotice, "%s ready", s_Info);
	s_nState = 1;
	return TRUE;
}

// ---- completion by interrupt --------------------------------------------------------------------------------
// The V3D core interrupt (GIC SPI 74): bin done, render done, and the binner's out-of-memory
// requests (answered from the overflow pool right there). The drawing task sleeps on an event;
// it wakes every 2 ms anyway and checks the frame counters, so a missing interrupt only costs
// latency (then it is logged once and the overflow requests are served by the task).
static CSynchronizationEvent s_BinDone, s_RenderDone;
static volatile unsigned s_nChunk = 0;
static volatile boolean s_bIrqSeen = FALSE;
static boolean s_bIrqWarned = FALSE;

static void GiveChunk (void)
{
	if (s_nChunk < OVERFLOW_CHUNKS)
	{
		unsigned n = s_nChunk;
		s_nChunk = n + 1;
		write32 (V3D_PTB_BPOA, s_Overflow.Bus (n * OVERFLOW_CHUNK));
		write32 (V3D_PTB_BPOS, OVERFLOW_CHUNK);
		write32 (V3D_CTL_INT_CLR, V3D_INT_OUTOMEM);
	}
	else
		write32 (V3D_CTL_INT_MSK_SET, V3D_INT_OUTOMEM);	// (none left: no interrupt storm)
}

static void V3DIrq (void *)
{
	u32 nSts = read32 (V3D_CTL_INT_STS);
	s_bIrqSeen = TRUE;
	if (nSts & V3D_INT_OUTOMEM) GiveChunk ();
	if (nSts & V3D_INT_FLDONE) { write32 (V3D_CTL_INT_CLR, V3D_INT_FLDONE); s_BinDone.Set (); }
	if (nSts & V3D_INT_FRDONE) { write32 (V3D_CTL_INT_CLR, V3D_INT_FRDONE); s_RenderDone.Set (); }
}

static void IrqOn (void)
{
	write32 (V3D_HUB_INT_MSK_SET, ~0u);				// (no hub interrupt used)
	write32 (V3D_CTL_INT_MSK_SET, ~(u32) V3D_INTS);
	write32 (V3D_CTL_INT_CLR, V3D_INTS);
	CInterruptSystem::Get ()->ConnectIRQ (V3D_IRQ, V3DIrq, 0);
	write32 (V3D_CTL_INT_MSK_CLR, V3D_INTS);
}

// Waits until the frame counter nReg moves off nOld: the event, or 2 ms slices. FALSE: timed out.
static boolean WaitCounter (CSynchronizationEvent &Ev, uintptr nReg, u32 nOld)
{
	unsigned t0 = CTimer::Get ()->GetClockTicks ();
	while ((read32 (nReg) & 0xFF) == nOld)
	{
		Ev.WaitWithTimeout (2000);
		if (!s_bIrqSeen)						// (no interrupt yet: serve the binner here)
		{
			EnterCritical ();
			if (read32 (V3D_CTL_INT_STS) & V3D_INT_OUTOMEM) GiveChunk ();
			LeaveCritical ();
		}
		if (CTimer::Get ()->GetClockTicks () - t0 > TIMEOUT_US) return FALSE;
	}
	if (!s_bIrqSeen && !s_bIrqWarned)
	{
		s_bIrqWarned = TRUE;
		CLogger::Get ()->Write (From, LogWarning, "no V3D interrupt seen: waiting by polling");
	}
	return TRUE;
}

// ---- the target: straight into the caller's pixels when the GPU can reach them ---------------------------------
// The pixels are the caller's virtual memory: each 4 KB page is translated (AT S1E1R); if the
// whole span is physically contiguous below 1 GB (a window canvas is), the GPU stores (and
// loads) the tiles right there, R and B swapped so that memory holds 0x00RRGGBB, alpha not
// written (the colour write mask) and cleared to 0. Else the frame goes through s_Target and
// is copied.
struct TTarget
{
	boolean bDirect;
	u32 nBus, nStrideBytes;		// where the GPU stores / loads, bytes a row
	uintptr ulVA; u32 nSpan;	// (direct: the caller's span, for the cache maintenance)
};

static u64 PhysOf (uintptr ulVA)
{
	u64 nPar;
	asm volatile ("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1" : "=r" (nPar) : "r" (ulVA) : "memory");
	if (nPar & 1) return ~0ull;
	return (nPar & 0x0000FFFFFFFFF000ull) | (ulVA & 0xFFF);
}

boolean g_bGpuDirect = TRUE;				// cmdline.txt gpudirect=0: always through s_Target

static void ResolveTarget (unsigned *pPx, int w, int h, int nStride, TTarget &T)
{
	T.bDirect = FALSE;
	T.nBus = s_Target.Bus (); T.nStrideBytes = (u32) w * 4;
	if (!g_bGpuDirect) return;
	uintptr ulVA = (uintptr) pPx;
	u64 nSpan = ((u64) (h - 1) * (u64) nStride + (u64) w) * 4;
	if ((ulVA & 3) || nSpan > 0x10000000) return;
	EnterCritical ();						// (PAR_EL1 is ours meanwhile)
	u64 nPA = PhysOf (ulVA);
	boolean bOK = nPA != ~0ull && nPA + nSpan <= 0x40000000;
	for (u64 nOff = 4096 - (ulVA & 4095); bOK && nOff < nSpan; nOff += 4096)
		bOK = PhysOf (ulVA + nOff) == nPA + nOff;
	LeaveCritical ();
	static int s_nLogged = -1;				// (the mode in kmsg when it changes)
	if (s_nLogged != (int) bOK)
	{
		s_nLogged = (int) bOK;
		CLogger::Get ()->Write (From, LogNotice, bOK ? "frames rendered straight into the target (at %lX)"
							 : "frames rendered into a GPU buffer, then copied (target at %lX)", (unsigned long) nPA);
	}
	if (!bOK) return;
	T.bDirect = TRUE;
	T.nBus = (u32) nPA; T.nStrideBytes = (u32) nStride * 4;
	T.ulVA = ulVA; T.nSpan = (u32) nSpan;
}

// Before the GPU runs: the frame's pixels (copy mode: loaded into s_Target when bKeep)
static void TargetBefore (const TTarget &T, const unsigned *pPx, int w, int h, int nStride, boolean bKeep)
{
	if (T.bDirect) { CleanAndInvalidateDataCacheRange (T.ulVA, T.nSpan); return; }
	if (bKeep)
		for (int y = 0; y < h; y++)
		{
			const unsigned *src = pPx + (long) y * nStride;
			u32 *d = (u32 *) (s_Target.p + (u32) y * (u32) w * 4);
			for (int x = 0; x < w; x++)
			{
				u32 c = src[x];
				d[x] = 0xFF000000 | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
			}
		}
	CleanAndInvalidateDataCacheRange ((uintptr) s_Target.p, (u32) (w * h * 4));
}

// After: the picture in the caller's pixels
static void TargetAfter (const TTarget &T, unsigned *pPx, int w, int h, int nStride)
{
	if (T.bDirect) { CleanAndInvalidateDataCacheRange (T.ulVA, T.nSpan); return; }
	InvalidateDataCacheRange ((uintptr) s_Target.p, (u32) (w * h * 4));
	for (int y = 0; y < h; y++)
	{
		const u32 *src = (const u32 *) (s_Target.p + (u32) y * (u32) w * 4);
		unsigned *d = pPx + (long) y * nStride;
		for (int x = 0; x < w; x++)
		{
			u32 c = src[x];
			d[x] = ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
		}
	}
}

// ---- a frame ---------------------------------------------------------------------------------------------------
static void InvalidateGpuCaches (void)
{
	write32 (V3D_CTL_L2TFLSTA, 0);
	write32 (V3D_CTL_L2TFLEND, ~0u);
	write32 (V3D_CTL_L2TCACTL, (1 << 0) | (0 << 1));		// L2TFLS, flush mode "flush"
	write32 (V3D_CTL_SLCACTL, ~0u);
}

static void Fail (const char *pWhat)
{
	CLogger::Get ()->Write (From, LogError, "%s timed out: CT0CS %08X CT1CS %08X CT0CA %08X CT1CA %08X INT %08X -- GPU left off",
				pWhat, read32 (V3D_CLE_CT0CS), read32 (V3D_CLE_CT1CS), read32 (V3D_CLE_CT0CA),
				read32 (V3D_CLE_CT1CA), read32 (V3D_CTL_INT_STS));
	Fmt (s_Info, sizeof s_Info, "V3D: stopped (a frame did not finish)", 0, 0, 0);
	s_nState = -1;
}

// The rendering list: each 64 x 64 tile cleared (or loaded from the target), its triangles
// drawn, stored into the target (raster RGBA8; direct: R / B swapped = 0x00RRGGBB).
static void BuildRCL (CList &R, CList &Ind, int w, int h, unsigned nClear, boolean bLoad, const TTarget &T)
{
	u32 tilesX = (u32) (w + 63) / 64, tilesY = (u32) (h + 63) / 64;
	u32 nClearRGBA = (T.bDirect ? 0 : 0xFF000000) | ((nClear & 0xFF) << 16) | (nClear & 0xFF00) | ((nClear >> 16) & 0xFF);
	R << TileRenderingModeCfgCommon (1, (u16) w, (u16) h, 0, false, false, 0, false, 2, false);
	R << TileRenderingModeCfgClearColorsPart1 (0, nClearRGBA, 0);
	R << TileRenderingModeCfgColor (0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	R << TileRenderingModeCfgZSClearValues (0, 1.0f);
	R << TileListInitialBlockSize (0, true);
	u32 stW = 1, stH = 1, fW, fH;
	for (;;)
	{
		fW = (tilesX + stW - 1) / stW; fH = (tilesY + stH - 1) / stH;
		if (fW * fH < 256) break;
		if (stW < stH) stW++; else stH++;
	}
	R << MulticoreRenderingTileListSetBase (s_TileAlloc.Bus ());
	R << MulticoreRenderingSupertileCfg (stW, stH, fW, fH, tilesX, tilesY, 1, false, false);
	R << TileCoordinates (0, 0);				// clear, then a dummy store (a hardware race)
	R << OP_END_OF_LOADS;
	R << StoreTileBufferGeneral ();
	R << ClearTileBuffers (true, true);
	R << OP_END_OF_TILE_MARKER;
	R << TileCoordinates (0, 0);
	R << OP_END_OF_LOADS;
	R << StoreTileBufferGeneral ();
	R << OP_END_OF_TILE_MARKER;
	R << OP_FLUSH_VCD_CACHE;
	u32 nGeneric = Ind.Bus ();					// each tile: its list, store, clear
	Ind << OP_TILE_COORDINATES_IMPLICIT;
	if (bLoad)							// the target's pixels first
		Ind << LoadTileBufferGeneral (BUFFER_RENDER_TARGET_0, V3D_TILING_RASTER, false, V3D_DECIMATE_MODE_SAMPLE_0,
					      V3D_OUTPUT_IMAGE_FORMAT_RGBA8, !T.bDirect, false, T.bDirect, T.nStrideBytes, 0, T.nBus);
	Ind << OP_END_OF_LOADS;
	Ind << PrimListFormat (LIST_TRIANGLES, false);
	Ind << BranchToImplicitTileList (0);
	Ind << StoreTileBufferGeneral (BUFFER_RENDER_TARGET_0, V3D_TILING_RASTER, false, V3D_DITHER_MODE_NONE,
				       V3D_DECIMATE_MODE_SAMPLE_0, V3D_OUTPUT_IMAGE_FORMAT_RGBA8, false, false, T.bDirect,
				       T.nStrideBytes, 0, T.nBus);
	Ind << ClearTileBuffers (true, true);
	Ind << OP_END_OF_TILE_MARKER;
	Ind << OP_RETURN_FROM_SUB_LIST;
	R << StartAddressOfGenericTileList (nGeneric, Ind.Bus ());
	for (u32 y = 0; y <= (u32) (h - 1) / (64 * stH); y++)
		for (u32 x = 0; x <= (u32) (w - 1) / (64 * stW); x++)
			R << SupertileCoorinates ((u8) x, (u8) y);
	R << OP_END_OF_RENDERING;
}

// Bins then renders (the interrupt, with a time limit). 0 ok, -3 the GPU did not finish.
static int Run (CList &B, CList &R, CList &Ind, u32 nAllocSize)
{
	CleanDataCacheRange ((uintptr) s_BCL.p, B.Size ());
	CleanDataCacheRange ((uintptr) s_RCL.p, R.Size ());
	CleanDataCacheRange ((uintptr) s_Ind.p, Ind.Size ());
	DataSyncBarrier ();

	// ---- bin
	InvalidateGpuCaches ();
	s_nChunk = 0;
	write32 (V3D_CTL_INT_CLR, V3D_INTS);
	write32 (V3D_CTL_INT_MSK_CLR, V3D_INTS);
	s_BinDone.Clear (); s_RenderDone.Clear ();
	u32 nBfc = read32 (V3D_CLE_BFC) & 0xFF;
	write32 (V3D_CLE_CT0QMA, s_TileAlloc.Bus ());
	write32 (V3D_CLE_CT0QMS, nAllocSize);
	write32 (V3D_CLE_CT0QTS, s_TileState.Bus () | (1 << 1));
	write32 (V3D_CLE_CT0QBA, B.Start ());
	CrashLogCrumb (CRUMB_V3D, 2);
	write32 (V3D_CLE_CT0QEA, B.Start () + B.Size ());
	if (!WaitCounter (s_BinDone, V3D_CLE_BFC, nBfc)) { Fail ("binning"); return -3; }

	// ---- render
	InvalidateGpuCaches ();
	u32 nRfc = read32 (V3D_CLE_RFC) & 0xFF;
	write32 (V3D_CLE_CT1QBA, R.Start ());
	CrashLogCrumb (CRUMB_V3D, 3);
	write32 (V3D_CLE_CT1QEA, R.Start () + R.Size ());
	if (!WaitCounter (s_RenderDone, V3D_CLE_RFC, nRfc)) { Fail ("rendering"); return -3; }
	CrashLogCrumb (CRUMB_V3D, 0);

	return 0;
}

static int Draw (const kapi_gpu_vertex *pV, unsigned n, unsigned nClear, unsigned *pDst, int w, int h, int nStride)
{
	u32 tilesX = (u32) (w + 63) / 64, tilesY = (u32) (h + 63) / 64, nTiles = tilesX * tilesY;
	u32 nAllocSize = ((nTiles * 64 + 4095) & ~4095u) + 2 * 1024 * 1024;
	if (!Alloc (s_Verts, (n ? n : 3) * sizeof (kapi_gpu_vertex)) || !Alloc (s_Target, (u32) (w * h * 4))
	    || !Alloc (s_TileAlloc, nAllocSize) || !Alloc (s_TileState, nTiles * 256))
		return -2;
	memcpy (s_Verts.p, pV, n * sizeof (kapi_gpu_vertex));
	CleanDataCacheRange ((uintptr) s_Verts.p, n * sizeof (kapi_gpu_vertex));
	TTarget T; ResolveTarget (pDst, w, h, nStride, T);
	TargetBefore (T, pDst, w, h, nStride, FALSE);

	// ---- uniforms, shader state record, attributes
	CList Ind (s_Ind);
	u32 nFragUnif = Ind.Bus ();
	u32 nVtxUnif = Ind.Bus ();
	Ind << 1.0f << (f32) (w / 2) * 256.0f << (f32) (h / 2) * -256.0f << 0.5f << 0.5f;
	u32 nCoordUnif = Ind.Bus ();
	Ind << 1.0f << (f32) (w / 2) * 256.0f << (f32) (h / 2) * -256.0f;
	Ind.Align (32);
	u32 nShaderRec = Ind.Bus ();
	GLShaderStateRecord Rec {};
	Rec.enable_clipping = true;
	Rec.fragment_shader_uses_real_pixel_centre_w_in_addition_to_centroid_w2 = true;
	Rec.disable_implicit_point_line_varyings = true;
	Rec.number_of_varyings_in_fragment_shader = 4;
	Rec.coordinate_shader_output_vpm_segment_size = 1;
	Rec.coordinate_shader_input_vpm_segment_size = 1;
	Rec.vertex_shader_output_vpm_segment_size = 1;
	Rec.vertex_shader_input_vpm_segment_size = 1;
	Rec.address_of_default_attribute_values = s_State.Bus (0);
	Rec.fragment_shader_code_address = s_State.Bus (256) >> 3;
	Rec.fragment_shader_uniforms_address = nFragUnif;
	Rec.fragment_shader_4_way_threadable = true;
	Rec.fragment_shader_start_in_final_thread_section = false;
	Rec.fragment_shader_propagate_nans = true;
	Rec.vertex_shader_code_address = s_State.Bus (512) >> 3;
	Rec.vertex_shader_uniforms_address = nVtxUnif;
	Rec.vertex_shader_4_way_threadable = true;
	Rec.vertex_shader_start_in_final_thread_section = true;
	Rec.vertex_shader_propagate_nans = true;
	Rec.coordinate_shader_code_address = s_State.Bus (1024) >> 3;
	Rec.coordinate_shader_uniforms_address = nCoordUnif;
	Rec.coordinate_shader_4_way_threadable = true;
	Rec.coordinate_shader_start_in_final_thread_section = true;
	Rec.coordinate_shader_propagate_nans = true;
	Ind << Rec;
	GlShaderStateAttributeRecord Pos {};
	Pos.address = s_Verts.Bus (0);
	Pos.number_of_values_read_by_vertex_shader = 3;
	Pos.number_of_values_read_by_coordinate_shader = 3;
	Pos.stride = sizeof (kapi_gpu_vertex);
	Pos.maximum_index = 0xFFFFFF;
	Pos.vec_size = 3;
	Pos.type = 2;						// float
	Ind << Pos;
	GlShaderStateAttributeRecord Col {};
	Col.address = s_Verts.Bus (12);
	Col.number_of_values_read_by_vertex_shader = 4;
	Col.number_of_values_read_by_coordinate_shader = 0;
	Col.stride = sizeof (kapi_gpu_vertex);
	Col.maximum_index = 0xFFFFFF;
	Col.type = 4;						// byte
	Col.normalized_int_type = true;
	Ind << Col;

	// ---- the binning list
	CList B (s_BCL);
	B << TileBinningModeCfg (0, 0, 1, 0, false, false, (u16) w, (u16) h);
	B << OP_FLUSH_VCD_CACHE;
	B << OcclusionQueryCounter (0);
	B << OP_START_TILE_BINNING;
	B << ClipWindow (0, 0, (u16) w, (u16) h);
	// both faces, depth test LESS with depth writes
	B << CfgBits (true, true, true, false, 0, 0, false, 1, true, false, false, false, false, false, false);
	B << PointSize (1.0f);
	B << LineWidth (1.0f);
	B << ClipperXYScaling ((f32) (w / 2) * 256.0f, (f32) (h / 2) * -256.0f);
	B << ClipperZScaleAndOffset (0.5f, 0.5f);
	B << CLipperZMinMaxClippingPlanes (0.0f, 1.0f);
	B << ViewportOffset ((f32) (w / 2), (f32) (h / 2), 0, 0);
	B << ColorWriteMasks (T.bDirect ? 0x8 : 0);		// (direct: alpha not written, stays 0)
	B << BlendConstantColor (0, 0, 0, 0);
	B << OP_ZERO_ALL_FLAT_SHADE_FLAGS;
	B << OP_ZERO_ALL_NON_PERSPECTIVE_FLAGS;
	B << OP_ZERO_ALL_CENTROID_FLAGS;
	B << TransformFeedbackSpecs (0, false);
	B << OcclusionQueryCounter (0);
	B << SampleState (0xF, 1.0f);
	B << VcmCacheSize (4, 4);
	B << GlShaderState (nShaderRec, 2);
	B << VertexArrayPrims (4, n, 0);				// triangles
	B << OP_FLUSH;

	// ---- the rendering list, then the GPU
	CList R (s_RCL);
	BuildRCL (R, Ind, w, h, nClear, FALSE, T);
	if (B.Overflow () || R.Overflow () || Ind.Overflow ()) return -2;
	int nRes = Run (B, R, Ind, nAllocSize);
	if (nRes != 0) return nRes;

	TargetAfter (T, pDst, w, h, nStride);
	return 0;
}

// ---- v53: textures -------------------------------------------------------------------------------------------
// A texture: its TEXTURE_SHADER_STATE (32 bytes, at 0) then its texels (RGBA8, R first in
// memory, at 256), laid out as the TMU reads the level 0 of that size (Mesa's
// v3d_setup_slices): a line of 4 x 4 utiles (LT) when a side is <= 4, 8 x 8 blocks of 2 x 2
// utiles in 1 or 2 columns (UBLINEAR) up to 16 wide, else UIF: columns of 4 such blocks,
// padded (ub_pad) and XOR-ed as the hardware expects.
struct TTexture
{
	TGpuBuf Mem;
	CAddressSpace *pOwner;			// 0: free
	u16 w, h;
};
static TTexture s_Tex[KAPI_GPU_MAX_TEXTURES];
#define TEX_TEXELS	256

static void SetBits (u8 *p, u32 nStart, u32 nSize, u32 v)
{
	for (u32 i = 0; i < nSize; i++)
		if (v & (1u << i)) p[(nStart + i) / 8] |= (u8) (1 << ((nStart + i) % 8));
}

static CAddressSpace *CurrentAS (void)
{
	return (CAddressSpace *) CScheduler::Get ()->GetCurrentTask ()->GetUserData (TASK_USER_DATA_USER);
}

static void FreeTex (TTexture &t)
{
	if (t.Mem.pRaw != 0) CMemorySystem::HeapFree (t.Mem.pRaw);
	t.Mem.pRaw = t.Mem.p = 0; t.Mem.nSize = 0;
	t.pOwner = 0;
}

static int Texture (int nHandle, const unsigned *pPx, int w, int h, int nStride)
{
	CAddressSpace *pAS = CurrentAS ();
	if (nHandle >= 0)
	{
		if (nHandle >= KAPI_GPU_MAX_TEXTURES || s_Tex[nHandle].pOwner != pAS || pAS == 0) return -2;
		if (pPx == 0) { FreeTex (s_Tex[nHandle]); return 0; }
	}
	if (pPx == 0 || w <= 0 || h <= 0 || w > KAPI_GPU_MAX_TEXSIZE || h > KAPI_GPU_MAX_TEXSIZE || nStride < w) return -2;
	if (nHandle < 0)
	{
		for (int i = 0; i < KAPI_GPU_MAX_TEXTURES; i++)
			if (s_Tex[i].pOwner == 0) { nHandle = i; break; }
		if (nHandle < 0) return -4;
	}
	TTexture &t = s_Tex[nHandle];
	TLayout L; Layout ((u32) w, (u32) h, L);
	u32 nBytes = TEX_TEXELS + L.nPadW * L.nPadH * 4;
	if (!Alloc (t.Mem, nBytes)) { FreeTex (t); return -4; }
	t.pOwner = pAS; t.w = (u16) w; t.h = (u16) h;
	u8 *pT = t.Mem.p + TEX_TEXELS;
	memset (pT, 0, nBytes - TEX_TEXELS);
	for (int y = 0; y < h; y++)
	{
		const unsigned *src = pPx + (long) y * nStride;
		for (int x = 0; x < w; x++)
		{
			u32 c = src[x];					// 0xAARRGGBB -> R G B A in memory
			*(u32 *) (pT + TexelOffset (L, (u32) x, (u32) y)) = (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
		}
	}
	u8 *pS = t.Mem.p;					// TEXTURE_SHADER_STATE (Mesa v3d_packet.xml, 4.2)
	memset (pS, 0, 32);
	u32 nBase = t.Mem.Bus (TEX_TEXELS);			// (64-byte aligned; its low bits are flags)
	memcpy (pS, &nBase, 4);
	SetBits (pS, 58, 14, (u32) w);				// image width
	SetBits (pS, 72, 14, (u32) h);				// image height
	SetBits (pS, 86, 14, 1);				// depth
	SetBits (pS, 100, 7, 4);				// RGBA8
	SetBits (pS, 108, 3, 2); SetBits (pS, 111, 3, 3);	// swizzle R G B A
	SetBits (pS, 114, 3, 4); SetBits (pS, 117, 3, 5);
	if (L.nKind == TL_UIF)
	{
		SetBits (pS, 107, 1, 1);			// extended (Mesa sets it with a strictly-UIF level 0)
		SetBits (pS, 128, 4, L.nUbPad);			// level 0 UB_PAD
		SetBits (pS, 132, 1, L.bXor ? 1 : 0);		// level 0 XOR enable
		SetBits (pS, 134, 1, 1);			// level 0 is strictly UIF
	}
	CleanDataCacheRange ((uintptr) t.Mem.p, nBytes);
	return nHandle;
}

void V3DReleaseAS (CAddressSpace *pAS)
{
	if (pAS == 0) return;
	for (int i = 0; i < KAPI_GPU_MAX_TEXTURES; i++)
		if (s_Tex[i].pOwner == pAS)
		{
			while (s_bBusy) CScheduler::Get ()->Yield ();	// (not in the middle of a frame)
			FreeTex (s_Tex[i]);
		}
}

// ---- v53: a frame of batches -----------------------------------------------------------------------------------
static TGpuBuf s_Verts3;

static int Render (const kapi_gpu_frame &F, const kapi_gpu_vertex3 *pV, unsigned nV, const kapi_gpu_batch *pB, unsigned nB)
{
	int w = F.w, h = F.h;
	u32 tilesX = (u32) (w + 63) / 64, tilesY = (u32) (h + 63) / 64, nTiles = tilesX * tilesY;
	u32 nAllocSize = ((nTiles * 64 + 4095) & ~4095u) + 2 * 1024 * 1024;
	if (!Alloc (s_Verts3, (nV ? nV : 3) * sizeof (kapi_gpu_vertex3)) || !Alloc (s_Target, (u32) (w * h * 4))
	    || !Alloc (s_TileAlloc, nAllocSize) || !Alloc (s_TileState, nTiles * 256)
	    || !Alloc (s_BCL, 1024 + nB * 64) || !Alloc (s_Ind, 4096 + nB * 512))
		return -2;
	memcpy (s_Verts3.p, pV, nV * sizeof (kapi_gpu_vertex3));
	CleanDataCacheRange ((uintptr) s_Verts3.p, nV * sizeof (kapi_gpu_vertex3));
	boolean bKeep = (F.flags & KAPI_GPU_F_KEEP) != 0;
	TTarget T; ResolveTarget (F.pixels, w, h, F.stride, T);
	TargetBefore (T, F.pixels, w, h, F.stride, bKeep);

	f32 fXs = (f32) (w / 2) * 256.0f, fYs = (f32) (h / 2) * -256.0f;
	static const f32 Ident[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };

	CList Ind (s_Ind);
	CList B (s_BCL);
	B << TileBinningModeCfg (0, 0, 1, 0, false, false, (u16) w, (u16) h);
	B << OP_FLUSH_VCD_CACHE;
	B << OcclusionQueryCounter (0);
	B << OP_START_TILE_BINNING;
	B << ClipWindow (0, 0, (u16) w, (u16) h);
	B << PointSize (1.0f);
	B << LineWidth (1.0f);
	B << ClipperXYScaling (fXs, fYs);
	B << ClipperZScaleAndOffset (0.5f, 0.5f);
	B << CLipperZMinMaxClippingPlanes (0.0f, 1.0f);
	B << ViewportOffset ((f32) (w / 2), (f32) (h / 2), 0, 0);
	B << ColorWriteMasks (T.bDirect ? 0x8 : 0);		// (direct: alpha not written, stays 0)
	B << BlendConstantColor (0, 0, 0, 0);
	B << OP_ZERO_ALL_FLAT_SHADE_FLAGS;
	B << OP_ZERO_ALL_NON_PERSPECTIVE_FLAGS;
	B << OP_ZERO_ALL_CENTROID_FLAGS;
	B << TransformFeedbackSpecs (0, false);
	B << OcclusionQueryCounter (0);
	B << SampleState (0xF, 1.0f);
	B << VcmCacheSize (4, 4);
	u32 nPrevBlend = ~0u;

	for (unsigned i = 0; i < nB; i++)
	{
		const kapi_gpu_batch &b = pB[i];
		if (b.count == 0) continue;
		const f32 *M = (b.flags & KAPI_GPU_B_NOMATRIX) ? Ident : b.matrix;
		const TTexture *pTex = b.texture >= 0 ? &s_Tex[b.texture] : 0;

		// uniforms: vertex (matrix, x y scales, z scale / offset), coordinate (matrix, x y scales)
		Ind.Align (4);
		u32 nVtxUnif = Ind.Bus ();
		for (int k = 0; k < 16; k++) Ind << M[k];
		Ind << fXs << fYs << 0.5f << 0.5f;
		u32 nCoordUnif = Ind.Bus ();
		for (int k = 0; k < 16; k++) Ind << M[k];
		Ind << fXs << fYs;
		u32 nFragUnif = Ind.Bus ();
		if (pTex)
		{
			Ind.Align (32);						// the sampler state
			u32 nSampler = Ind.Bus ();
			u8 S[32]; memset (S, 0, sizeof S);
			boolean bLinear = (b.flags & KAPI_GPU_B_LINEAR) != 0;
			static const u32 Wrap[4] = { 0, 1, 2, 0 };		// repeat, clamp, mirror
			SetBits (S, 0, 1, bLinear ? 0 : 1);			// mag nearest
			SetBits (S, 1, 1, bLinear ? 0 : 1);			// min nearest
			SetBits (S, 2, 1, 1);					// mip nearest
			SetBits (S, 48, 3, Wrap[(b.flags >> 13) & 3]);
			SetBits (S, 51, 3, Wrap[(b.flags >> 15) & 3]);
			SetBits (S, 54, 3, 1);
			for (unsigned k = 0; k < sizeof S; k++) Ind << S[k];
			nFragUnif = Ind.Bus ();
			Ind << (u32) (pTex->Mem.Bus (0) | 3);			// p0: texture state, 2 words (f16 RG, BA)
			Ind << nSampler;					// p1: sampler state, 16-bit output
		}
		boolean bAT = (b.flags & (1u << 18)) != 0;			// (v54) the alpha test: its threshold
		if (bAT) Ind << (f32) ((b.flags >> 19) & 255) / 255.0f;
		if (!pTex && !bAT) Ind << 0u;

		// the shader state record + its attributes (position, s t, colour)
		Ind.Align (32);
		u32 nShaderRec = Ind.Bus ();
		GLShaderStateRecord Rec {};
		Rec.enable_clipping = true;
		Rec.fragment_shader_uses_real_pixel_centre_w_in_addition_to_centroid_w2 = true;
		Rec.disable_implicit_point_line_varyings = true;
		Rec.number_of_varyings_in_fragment_shader = 10;
		Rec.coordinate_shader_output_vpm_segment_size = 1;
		Rec.coordinate_shader_input_vpm_segment_size = 1;
		Rec.vertex_shader_output_vpm_segment_size = 2;
		Rec.vertex_shader_input_vpm_segment_size = 2;
		Rec.address_of_default_attribute_values = s_State.Bus (0);
		Rec.fragment_shader_code_address = s_State.Bus (pTex ? (bAT ? STATE_FS_TEX_AT : STATE_FS_TEX) : (bAT ? STATE_FS_COLOR_AT : STATE_FS_COLOR)) >> 3;
		Rec.fragment_shader_uniforms_address = nFragUnif;
		Rec.fragment_shader_4_way_threadable = true;
		Rec.fragment_shader_start_in_final_thread_section = false;
		Rec.fragment_shader_propagate_nans = true;
		Rec.vertex_shader_code_address = s_State.Bus (STATE_VS_CLIP) >> 3;
		Rec.vertex_shader_uniforms_address = nVtxUnif;
		Rec.vertex_shader_4_way_threadable = true;
		Rec.vertex_shader_start_in_final_thread_section = true;
		Rec.vertex_shader_propagate_nans = true;
		Rec.coordinate_shader_code_address = s_State.Bus (STATE_CS_CLIP) >> 3;
		Rec.coordinate_shader_uniforms_address = nCoordUnif;
		Rec.coordinate_shader_4_way_threadable = true;
		Rec.coordinate_shader_start_in_final_thread_section = true;
		Rec.coordinate_shader_propagate_nans = true;
		Ind << Rec;
		GlShaderStateAttributeRecord A {};
		A.address = s_Verts3.Bus (0);					// x y z w
		A.number_of_values_read_by_vertex_shader = 4;
		A.number_of_values_read_by_coordinate_shader = 4;
		A.stride = sizeof (kapi_gpu_vertex3);
		A.maximum_index = 0xFFFFFF;
		A.vec_size = 0;							// (4)
		A.type = 2;							// float
		Ind << A;
		A.address = s_Verts3.Bus (16);					// s t
		A.number_of_values_read_by_vertex_shader = 2;
		A.number_of_values_read_by_coordinate_shader = 0;
		A.vec_size = 2;
		Ind << A;
		A.address = s_Verts3.Bus (24);					// r g b a
		A.number_of_values_read_by_vertex_shader = 4;
		A.vec_size = 0;
		A.type = 4;							// byte
		A.normalized_int_type = true;
		Ind << A;
		A.address = s_Verts3.Bus (28);					// r2 g2 b2 a2 (v54)
		Ind << A;

		// the state, then the triangles
		u32 nZ = KAPI_GPU_B_ZFUNC (b.flags);
		if (nZ == 0) nZ = 1;						// LESS
		u32 nBlend = (b.flags >> 8) & 15;
		boolean bZWrite = !(b.flags & KAPI_GPU_B_NOZWRITE) && nZ != 7;
		B << CfgBits (!(b.flags & KAPI_GPU_B_CULL_FRONT), !(b.flags & KAPI_GPU_B_CULL_BACK), true, false, 0, 0, false,
			      nZ, bZWrite, false, false, false, nBlend != 0, false, false);
		if (nBlend != 0 && nBlend != nPrevBlend)
		{
			// factors: 0 zero, 1 one, 4 dst colour, 6 src alpha, 7 1 - src alpha; mode 0 add
			static const u8 Fac[5][4] = { { 1, 0, 1, 0 }, { 6, 7, 1, 7 }, { 6, 1, 6, 1 }, { 4, 0, 4, 0 }, { 1, 7, 1, 7 } };
			const u8 *f = Fac[nBlend <= 4 ? nBlend : 1];
			B << BlendEnables (1);
			B << BlendCfg (0, f[2], f[3], 0, f[0], f[1], 0xF);
			nPrevBlend = nBlend;
		}
		B << GlShaderState (nShaderRec, 4);
		B << VertexArrayPrims (4, b.count, b.first);			// triangles
	}
	B << OP_FLUSH;

	CList R (s_RCL);
	BuildRCL (R, Ind, w, h, F.clear, bKeep, T);
	if (B.Overflow () || R.Overflow () || Ind.Overflow ()) return -2;
	int nRes = Run (B, R, Ind, nAllocSize);
	if (nRes != 0) return nRes;

	TargetAfter (T, F.pixels, w, h, F.stride);
	return 0;
}

// ---- kapi ------------------------------------------------------------------------------------------------------
extern "C" int kapi_gpu_info (char *pBuf, unsigned nCap)
{
	boolean bUp = Up ();
	if (pBuf != 0 && nCap > 0)
	{
		unsigned i = 0; for (; s_Info[i] && i + 1 < nCap; i++) pBuf[i] = s_Info[i];
		pBuf[i] = 0;
	}
	return bUp ? 1 : 0;
}

extern "C" int kapi_gpu_draw (const kapi_gpu_vertex *pV, unsigned n, unsigned nClear, unsigned *pDst, int w, int h, int nStride)
{
	if (!Up ()) return -1;
	if (pDst == 0 || w <= 0 || h <= 0 || w > MAX_W || h > MAX_H || nStride < w
	    || n % 3 != 0 || n > KAPI_GPU_MAX_VERTS || (n && pV == 0))
		return -2;
	while (s_bBusy) CScheduler::Get ()->Yield ();		// one frame at a time
	s_bBusy = TRUE;
	CScheduler::Get ()->EnterNoKill ();			// (killed meanwhile: it ends after the frame)
	int r = s_nState > 0 ? Draw (pV, n, nClear, pDst, w, h, nStride) : -1;
	s_bBusy = FALSE;
	CScheduler::Get ()->LeaveNoKill ();
	return r;
}

extern "C" int kapi_gpu_texture (int nHandle, const unsigned *pPixels, int w, int h, int nStride)
{
	if (!Up ()) return -1;
	while (s_bBusy) CScheduler::Get ()->Yield ();		// (not while a frame reads them)
	s_bBusy = TRUE;
	CScheduler::Get ()->EnterNoKill ();
	CrashLogCrumb (CRUMB_V3D, 4);
	int r = s_nState > 0 ? Texture (nHandle, pPixels, w, h, nStride) : -1;
	CrashLogCrumb (CRUMB_V3D, 0);
	s_bBusy = FALSE;
	CScheduler::Get ()->LeaveNoKill ();
	return r;
}

// ---- the triangles made safe for the V3D ----------------------------------------------------------------------
// The V3D's own clipper is given only well-formed triangles: each one is transformed by its batch's
// matrix and clipped against the near plane (z >= -w, w > 0) and a guard band 4 x the screen (|x|,
// |y| <= 4 w); inside ones pass as they are, the matrix applied (the batch becomes NOMATRIX). An
// app's extreme coordinates -- Ocarina of Time's triangles crossing the camera plane, projected
// ~50000 screens away -- froze the whole Pi within seconds (the binner's fixed point overflowing:
// the GPU wedged, then the bus), while the software renderer, which clips them, never did.
// The frame's batches with their triangles transformed and clipped (NOMATRIX): into s_pClipV / s_pClipB.
static kapi_gpu_vertex3 *s_pClipV = 0; static unsigned s_nClipVCap = 0;
static kapi_gpu_batch *s_pClipB = 0; static unsigned s_nClipBCap = 0;

static boolean ClipFrame (const kapi_gpu_vertex3 *pV, unsigned nV, const kapi_gpu_batch *pB, unsigned nB,
			  unsigned *pOutV, unsigned *pOutB)
{
	unsigned nCap = nV * 2 + 64;					// (clipped triangles: at most 7 each; beyond: dropped)
	if (nCap > KAPI_GPU_MAX_VERTS) nCap = KAPI_GPU_MAX_VERTS;
	if (s_nClipVCap < nCap) { delete [] s_pClipV; s_pClipV = new kapi_gpu_vertex3[nCap]; s_nClipVCap = s_pClipV ? nCap : 0; }
	if (s_nClipBCap < nB + 1) { delete [] s_pClipB; s_pClipB = new kapi_gpu_batch[nB + 1]; s_nClipBCap = s_pClipB ? nB + 1 : 0; }
	if (s_pClipV == 0 || s_pClipB == 0) return FALSE;
	static const f32 Ident[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
	unsigned n = 0;
	for (unsigned i = 0; i < nB; i++)
	{
		const kapi_gpu_batch &b = pB[i];
		kapi_gpu_batch &o = s_pClipB[i];
		o = b; o.flags |= KAPI_GPU_B_NOMATRIX; o.first = n;
		for (int k = 0; k < 16; k++) o.matrix[k] = Ident[k];
		const f32 *M = (b.flags & KAPI_GPU_B_NOMATRIX) ? Ident : b.matrix;
		for (unsigned t = 0; t + 3 <= b.count && n + 21 <= nCap; t += 3)
		{
			kapi_gpu_vertex3 T[3], Out[21];
			for (int k = 0; k < 3; k++)
			{
				const kapi_gpu_vertex3 &p = pV[b.first + t + k];
				kapi_gpu_vertex3 &q = T[k];
				q = p;
				q.x = M[0] * p.x + M[1] * p.y + M[2] * p.z + M[3] * p.w;
				q.y = M[4] * p.x + M[5] * p.y + M[6] * p.z + M[7] * p.w;
				q.z = M[8] * p.x + M[9] * p.y + M[10] * p.z + M[11] * p.w;
				q.w = M[12] * p.x + M[13] * p.y + M[14] * p.z + M[15] * p.w;
			}
			unsigned k = V3DClipTriangle (T, Out);
			for (unsigned j = 0; j < k; j++) s_pClipV[n++] = Out[j];
		}
		o.count = n - o.first;
	}
	*pOutV = n; *pOutB = nB;
	return TRUE;
}

extern "C" int kapi_gpu_render (const kapi_gpu_frame *pF, const kapi_gpu_vertex3 *pV, unsigned nV,
				const kapi_gpu_batch *pB, unsigned nB)
{
	if (!Up ()) return -1;
	if (pF == 0 || pF->pixels == 0 || pF->w <= 0 || pF->h <= 0 || pF->w > MAX_W || pF->h > MAX_H
	    || pF->stride < pF->w || nV > KAPI_GPU_MAX_VERTS || (nV && pV == 0) || nB > KAPI_GPU_MAX_BATCHES || (nB && pB == 0))
		return -2;
	CAddressSpace *pAS = CurrentAS ();
	for (unsigned i = 0; i < nB; i++)				// each batch: its vertices, its texture
	{
		const kapi_gpu_batch &b = pB[i];
		if (b.count % 3 != 0 || b.first > nV || b.count > nV - b.first) return -2;
		if (b.texture >= KAPI_GPU_MAX_TEXTURES || (b.texture >= 0 && s_Tex[b.texture].pOwner != pAS) || b.texture < -1)
			return -2;
	}
	while (s_bBusy) CScheduler::Get ()->Yield ();		// one frame at a time
	s_bBusy = TRUE;
	CScheduler::Get ()->EnterNoKill ();
	unsigned nCV = 0, nCB = 0;
	CrashLogCrumb (CRUMB_V3D, 1);
	int r = s_nState <= 0 ? -1
	      : !ClipFrame (pV, nV, pB, nB, &nCV, &nCB) ? -4
	      : Render (*pF, s_pClipV, nCV, s_pClipB, nCB);
	CrashLogCrumb (CRUMB_V3D, 0);
	s_bBusy = FALSE;
	CScheduler::Get ()->LeaveNoKill ();
	return r;
}
