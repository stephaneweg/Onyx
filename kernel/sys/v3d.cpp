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
#include <kern/kapi_abi.h>
#include <kern/v3d_cl.h>
#include <kern/v3d.h>
#include <circle/memio.h>
#include <circle/bcm2835.h>
#include <circle/string.h>
#include <circle/bcmpropertytags.h>
#include <circle/memory.h>
#include <circle/synchronize.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/util.h>
#include <circle/types.h>

static const char From[] = "v3d";

// ---- registers --------------------------------------------------------------------------------------
#define V3D_HUB			(ARM_IO_BASE + 0xC00000)
#define V3D_HUB_IDENT0		(V3D_HUB + 0x08)
#define V3D_HUB_IDENT1		(V3D_HUB + 0x0C)
#define V3D_HUB_IDENT2		(V3D_HUB + 0x10)
#define V3D_CORE0		(ARM_IO_BASE + 0xC04000)
#define V3D_CTL_IDENT0		(V3D_CORE0 + 0x000)
#define V3D_CTL_SLCACTL		(V3D_CORE0 + 0x024)
#define V3D_CTL_L2TCACTL	(V3D_CORE0 + 0x030)
#define V3D_CTL_L2TFLSTA	(V3D_CORE0 + 0x034)
#define V3D_CTL_L2TFLEND	(V3D_CORE0 + 0x038)
#define V3D_CTL_INT_STS		(V3D_CORE0 + 0x050)
#define V3D_CTL_INT_CLR		(V3D_CORE0 + 0x058)
#define   V3D_INT_OUTOMEM	(1 << 2)
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
	CleanDataCacheRange ((uintptr) p, 2048);
	Fmt (s_Info, sizeof s_Info, "V3D %u.%u (%u core)", nTver, nRev, nCores);
	CLogger::Get ()->Write (From, LogNotice, "%s ready", s_Info);
	s_nState = 1;
	return TRUE;
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

static int Draw (const kapi_gpu_vertex *pV, unsigned n, unsigned nClear, unsigned *pDst, int w, int h, int nStride)
{
	u32 tilesX = (u32) (w + 63) / 64, tilesY = (u32) (h + 63) / 64, nTiles = tilesX * tilesY;
	u32 nAllocSize = ((nTiles * 64 + 4095) & ~4095u) + 2 * 1024 * 1024;
	if (!Alloc (s_Verts, (n ? n : 3) * sizeof (kapi_gpu_vertex)) || !Alloc (s_Target, (u32) (w * h * 4))
	    || !Alloc (s_TileAlloc, nAllocSize) || !Alloc (s_TileState, nTiles * 256))
		return -2;
	memcpy (s_Verts.p, pV, n * sizeof (kapi_gpu_vertex));
	CleanDataCacheRange ((uintptr) s_Verts.p, n * sizeof (kapi_gpu_vertex));
	CleanAndInvalidateDataCacheRange ((uintptr) s_Target.p, (u32) (w * h * 4));	// (no dirty line over the GPU's output)

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
	B << ColorWriteMasks (0);
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

	// ---- the rendering list
	u32 nClearRGBA = 0xFF000000 | ((nClear & 0xFF) << 16) | (nClear & 0xFF00) | ((nClear >> 16) & 0xFF);
	CList R (s_RCL);
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
	Ind << OP_END_OF_LOADS;
	Ind << PrimListFormat (LIST_TRIANGLES, false);
	Ind << BranchToImplicitTileList (0);
	Ind << StoreTileBufferGeneral (BUFFER_RENDER_TARGET_0, V3D_TILING_RASTER, false, V3D_DITHER_MODE_NONE,
				       V3D_DECIMATE_MODE_SAMPLE_0, V3D_OUTPUT_IMAGE_FORMAT_RGBA8, false, false, false,
				       (u32) (w * 4), 0, s_Target.Bus ());
	Ind << ClearTileBuffers (true, true);
	Ind << OP_END_OF_TILE_MARKER;
	Ind << OP_RETURN_FROM_SUB_LIST;
	R << StartAddressOfGenericTileList (nGeneric, Ind.Bus ());
	for (u32 y = 0; y <= (u32) (h - 1) / (64 * stH); y++)
		for (u32 x = 0; x <= (u32) (w - 1) / (64 * stW); x++)
			R << SupertileCoorinates ((u8) x, (u8) y);
	R << OP_END_OF_RENDERING;
	if (B.Overflow () || R.Overflow () || Ind.Overflow ()) return -2;

	CleanDataCacheRange ((uintptr) s_BCL.p, B.Size ());
	CleanDataCacheRange ((uintptr) s_RCL.p, R.Size ());
	CleanDataCacheRange ((uintptr) s_Ind.p, Ind.Size ());
	DataSyncBarrier ();

	// ---- bin
	InvalidateGpuCaches ();
	write32 (V3D_CTL_INT_CLR, V3D_INT_OUTOMEM);
	u32 nBfc = read32 (V3D_CLE_BFC) & 0xFF;
	write32 (V3D_CLE_CT0QMA, s_TileAlloc.Bus ());
	write32 (V3D_CLE_CT0QMS, nAllocSize);
	write32 (V3D_CLE_CT0QTS, s_TileState.Bus () | (1 << 1));
	write32 (V3D_CLE_CT0QBA, B.Start ());
	write32 (V3D_CLE_CT0QEA, B.Start () + B.Size ());
	unsigned t0 = CTimer::Get ()->GetClockTicks (), nChunk = 0;
	while ((read32 (V3D_CLE_BFC) & 0xFF) == nBfc)
	{
		if ((read32 (V3D_CTL_INT_STS) & V3D_INT_OUTOMEM) && nChunk < OVERFLOW_CHUNKS)	// more tile-list memory
		{
			write32 (V3D_PTB_BPOA, s_Overflow.Bus (nChunk++ * OVERFLOW_CHUNK));
			write32 (V3D_PTB_BPOS, OVERFLOW_CHUNK);
			write32 (V3D_CTL_INT_CLR, V3D_INT_OUTOMEM);
		}
		if (CTimer::Get ()->GetClockTicks () - t0 > TIMEOUT_US) { Fail ("binning"); return -3; }
		CScheduler::Get ()->Yield ();
	}

	// ---- render
	InvalidateGpuCaches ();
	u32 nRfc = read32 (V3D_CLE_RFC) & 0xFF;
	write32 (V3D_CLE_CT1QBA, R.Start ());
	write32 (V3D_CLE_CT1QEA, R.Start () + R.Size ());
	t0 = CTimer::Get ()->GetClockTicks ();
	while ((read32 (V3D_CLE_RFC) & 0xFF) == nRfc)
	{
		if (CTimer::Get ()->GetClockTicks () - t0 > TIMEOUT_US) { Fail ("rendering"); return -3; }
		CScheduler::Get ()->Yield ();
	}

	// ---- the picture: RGBA8 (R first in memory) -> 0x00RRGGBB
	InvalidateDataCacheRange ((uintptr) s_Target.p, (u32) (w * h * 4));
	for (int y = 0; y < h; y++)
	{
		const u32 *s = (const u32 *) (s_Target.p + (u32) y * (u32) w * 4);
		unsigned *d = pDst + (long) y * nStride;
		for (int x = 0; x < w; x++)
		{
			u32 c = s[x];
			d[x] = ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
		}
	}
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
