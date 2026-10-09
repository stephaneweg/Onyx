//
// nds/nds_render3d.cpp -- the 3D engine's rasterizer, in software: the polygons of the set the last
// swap gave (the opaque ones, then the translucent ones sorted by their top unless the game sorts
// them), a scanline at a time between their two edges, the colours, the texture coordinates and the
// w-buffer's depth interpolated with the perspective (the z-buffer's linearly); the seven texture
// formats (A3I5, 4 / 16 / 256 colours, 4x4 compressed, A5I3, direct), repeat / flip / clamp; the
// modulation, decal, toon and highlight modes; the alpha test and the blending (the same polygon ID
// not blended twice); the shadow polygons (a stencil); the wireframe; then the edge marking and the
// fog. The clear colour / depth, or the rear-plane bitmap. Integer only (it may run on another core).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

static const u32 A_OPAQUE_ID = 24, A_TRANS_ID = 16, A_FOG = 1u << 0, A_TRANS = 1u << 1, A_EDGE = 1u << 2;

void Render3D::reset (Machine *mm)
{
	m = mm;
	for (int i = 0; i < W * H; i++) { color[i] = 0; depth[i] = 0xFFFFFF; attr[i] = 0; stencil[i] = 0; }
	linesDone = H;
}

static inline u32 c5to6 (u32 c) { return (c << 1) | (c ? 1 : 0); }

// a texture's memory and its palettes' (16 KB pages)
static inline u8 texByte (Machine *m, u32 a) { u8 *p = m->ptrTexPage[(a >> 14) & 31]; return p ? p[a & 0x3FFF] : 0; }
static inline u16 texHalf (Machine *m, u32 a) { u8 *p = m->ptrTexPage[(a >> 14) & 31]; return p ? rd16 (p + (a & 0x3FFE)) : 0; }
static inline u16 palHalf (Machine *m, u32 a) { u8 *p = m->ptrTexPal[(a >> 14) & 7]; return p ? rd16 (p + (a & 0x3FFE)) : 0; }

// -> 0xAARRGGBB-ish: bits 0-17 colour (6-bit channels), bits 24-28 alpha (0 transparent)
u32 Render3D::sampleTex (const Polygon *p, s32 s, s32 t, u32 vcolor)
{
	(void) vcolor;
	u32 tp = p->texParam;
	int fmt = (int) (tp >> 26) & 7;
	s32 sw = 8 << ((tp >> 20) & 7), sh = 8 << ((tp >> 23) & 7);
	s32 u = s >> 4, v = t >> 4;
	if (tp & (1u << 16)) { if (tp & (1u << 18)) { u &= 2 * sw - 1; if (u >= sw) u = 2 * sw - 1 - u; } else u &= sw - 1; }
	else { if (u < 0) u = 0; else if (u >= sw) u = sw - 1; }
	if (tp & (1u << 17)) { if (tp & (1u << 19)) { v &= 2 * sh - 1; if (v >= sh) v = 2 * sh - 1 - v; } else v &= sh - 1; }
	else { if (v < 0) v = 0; else if (v >= sh) v = sh - 1; }
	u32 base = (tp & 0xFFFF) * 8;
	u32 texel = (u32) (v * sw + u);
	u32 palA = p->palBase * 16;
	bool c0t = tp & (1u << 29);
	u32 col15, a5 = 31;
	switch (fmt)
	{
	case 1:								// A3I5
	{
		u8 b = texByte (m, base + texel);
		u32 a3 = b >> 5; a5 = (a3 << 2) | (a3 >> 1);
		col15 = palHalf (m, palA + (b & 31) * 2);
		break;
	}
	case 2:								// 4 colours
	{
		u8 b = texByte (m, base + texel / 4);
		u32 i = (b >> ((texel & 3) * 2)) & 3;
		if (!i && c0t) return 0;
		col15 = palHalf (m, p->palBase * 8 + i * 2);
		break;
	}
	case 3:								// 16 colours
	{
		u8 b = texByte (m, base + texel / 2);
		u32 i = (texel & 1) ? b >> 4 : b & 15;
		if (!i && c0t) return 0;
		col15 = palHalf (m, palA + i * 2);
		break;
	}
	case 4:								// 256 colours
	{
		u32 i = texByte (m, base + texel);
		if (!i && c0t) return 0;
		col15 = palHalf (m, palA + i * 2);
		break;
	}
	case 5:								// 4x4 compressed
	{
		u32 blk = (u32) ((v >> 2) * (sw >> 2) + (u >> 2));
		u32 baddr = base + blk * 4;
		u32 bits = (u32) texByte (m, baddr + (u32) (v & 3));
		u32 i = (bits >> ((u & 3) * 2)) & 3;
		u32 slot1 = (baddr & 0x1FFFF) / 2 + ((baddr >= 0x40000) ? 0x10000 : 0) + 0x20000;
		u16 pinfo = texHalf (m, slot1 & ~1u);
		u32 pa = palA + (pinfo & 0x3FFF) * 4;
		int mode = pinfo >> 14;
		switch (i)
		{
		case 0: col15 = palHalf (m, pa); break;
		case 1: col15 = palHalf (m, pa + 2); break;
		case 2:
			if (mode == 0 || mode == 2) col15 = palHalf (m, pa + 4);
			else
			{
				u32 c0 = palHalf (m, pa), c1 = palHalf (m, pa + 2);
				u32 r0 = c0 & 31, g0 = (c0 >> 5) & 31, b0 = (c0 >> 10) & 31, r1 = c1 & 31, g1 = (c1 >> 5) & 31, b1 = (c1 >> 10) & 31;
				if (mode == 1) col15 = ((r0 + r1) >> 1) | (((g0 + g1) >> 1) << 5) | (((b0 + b1) >> 1) << 10);
				else col15 = ((r0 * 5 + r1 * 3) >> 3) | (((g0 * 5 + g1 * 3) >> 3) << 5) | (((b0 * 5 + b1 * 3) >> 3) << 10);
			}
			break;
		default:
			if (mode == 0 || mode == 1) return 0;
			if (mode == 2) col15 = palHalf (m, pa + 6);
			else
			{
				u32 c0 = palHalf (m, pa), c1 = palHalf (m, pa + 2);
				u32 r0 = c0 & 31, g0 = (c0 >> 5) & 31, b0 = (c0 >> 10) & 31, r1 = c1 & 31, g1 = (c1 >> 5) & 31, b1 = (c1 >> 10) & 31;
				col15 = ((r0 * 3 + r1 * 5) >> 3) | (((g0 * 3 + g1 * 5) >> 3) << 5) | (((b0 * 3 + b1 * 5) >> 3) << 10);
			}
			break;
		}
		break;
	}
	case 6:								// A5I3
	{
		u8 b = texByte (m, base + texel);
		a5 = b >> 3;
		col15 = palHalf (m, palA + (b & 7) * 2);
		break;
	}
	case 7:								// direct
	{
		u16 c = texHalf (m, base + texel * 2);
		if (!(c & 0x8000)) return 0;
		col15 = c;
		break;
	}
	default: return 0;
	}
	if (!a5) return 0;
	return c5to6 (col15 & 31) | (c5to6 ((col15 >> 5) & 31) << 6) | (c5to6 ((col15 >> 10) & 31) << 12) | (a5 << 24);
}

// ---- a polygon -------------------------------------------------------------------------------------------------------------
namespace {
struct EdgeVal { s32 x; s32 r, g, b, s, t; s64 z; s32 w; };		// x in 16.16

// the value at y along the edge a -> b (perspective-correct with w)
static void edgeAt (const Vertex *a, const Vertex *b, s32 wa, s32 wb, s32 y, bool wbuf, EdgeVal &o)
{
	s32 dy = b->sy - a->sy;
	s64 t = dy ? ((s64) (y - a->sy) << 16) / dy : 0;			// 0..65536, linear
	o.x = (s32) (((s64) a->sx << 16) + (((s64) (b->sx - a->sx) << 16) * t >> 16));
	s64 den = (65536 - t) * wb + t * wa;
	s64 tp = den ? (t * wa << 16) / den : t;				// the perspective weight of b
	o.r = a->cr + (s32) (((s64) (b->cr - a->cr) * tp) >> 16);
	o.g = a->cg + (s32) (((s64) (b->cg - a->cg) * tp) >> 16);
	o.b = a->cb + (s32) (((s64) (b->cb - a->cb) * tp) >> 16);
	o.s = a->s + (s32) (((s64) (b->s - a->s) * tp) >> 16);
	o.t = a->t + (s32) (((s64) (b->t - a->t) * tp) >> 16);
	o.w = den ? (s32) (((s64) wa * wb << 16) / den) : wa;
	if (wbuf) o.z = o.w;
	else o.z = a->sz + (((s64) (b->sz - a->sz) * t) >> 16);
}
}

void Render3D::drawPolygon (const Polygon *p)
{
	int n = p->nv;
	if (n < 3) return;
	const Gpu3D &g = m->gpu3d;
	u32 disp = g.rdDispcnt;
	bool wbuf = g.rdSwap & 2;
	u32 pattr = p->attr;
	u32 polyAlpha = (pattr >> 16) & 31;
	bool wire = polyAlpha == 0;
	if (wire) polyAlpha = 31;
	int mode = (int) (pattr >> 4) & 3;
	u32 pid = (pattr >> 24) & 63;
	bool texOn = (disp & 1) && ((p->texParam >> 26) & 7);
	bool shadowMask = mode == 3 && pid == 0;
	bool depthEq = pattr & 0x4000;
	bool transUpdate = pattr & 0x800;
	bool alphaTest = disp & 4;
	bool blend = disp & 8;
	u32 alphaRef = g.rdRegs[0x10] & 31;
	// w normalised to 16 bits for the perspective
	s32 maxw = 1;
	for (int i = 0; i < n; i++) { s32 w = p->v[i]->sw; if (w < 0) w = -w; if (w > maxw) maxw = w; }
	int wsh = 0; while ((maxw >> wsh) > 0xFFFF) wsh += 4;
	s32 wv[10];
	for (int i = 0; i < n; i++) { s32 w = p->v[i]->sw >> wsh; if (w <= 0) w = 1; wv[i] = w; }
	s32 ymin = p->ymin, ymax = p->ymax;
	bool flat = ymin == ymax;
	if (flat) ymax = ymin + 1;
	if (ymin < 0) ymin = 0;
	if (ymax > H) ymax = H;
	for (s32 y = ymin; y < ymax; y++)
	{
		// the two edges crossing this line
		EdgeVal e[2]; int ne = 0;
		if (flat)
		{
			int il = 0, ir = 0;
			for (int i = 1; i < n; i++) { if (p->v[i]->sx < p->v[il]->sx) il = i; if (p->v[i]->sx > p->v[ir]->sx) ir = i; }
			edgeAt (p->v[il], p->v[il], wv[il], wv[il], y, wbuf, e[0]);
			edgeAt (p->v[ir], p->v[ir], wv[ir], wv[ir], y, wbuf, e[1]);
			e[1].x += 0x10000;
			ne = 2;
		}
		else
			for (int i = 0; i < n && ne < 2; i++)
			{
				int j = (i + 1) % n;
				const Vertex *a = p->v[i], *b = p->v[j];
				s32 wa = wv[i], wb = wv[j];
				if (a->sy == b->sy) continue;
				if (a->sy > b->sy) { const Vertex *t = a; a = b; b = t; s32 tw = wa; wa = wb; wb = tw; }
				if (y < a->sy || y >= b->sy) continue;
				edgeAt (a, b, wa, wb, y, wbuf, e[ne++]);
			}
		if (ne < 2) continue;
		if (e[0].x > e[1].x) { EdgeVal t = e[0]; e[0] = e[1]; e[1] = t; }
		s32 xl = (e[0].x + 0x8000) >> 16, xr = (e[1].x + 0x8000) >> 16;
		if (xr == xl) xr = xl + 1;
		s32 span = xr - xl;
		s32 x0 = xl < 0 ? 0 : xl, x1 = xr > W ? W : xr;
		s32 wl = e[0].w > 0 ? e[0].w : 1, wr = e[1].w > 0 ? e[1].w : 1;
		for (s32 x = x0; x < x1; x++)
		{
			if (wire && !(x == xl || x == xr - 1 || y == ymin || y == ymax - 1)) continue;
			int idx = y * W + x;
			s64 t = span > 1 ? ((s64) (x - xl) << 16) / (span - 1) : 0;
			if (t > 65536) t = 65536;
			s64 den = (65536 - t) * wr + t * wl;
			s64 tp = den ? (t * wl << 16) / den : t;
			s64 z;
			if (wbuf) z = den ? ((s64) wl * wr << 16) / den : wl;
			else z = e[0].z + (((e[1].z - e[0].z) * t) >> 16);
			if (wbuf) z <<= wsh;
			if (z > 0xFFFFFF) z = 0xFFFFFF; if (z < 0) z = 0;
			// the depth test
			u32 dz = depth[idx];
			bool pass = depthEq ? ((s64) dz - z <= 0x200 && z - (s64) dz <= 0x200) : (u32) z < dz;
			if (shadowMask) { if (!pass) stencil[idx] = 1; continue; }
			if (!pass) continue;
			if (mode == 3)
			{
				if (!stencil[idx]) continue;
				stencil[idx] = 0;
				if (((attr[idx] >> A_OPAQUE_ID) & 63) == pid) continue;
			}
			s32 r = e[0].r + (s32) (((s64) (e[1].r - e[0].r) * tp) >> 16);
			s32 gg = e[0].g + (s32) (((s64) (e[1].g - e[0].g) * tp) >> 16);
			s32 b = e[0].b + (s32) (((s64) (e[1].b - e[0].b) * tp) >> 16);
			u32 a = polyAlpha;
			if (mode == 2)						// toon / highlight: the table by the red
			{
				u32 tc = g.toonTable[(r >> 1) & 31];
				s32 tr = (s32) c5to6 (tc & 31), tg = (s32) c5to6 ((tc >> 5) & 31), tb = (s32) c5to6 ((tc >> 10) & 31);
				if (disp & 2) { r += tr; gg += tg; b += tb; }	// (highlight: added after the texture below)
				else { r = tr; gg = tg; b = tb; }
			}
			if (texOn)
			{
				s32 s = e[0].s + (s32) (((s64) (e[1].s - e[0].s) * tp) >> 16);
				s32 tt = e[0].t + (s32) (((s64) (e[1].t - e[0].t) * tp) >> 16);
				u32 tx = sampleTex (p, s, tt, 0);
				u32 ta = tx >> 24;
				if (!ta) continue;
				s32 tr = (s32) (tx & 63), tg = (s32) ((tx >> 6) & 63), tb = (s32) ((tx >> 12) & 63);
				if (mode == 1)					// decal
				{
					if (ta == 31) { r = tr; gg = tg; b = tb; }
					else { r = (tr * (s32) ta + r * (31 - (s32) ta)) >> 5; gg = (tg * (s32) ta + gg * (31 - (s32) ta)) >> 5; b = (tb * (s32) ta + b * (31 - (s32) ta)) >> 5; }
				}
				else
				{
					if (mode == 2 && (disp & 2))
					{
						// highlight: the vertex colour modulates, the toon colour is added
						s32 vr = e[0].r + (s32) (((s64) (e[1].r - e[0].r) * tp) >> 16);
						s32 vg = e[0].g + (s32) (((s64) (e[1].g - e[0].g) * tp) >> 16);
						s32 vb = e[0].b + (s32) (((s64) (e[1].b - e[0].b) * tp) >> 16);
						s32 hr = r - vr, hg = gg - vg, hb = b - vb;
						r = (((tr + 1) * (vr + 1) - 1) >> 6) + hr; gg = (((tg + 1) * (vg + 1) - 1) >> 6) + hg; b = (((tb + 1) * (vb + 1) - 1) >> 6) + hb;
					}
					else
					{
						r = ((tr + 1) * (r + 1) - 1) >> 6; gg = ((tg + 1) * (gg + 1) - 1) >> 6; b = ((tb + 1) * (b + 1) - 1) >> 6;
					}
					a = ((ta + 1) * (a + 1) - 1) >> 5;
				}
			}
			if (r > 63) r = 63; if (gg > 63) gg = 63; if (b > 63) b = 63;
			if (r < 0) r = 0; if (gg < 0) gg = 0; if (b < 0) b = 0;
			if (!a) continue;
			if (alphaTest && a <= alphaRef) continue;
			u32 rgb = (u32) r | ((u32) gg << 6) | ((u32) b << 12);
			if (a == 31)
			{
				color[idx] = rgb | (31u << 24);
				depth[idx] = (u32) z;
				attr[idx] = (pid << A_OPAQUE_ID) | ((pattr & 0x8000) ? A_FOG : 0) | ((x == xl || x == xr - 1 || y == ymin || y == ymax - 1) ? A_EDGE : 0);
			}
			else
			{
				u32 da = attr[idx];
				if ((da & A_TRANS) && ((da >> A_TRANS_ID) & 63) == pid) continue;	// (the same polygon ID: once)
				u32 dc = color[idx], dA = (dc >> 24) & 31;
				if (blend && dA)
				{
					u32 dr = dc & 63, dg = (dc >> 6) & 63, db = (dc >> 12) & 63;
					u32 nr = ((u32) r * (a + 1) + dr * (31 - a)) >> 5, ng = ((u32) gg * (a + 1) + dg * (31 - a)) >> 5, nb = ((u32) b * (a + 1) + db * (31 - a)) >> 5;
					rgb = nr | (ng << 6) | (nb << 12);
					if (dA > a) a = dA;
				}
				color[idx] = rgb | (a << 24);
				if (transUpdate) depth[idx] = (u32) z;
				attr[idx] = (da & ~(63u << A_TRANS_ID) & ~A_FOG) | (pid << A_TRANS_ID) | A_TRANS | ((da & A_FOG) && (pattr & 0x8000) ? A_FOG : 0);
			}
		}
	}
}

void Render3D::finish ()
{
	const Gpu3D &g = m->gpu3d;
	u32 disp = g.rdDispcnt;
	const u8 *R = g.rdRegs;
	if (disp & 0x20)							// edge marking
	{
		u32 clearD = 0x7FFF * 0x200;
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				int i = y * W + x;
				u32 a = attr[i];
				if (!(a & A_EDGE) || !(color[i] >> 24)) continue;
				u32 pid = (a >> A_OPAQUE_ID) & 63, d = depth[i];
				bool edge = false;
				const int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
				for (int k = 0; k < 4 && !edge; k++)
				{
					int nx = x + dx[k], ny = y + dy[k];
					u32 npid, nd;
					if (nx < 0 || ny < 0 || nx >= W || ny >= H) { npid = (rd32 (R + 0x20) >> 24) & 63; nd = clearD; }
					else { npid = (attr[ny * W + nx] >> A_OPAQUE_ID) & 63; nd = depth[ny * W + nx]; }
					if (npid != pid && d < nd) edge = true;
				}
				if (edge)
				{
					u32 ec = rd16 (R + (pid >> 3) * 2);
					color[i] = (color[i] & 0xFF000000) | c5to6 (ec & 31) | (c5to6 ((ec >> 5) & 31) << 6) | (c5to6 ((ec >> 10) & 31) << 12);
				}
			}
	}
	if (disp & 0x80)							// fog
	{
		u32 fogShift = (disp >> 8) & 15;
		u32 fogOff = (u32) (rd16 (R + 0x2C) & 0x7FFF) * 0x200;
		u32 fc = rd32 (R + 0x28);
		u32 fr = c5to6 (fc & 31), fg = c5to6 ((fc >> 5) & 31), fb = c5to6 ((fc >> 10) & 31), fa = (fc >> 16) & 31;
		u8 tbl[34];
		tbl[0] = R[0x30] & 0x7F;
		for (int i = 0; i < 32; i++) tbl[i + 1] = R[0x30 + i] & 0x7F;
		tbl[33] = R[0x30 + 31] & 0x7F;
		bool alphaOnly = disp & 0x40;
		for (int i = 0; i < W * H; i++)
		{
			if (!(attr[i] & A_FOG)) continue;
			u32 z = depth[i], id, frac;
			if (z < fogOff) { id = 0; frac = 0; }
			else
			{
				u64 zz = (u64) ((z - fogOff) >> 2) << fogShift;
				id = (u32) (zz >> 17);
				if (id >= 32) { id = 32; frac = 0; } else frac = (u32) (zz & 0x1FFFF);
			}
			u32 dens = ((u32) tbl[id] * (0x20000 - frac) + (u32) tbl[id + 1] * frac) >> 17;
			if (dens >= 127) dens = 128;
			u32 c = color[i];
			u32 a = (c >> 24) & 31;
			u32 na = (fa * dens + a * (128 - dens)) >> 7;
			if (alphaOnly) color[i] = (c & 0x3FFFF) | (na << 24);
			else
			{
				u32 r = (fr * dens + (c & 63) * (128 - dens)) >> 7, gg = (fg * dens + ((c >> 6) & 63) * (128 - dens)) >> 7, b = (fb * dens + ((c >> 12) & 63) * (128 - dens)) >> 7;
				color[i] = r | (gg << 6) | (b << 12) | (na << 24);
			}
		}
	}
}

void Render3D::renderFrame ()
{
	linesDone = 0;
	Gpu3D &g = m->gpu3d;
	const u8 *R = g.rdRegs;
	u32 disp = g.rdDispcnt;
	// the clear
	u32 cc = rd32 (R + 0x20);
	u32 crgb = c5to6 (cc & 31) | (c5to6 ((cc >> 5) & 31) << 6) | (c5to6 ((cc >> 10) & 31) << 12);
	u32 ca = (cc >> 16) & 31, cid = (cc >> 24) & 63;
	u32 cd = rd16 (R + 0x24) & 0x7FFF;
	u32 cdepth = cd * 0x200 + ((cd + 1) >> 15) * 0x1FF;
	u32 cattr = (cid << A_OPAQUE_ID) | ((cc & 0x8000) ? A_FOG : 0);
	if (disp & 0x4000)							// the rear-plane bitmap (texture slots 2 and 3)
	{
		u32 off = rd16 (R + 0x26);
		int ox = (int) (off & 0xFF), oy = (int) ((off >> 8) & 0xFF);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				u32 a = (u32) (((oy + y) & 255) * 256 + ((ox + x) & 255)) * 2;
				u16 c = texHalf (m, 0x40000 + a), d = texHalf (m, 0x60000 + a);
				int i = y * W + x;
				color[i] = c5to6 (c & 31) | (c5to6 ((c >> 5) & 31) << 6) | (c5to6 ((c >> 10) & 31) << 12) | ((c & 0x8000) ? 31u << 24 : 0);
				u32 dd = d & 0x7FFF;
				depth[i] = dd * 0x200 + ((dd + 1) >> 15) * 0x1FF;
				attr[i] = (cid << A_OPAQUE_ID) | ((d & 0x8000) ? A_FOG : 0);
				stencil[i] = 0;
			}
	}
	else
		for (int i = 0; i < W * H; i++) { color[i] = crgb | (ca << 24); depth[i] = cdepth; attr[i] = cattr; stencil[i] = 0; }
	int set = g.rdSet;
	int np = g.nPolys[set];
	const Polygon *polys = g.pram[set];
	// the opaque ones, then the translucent ones (sorted by their top, then their bottom, unless manual)
	static const Polygon *order[Gpu3D::MAX_POLYS];
	int no = 0;
	for (int i = 0; i < np; i++) if (!polys[i].translucent) order[no++] = &polys[i];
	int nt0 = no;
	for (int i = 0; i < np; i++) if (polys[i].translucent) order[no++] = &polys[i];
	if (!(g.rdSwap & 1))
		for (int i = nt0 + 1; i < no; i++)		// (insertion sort: stable)
		{
			const Polygon *p = order[i]; int j = i - 1;
			while (j >= nt0 && (order[j]->ymin > p->ymin || (order[j]->ymin == p->ymin && order[j]->ymax > p->ymax))) { order[j + 1] = order[j]; j--; }
			order[j + 1] = p;
		}
	// the opaque ones sorted by their top too (the hardware's order)
	for (int i = 1; i < nt0; i++)
	{
		const Polygon *p = order[i]; int j = i - 1;
		while (j >= 0 && (order[j]->ymin > p->ymin || (order[j]->ymin == p->ymin && order[j]->ymax > p->ymax))) { order[j + 1] = order[j]; j--; }
		order[j + 1] = p;
	}
	for (int i = 0; i < no; i++) drawPolygon (order[i]);
	finish ();
	__atomic_store_n (&linesDone, H, __ATOMIC_RELEASE);
}

} // namespace nds
