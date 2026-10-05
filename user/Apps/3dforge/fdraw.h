//
// 3dforge/fdraw.h -- the part as a flat drawing or a picture, seen from a side: what Export writes besides the meshes.
//
//   * A drawing: the bodies' edges (where two faces meet, and the outline of each curved face for this view) laid
//     flat in millimetres, at the part's own size. What a face hides is found with a depth picture of the bodies
//     (the z-buffer of frender.h): each edge is walked a pixel at a time and cut where it goes behind. The hidden
//     parts are dropped, or kept apart (drawn dashed).
//   * Written as DXF (lines, on the layers VISIBLE and HIDDEN), SVG, or PDF (Libs/pdf/pdfwrite.h) -- one unit a
//     millimetre, the page the drawing's size plus a margin.
//   * A picture: the bodies shaded as in the view, on white, as PNG.
//
// MIT licence (Onyx).
//
#ifndef _3dforge_fdraw_h
#define _3dforge_fdraw_h

#include "frender.h"
#include "pdf/pdfwrite.h"		// (and the PNG writer it includes: imagekit/img/pngsave.hpp)

namespace forge {

struct Seg2 { float x0, y0, x1, y1; bool hidden; };
struct Drawing
{
	std::vector<Seg2> segs; double w, h;		// millimetres; (0, 0) the bottom left corner, y up
	int nvis, nhid;
	Drawing () : w (0), h (0), nvis (0), nhid (0) {}
};

// The bodies to export (only >= 0: that one; else the shown ones) and a camera that frames them from (az, el), the
// longer side `longest` pixels: *ex, *ey their half sizes on the page, millimetres.
static bool export_cam (const Doc &d, int only, double az, double el, int longest, Cam *c, std::vector<const Body *> *list, double *ex, double *ey, double *radius)
{
	list->clear ();
	for (const Body &b : d.bodies)
	{
		bool vis = true; for (const BodyProp &p : d.props) if (p.id == b.id) vis = p.visible;
		if ((only >= 0 ? b.id == only : vis) && b.mesh.tris ()) list->push_back (&b);
	}
	if (list->empty ()) return false;
	V3 lo = (*list)[0]->mesh.lo, hi = (*list)[0]->mesh.hi;
	for (const Body *b : *list)
	{
		lo = V3 (std::min (lo.x, b->mesh.lo.x), std::min (lo.y, b->mesh.lo.y), std::min (lo.z, b->mesh.lo.z));
		hi = V3 (std::max (hi.x, b->mesh.hi.x), std::max (hi.y, b->mesh.hi.y), std::max (hi.z, b->mesh.hi.z));
	}
	c->az = az; c->el = el; c->set (); c->t = (lo + hi) * 0.5;
	*ex = *ey = 0;
	for (const Body *b : *list) for (const V3 &p : b->mesh.v)		// (the vertices themselves: a box of the box would be too wide)
	{
		V3 q = p - c->t; double x = fabs (dot (q, c->r)), y = fabs (dot (q, c->u));
		if (x > *ex) *ex = x; if (y > *ey) *ey = y;
	}
	if (*ex < 0.5) *ex = 0.5; if (*ey < 0.5) *ey = 0.5;
	c->scale = (longest - 8) / (2 * std::max (*ex, *ey));
	c->w = (int) (2 * *ex * c->scale) + 8; c->h = (int) (2 * *ey * c->scale) + 8;
	*radius = len (hi - lo) * 0.5 + 10;
	return true;
}

static bool make_drawing (const Doc &d, int only, double az, double el, bool hidden, Drawing &out)
{
	out.segs.clear (); out.nvis = out.nhid = 0;
	Cam c; std::vector<const Body *> list; double ex, ey, R;
	if (!export_cam (d, only, az, el, 1000, &c, &list, &ex, &ey, &R)) return false;
	out.w = 2 * ex; out.h = 2 * ey;
	Scene sc; sc.begin (c, R);
	sc.batch (KAPI_GPU_B_CULL_BACK, true);
	for (const Body *b : list) scene_body (sc, b->mesh, 0xFFFFFF, 255);
	sc.end (); soft_render (sc, 0);
	const double bias = 0.35 / sc.R;				// (a third of a millimetre: an edge lies on its own faces)
	auto seen = [&] (const V3 &p)
	{
		double x, y, z; sc.clip (p, 0, &x, &y, &z);
		int px = (int) ((x * 0.5 + 0.5) * sc.W), py = (int) ((0.5 - y * 0.5) * sc.H); float far = -2;
		for (int j = py - 1; j <= py + 1; j++) for (int i = px - 1; i <= px + 1; i++)
			if (i >= 0 && j >= 0 && i < sc.W && j < sc.H) { float zz = sc.zb[j * sc.W + i]; if (zz > far) far = zz; }
		return far < -1.5f || z <= far + bias;
	};
	auto flat = [&] (const V3 &p, float *x, float *y) { V3 q = p - c.t; *x = (float) (dot (q, c.r) + ex); *y = (float) (dot (q, c.u) + ey); };
	auto edge = [&] (const V3 &a, const V3 &b)
	{
		float x0, y0, x1, y1; flat (a, &x0, &y0); flat (b, &x1, &y1);
		double l2 = hypot (x1 - x0, y1 - y0);
		if (l2 < 1e-4) return;					// (seen end on)
		int n = (int) (l2 * c.scale * SS / 1.5) + 2; int start = 0; bool cur = false;
		for (int i = 0; i <= n; i++)
		{
			bool v = i < n ? seen (a + (b - a) * ((i + 0.5) / n)) : !cur;
			if (i == 0) { cur = v; continue; }
			if (v == cur) continue;
			if (cur || hidden)
			{
				float t0 = (float) start / n, t1 = (float) i / n;
				Seg2 s = { x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1, !cur };
				out.segs.push_back (s); if (cur) out.nvis++; else out.nhid++;
			}
			start = i; cur = v;
		}
	};
	for (const Body *b : list)
	{
		const RMesh &m = b->mesh;
		for (const Chain &ch : m.chains)
		{
			int n = (int) ch.pts.size ();
			for (int k = 0; k < (ch.closed ? n : n - 1); k++) edge (m.v[ch.pts[k]], m.v[ch.pts[(k + 1) % n]]);
		}
		for (size_t i = 0; i + 3 < m.smooth.size (); i += 4)
			if ((dot (m.n[m.smooth[i + 2]], c.d) > 0) != (dot (m.n[m.smooth[i + 3]], c.d) > 0)) edge (m.v[m.smooth[i]], m.v[m.smooth[i + 1]]);
	}
	return true;
}

static const double DRAW_MARGIN = 10;			// millimetres around the drawing
// A hidden line as dashes (2 mm, 1.5 mm apart): for what has no dashed stroke of its own.
template <class F> static void dashes (const Seg2 &s, F put)
{
	double l = hypot (s.x1 - s.x0, s.y1 - s.y0); if (l < 1e-6) return;
	for (double t = 0; t < l; t += 3.5)
	{
		double a = t / l, b = std::min (1.0, (t + 2) / l);
		put (s.x0 + (s.x1 - s.x0) * a, s.y0 + (s.y1 - s.y0) * a, s.x0 + (s.x1 - s.x0) * b, s.y0 + (s.y1 - s.y0) * b);
	}
}
static std::string drawing_svg (const Drawing &g)
{
	char b[240]; double W = g.w + 2 * DRAW_MARGIN, H = g.h + 2 * DRAW_MARGIN; std::string s;
	snprintf (b, sizeof b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.3fmm\" height=\"%.3fmm\" viewBox=\"0 0 %.3f %.3f\">\n", W, H, W, H); s += b;
	s += "<!-- 3DForge (Onyx): one unit a millimetre -->\n";
	for (int pass = 0; pass < 2; pass++)
	{
		bool hid = pass == 0; bool any = false;
		for (const Seg2 &q : g.segs)
		{
			if (q.hidden != hid) continue;
			if (!any) { s += hid ? "<g fill=\"none\" stroke=\"#808080\" stroke-width=\"0.18\" stroke-dasharray=\"2 1.5\" stroke-linecap=\"round\">\n" : "<g fill=\"none\" stroke=\"#000000\" stroke-width=\"0.25\" stroke-linecap=\"round\">\n"; any = true; }
			snprintf (b, sizeof b, "<path d=\"M%.4f %.4fL%.4f %.4f\"/>\n", q.x0 + DRAW_MARGIN, H - (q.y0 + DRAW_MARGIN), q.x1 + DRAW_MARGIN, H - (q.y1 + DRAW_MARGIN)); s += b;
		}
		if (any) s += "</g>\n";
	}
	s += "</svg>\n";
	return s;
}
// DXF as AutoCAD R12 reads it: the two layers, then a LINE an edge (millimetres, y up).
static std::string drawing_dxf (const Drawing &g)
{
	char b[240]; std::string s = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n";
	s += "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n2\n0\nLAYER\n2\nVISIBLE\n70\n0\n62\n7\n6\nCONTINUOUS\n0\nLAYER\n2\nHIDDEN\n70\n0\n62\n8\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n";
	s += "0\nSECTION\n2\nENTITIES\n";
	for (const Seg2 &q : g.segs)
	{
		snprintf (b, sizeof b, "0\nLINE\n8\n%s\n10\n%.5f\n20\n%.5f\n30\n0.0\n11\n%.5f\n21\n%.5f\n31\n0.0\n", q.hidden ? "HIDDEN" : "VISIBLE", q.x0, q.y0, q.x1, q.y1); s += b;
	}
	s += "0\nENDSEC\n0\nEOF\n";
	return s;
}
static std::string drawing_pdf (const Drawing &g, const char *title)
{
	const float K = 72.0f / 25.4f; float W = (float) (g.w + 2 * DRAW_MARGIN) * K, H = (float) (g.h + 2 * DRAW_MARGIN) * K;
	pdfw::Writer w; w.info (title, 0, 0, "3DForge (Onyx)"); w.begin_page (W, H);
	for (int pass = 0; pass < 2; pass++)
	{
		bool hid = pass == 0; std::vector<float> xy; std::vector<unsigned char> verb;
		auto put = [&] (double x0, double y0, double x1, double y1)
		{
			xy.push_back ((float) (x0 + DRAW_MARGIN) * K); xy.push_back (H - (float) (y0 + DRAW_MARGIN) * K); verb.push_back (0);
			xy.push_back ((float) (x1 + DRAW_MARGIN) * K); xy.push_back (H - (float) (y1 + DRAW_MARGIN) * K); verb.push_back (1);
		};
		for (const Seg2 &q : g.segs)
		{
			if (q.hidden != hid) continue;
			if (hid) dashes (q, put); else put (q.x0, q.y0, q.x1, q.y1);
		}
		if (!verb.empty ()) w.path (&xy[0], &verb[0], (int) verb.size (), hid ? 0x808080 : 0x000000, (hid ? 0.18f : 0.25f) * K);
	}
	w.end_page ();
	unsigned n = 0; unsigned char *p = w.finish (&n); std::string s ((const char *) p, n); delete[] p;
	return s;
}
// The bodies shaded, on white: a PNG `longest` pixels along its longer side. *pw, *ph: its size.
static std::string picture_png (const Doc &d, int only, double az, double el, int longest, int *pw, int *ph)
{
	Cam c; std::vector<const Body *> list; double ex, ey, R; *pw = *ph = 0;
	if (!export_cam (d, only, az, el, longest, &c, &list, &ex, &ey, &R)) return std::string ();
	Scene sc; sc.begin (c, R); sc.backdrop (0xFFFFFF, 0xFFFFFF);
	for (const Body *b : list)
	{
		unsigned col = BODY_COLOURS[0]; for (const BodyProp &p : d.props) if (p.id == b->id) col = p.colour;
		sc.batch (KAPI_GPU_B_CULL_BACK, true); scene_body (sc, b->mesh, col, 255);
		sc.batch (KAPI_GPU_B_ZFUNC (KAPI_GPU_Z_LEQUAL) | KAPI_GPU_B_NOZWRITE, false); scene_edges (sc, b->mesh, 0x222E42, 0.6);
	}
	std::vector<unsigned> px ((size_t) c.w * c.h);
	bool off = g_gpuOff; g_gpuOff = true; scene_show (sc, 0xFFFFFF, &px[0], c.w); g_gpuOff = off;	// (the processor: any size)
	unsigned n = 0; unsigned char *p = pngsave::png_encode (&px[0], c.w, c.h, false, &n);
	std::string s; if (p) { s.assign ((const char *) p, n); delete[] p; }
	*pw = c.w; *ph = c.h;
	return s;
}

} // namespace forge

#endif
