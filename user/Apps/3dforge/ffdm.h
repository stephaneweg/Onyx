//
// ffdm.h -- 3DForge, Manufacture for a filament printer: the path of the nozzle, layer by layer, whatever the printer
// (what a printer wants of it -- its G-code's start and end, its speeds -- is a writer's business: fdm_gcode is a
// plain one, Marlin's words). Portable (the PC tests it: tools/tests/3dforge/fdmtest.cpp).
//
//   * A layer is the body's section at its middle height (Manifold's Slice).
//   * Walls: the section's outline pushed in by half a line's width, then a width more for each wall (Clipper2).
//   * Inside the walls, a place is filled solid where the body does not go on for `top` layers above it or `bottom`
//     layers below it (the section less what all those layers share), and sparsely elsewhere: straight lines, a
//     line's width apart (solid) or wider (the infill's share), at 45 degrees one way then the other.
//   * The first layer has a skirt: loops around everything, a little away.
//   * What is pushed out along a path is its length x the line's width x the layer's height; the filament it takes
//     follows from the filament's diameter.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef FORGE_FFDM_H
#define FORGE_FFDM_H

#include "fdoc.h"

namespace forge {

struct FdmSettings
{
	double layer, width;			// a layer's height, a line's width (mm)
	double walls, top, bottom;		// how many walls; solid layers above and below
	double infill;				// the share filled inside (0 .. 1)
	int pattern;				// ... as: 0 lines, one way a layer then the other; 1 a grid, both ways each layer
	double angle;				// ... turned by (degrees; 45 at first)
	double skirt, skirtGap;			// the skirt's loops, how far from the part
	double filament;			// the filament's diameter
	FdmSettings () : layer (0.2), width (0.45), walls (2), top (4), bottom (4), infill (0.2), pattern (0), angle (45), skirt (2), skirtGap (5), filament (1.75) {}
};
enum { FDM_OUTER, FDM_INNER, FDM_SOLID, FDM_SPARSE, FDM_SKIRT };
struct FdmPath { std::vector<V2> pts; bool closed; unsigned char kind; };
struct FdmLayer { double z; std::vector<FdmPath> paths; };
struct FdmJob
{
	std::vector<FdmLayer> layers; V3 lo, hi;
	double length, volume, filament;	// mm drawn, mm3 pushed out, mm of filament
	char err[72];
	FdmJob () : length (0), volume (0), filament (0) { err[0] = 0; }
};

static void fdm_loops (const CrossSection &cs, int kind, std::vector<FdmPath> &out)
{
	for (const SimplePolygon &p : cs.ToPolygons ())
	{
		if (p.size () < 3) continue;
		FdmPath f; f.closed = true; f.kind = (unsigned char) kind; for (const auto &q : p) f.pts.push_back (V2 (q.x, q.y));
		out.push_back (f);
	}
}
// Straight lines `gap` apart at `deg` degrees across the region, one after the other there and back.
static void fdm_lines (const CrossSection &region, double gap, double deg, int kind, std::vector<FdmPath> &out)
{
	Polygons ps = region.ToPolygons (); if (ps.empty () || gap < 0.01) return;
	double a = deg * PI / 180, c = cos (a), s = sin (a), ymin = 1e30, ymax = -1e30;
	struct Edge { double x0, y0, x1, y1; }; std::vector<Edge> edges;
	for (const SimplePolygon &p : ps)
		for (size_t i = 0, j = p.size () - 1; i < p.size (); j = i++)
		{
			// (turned by -deg: the lines are level there)
			Edge e = { p[j].x * c + p[j].y * s, -p[j].x * s + p[j].y * c, p[i].x * c + p[i].y * s, -p[i].x * s + p[i].y * c };
			if (e.y0 == e.y1) continue;
			edges.push_back (e); ymin = std::min (ymin, std::min (e.y0, e.y1)); ymax = std::max (ymax, std::max (e.y0, e.y1));
		}
	std::vector<double> xs; bool back = false;
	for (double y = (floor (ymin / gap) + 0.5) * gap; y < ymax; y += gap, back = !back)
	{
		xs.clear ();
		for (const Edge &e : edges) if ((e.y0 <= y) != (e.y1 <= y)) xs.push_back (e.x0 + (e.x1 - e.x0) * (y - e.y0) / (e.y1 - e.y0));
		std::sort (xs.begin (), xs.end ());
		std::vector<FdmPath> row;
		for (size_t i = 0; i + 1 < xs.size (); i += 2)
		{
			if (xs[i + 1] - xs[i] < gap * 0.2) continue;
			FdmPath f; f.closed = false; f.kind = (unsigned char) kind;
			f.pts.push_back (V2 (xs[i] * c - y * s, xs[i] * s + y * c)); f.pts.push_back (V2 (xs[i + 1] * c - y * s, xs[i + 1] * s + y * c));
			row.push_back (f);
		}
		if (back) { std::reverse (row.begin (), row.end ()); for (FdmPath &f : row) std::swap (f.pts[0], f.pts[1]); }
		for (FdmPath &f : row) out.push_back (f);
	}
}
// The nozzle's path for `solid` (anywhere: its box's underside is the first layer's). progress (done, of): false stops.
template <class F> static bool fdm_paths (const Manifold &solid, const FdmSettings &s, FdmJob &job, F progress)
{
	job = FdmJob ();
	if (solid.IsEmpty () || s.layer < 0.02 || s.width < 0.05) { snprintf (job.err, sizeof job.err, "There is nothing to print"); return false; }
	auto box = solid.BoundingBox (); double w = s.width, h = s.layer;
	int n = (int) ceil ((box.max.z - box.min.z) / h - 1e-6), walls = (int) (s.walls + 0.5), top = (int) (s.top + 0.5), bottom = (int) (s.bottom + 0.5);
	if (n < 1) n = 1; if (walls < 1) walls = 1;
	job.lo = V3 (box.min.x, box.min.y, 0); job.hi = V3 (box.max.x, box.max.y, n * h);
	const auto RND = CrossSection::JoinType::Round;
	// the sections, and what is inside the walls
	std::vector<CrossSection> sec (n), in (n);
	for (int i = 0; i < n; i++)
	{
		if (!progress (i, 2 * n)) return false;
		sec[i] = CrossSection (solid.Slice (box.min.z + (i + 0.5) * h), CrossSection::FillRule::Positive);
		in[i] = sec[i].Offset (-walls * w, RND, 2.0, 24);
	}
	double sparse_gap = s.infill > 0.01 ? w / (s.infill > 1 ? 1 : s.infill) : 0;
	job.layers.resize (n);
	for (int i = 0; i < n; i++)
	{
		if (!progress (n + i, 2 * n)) return false;
		FdmLayer &l = job.layers[i]; l.z = (i + 1) * h;
		if (i == 0 && s.skirt >= 0.5)
			for (int k = 0; k < (int) (s.skirt + 0.5); k++) fdm_loops (sec[0].Hull ().Offset (s.skirtGap + k * w, RND, 2.0, 32), FDM_SKIRT, l.paths);
		for (int k = walls - 1; k >= 0; k--)				// (the inner walls first, the one seen last)
			fdm_loops (sec[i].Offset (-(k + 0.5) * w, RND, 2.0, 24), k ? FDM_INNER : FDM_OUTER, l.paths);
		if (in[i].IsEmpty ()) continue;
		// solid where the body stops within `top` layers above or `bottom` below
		CrossSection solid_part;
		if (i < bottom || i >= n - top) solid_part = in[i];
		else
		{
			CrossSection above = sec[i + 1], below = sec[i - 1];
			for (int k = 2; k <= top; k++) above = above ^ sec[i + k];
			for (int k = 2; k <= bottom; k++) below = below ^ sec[i - k];
			CrossSection open = (in[i] - above) + (in[i] - below);
			if (open.Area () > w * w) solid_part = open.Offset (w, RND, 2.0, 12) ^ in[i];	// (a line more: it holds on what is beside)
		}
		double deg = s.angle + (i % 2 ? 90 : 0);
		// (the lines go a little under the inner wall: they hold on it)
		if (!solid_part.IsEmpty ()) fdm_lines (solid_part.Offset (w * 0.15, RND, 2.0, 12), w, deg, FDM_SOLID, l.paths);
		if (sparse_gap > 0)
		{
			CrossSection sparse = solid_part.IsEmpty () ? in[i] : in[i] - solid_part;
			if (sparse.Area () > w * w)
			{
				CrossSection r = sparse.Offset (w * 0.15, RND, 2.0, 12);
				if (s.pattern == 1) { fdm_lines (r, sparse_gap * 2, s.angle, FDM_SPARSE, l.paths); fdm_lines (r, sparse_gap * 2, s.angle + 90, FDM_SPARSE, l.paths); }
				else fdm_lines (r, sparse_gap, deg, FDM_SPARSE, l.paths);
			}
		}
	}
	for (const FdmLayer &l : job.layers) for (const FdmPath &p : l.paths)
		for (size_t k = 1; k < p.pts.size () + (p.closed ? 1 : 0); k++) { const V2 &a = p.pts[k - 1], &b = p.pts[k % p.pts.size ()]; job.length += hypot (b.x - a.x, b.y - a.y); }
	job.volume = job.length * w * h; job.filament = job.volume / (PI * s.filament * s.filament / 4);
	return true;
}

// ---- a plain G-code (Marlin's words): what any writer for a printer starts from ----------------------------------------
struct FdmMachine
{
	char name[32]; double bedX, bedY, sizeZ;
	double nozzleT, bedT;			// degrees
	double speed, firstSpeed, travel;	// mm/s
	double retract, retractSpeed;		// mm, mm/s
	double fan;				// 0 .. 100, from the third layer
};
static const FdmMachine FDM_MACHINE0 = { "Generic Marlin printer", 220, 220, 250, 210, 60, 60, 25, 150, 0.8, 35, 100 };
// The paths as G-code: the body's box put on the bed's middle. *minutes: its time (the moves only).
static std::string fdm_gcode (const FdmJob &j, const FdmSettings &s, const FdmMachine &m, const char *title, double *minutes)
{
	std::string g; char b[160]; double e = 0, secs = 0, per = s.width * s.layer / (PI * s.filament * s.filament / 4);
	double ox = m.bedX / 2 - (j.lo.x + j.hi.x) / 2, oy = m.bedY / 2 - (j.lo.y + j.hi.y) / 2;
	auto put = [&] (const char *t) { g += t; g += '\n'; };
	snprintf (b, sizeof b, "; 3DForge - %s", title ? title : "part"); put (b);
	snprintf (b, sizeof b, "; %s - layer %.3g mm, line %.3g mm, %d layers, %.2f m of filament", m.name, s.layer, s.width, (int) j.layers.size (), j.filament / 1000); put (b);
	snprintf (b, sizeof b, "M140 S%.0f", m.bedT); put (b); snprintf (b, sizeof b, "M104 S%.0f", m.nozzleT); put (b);
	put ("G21"); put ("G90"); put ("M82"); put ("G28");
	snprintf (b, sizeof b, "M190 S%.0f", m.bedT); put (b); snprintf (b, sizeof b, "M109 S%.0f", m.nozzleT); put (b);
	put ("G92 E0");
	V2 at (0, 0); bool have = false, out = false;
	for (size_t li = 0; li < j.layers.size (); li++)
	{
		const FdmLayer &l = j.layers[li]; double f = (li == 0 ? m.firstSpeed : m.speed) * 60;
		snprintf (b, sizeof b, "; layer %d", (int) li + 1); put (b);
		if (li == 2 && m.fan > 0) { snprintf (b, sizeof b, "M106 S%d", (int) (m.fan * 2.55)); put (b); }
		snprintf (b, sizeof b, "G0 Z%.3f F%.0f", l.z, m.travel * 60); put (b);
		for (const FdmPath &p : l.paths)
		{
			if (p.pts.size () < 2) continue;
			V2 st (p.pts[0].x + ox, p.pts[0].y + oy); double d = have ? hypot (st.x - at.x, st.y - at.y) : 1e9;
			if (d > 1e-4)
			{
				bool rt = d > 2 && m.retract > 0 && out;	// (a long way without drawing: the filament pulled back)
				if (rt) { e -= m.retract; snprintf (b, sizeof b, "G1 E%.5f F%.0f", e, m.retractSpeed * 60); put (b); secs += m.retract / m.retractSpeed; }
				snprintf (b, sizeof b, "G0 X%.3f Y%.3f F%.0f", st.x, st.y, m.travel * 60); put (b); if (have) secs += d / m.travel;
				if (rt) { e += m.retract; snprintf (b, sizeof b, "G1 E%.5f F%.0f", e, m.retractSpeed * 60); put (b); secs += m.retract / m.retractSpeed; }
			}
			at = st; have = true;
			for (size_t k = 1; k < p.pts.size () + (p.closed ? 1 : 0); k++)
			{
				const V2 &q = p.pts[k % p.pts.size ()]; V2 to (q.x + ox, q.y + oy); double len2 = hypot (to.x - at.x, to.y - at.y);
				if (len2 < 1e-6) continue;
				e += len2 * per; snprintf (b, sizeof b, "G1 X%.3f Y%.3f E%.5f F%.0f", to.x, to.y, e, f); put (b);
				secs += len2 / (f / 60); at = to; out = true;
			}
		}
	}
	snprintf (b, sizeof b, "G1 E%.5f F%.0f", e - m.retract, m.retractSpeed * 60); put (b);
	put ("M107"); put ("M104 S0"); put ("M140 S0");
	snprintf (b, sizeof b, "G0 Z%.3f F%.0f", std::min (m.sizeZ, j.hi.z + 10), m.travel * 60); put (b);
	put ("G0 X0 Y0"); put ("M84");
	if (minutes) *minutes = secs / 60;
	return g;
}

} // namespace forge

#endif
