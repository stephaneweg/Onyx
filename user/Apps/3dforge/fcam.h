//
// 3dforge/fcam.h -- Manufacture: from a body, the moves of a flat end mill on a small CNC router, and their G-code
// (GRBL). Portable, as fdoc.h (tools/tests/3dforge runs it on the PC).
//
//   * The setup: the body, the stock around it (margins, or a size of its own), the origin -- one of the stock's 27
//     points -- and where the machine's X points; Y follows (a quarter turn to the left, seen from above), Z is up.
//   * The tool (diameter, cutting length, spindle, feeds) and the machine (its travel), kept as presets.
//   * Clearing: the stock removed in levels. At a level the tool stays out of the body's shadow from there up, plus
//     its radius and what is left on the walls: Manifold cuts the body at the level and projects it, Clipper2 offsets.
//     The passes are that outline pushed outward a step over at a time, cut from the outside in. Levels: a step down
//     apart, and one on each flat face turned up.
//   * Contour: the tool's side on the body's outline seen from above (outside), or in its holes (inside), a pass a
//     step down; tabs: bridges left at the bottom.
//   * An operation works on the whole body, or on one flat face turned up (several steps on one part): a contour then
//     follows that face's edge -- around it, down to the floor under it, or inside it at its own height --, a clearing
//     takes away what is above the face only, down to it.
//   * Every pass is entered by a ramp along itself, from the level above -- already cleared --: a flat end mill
//     does not plunge.
//   * What was cut is kept as a height map of the stock (cam_simulate): it says whether a fast move would hit matter,
//     whether the tool cuts into the body, and is what Simulate shows.
//
// Heights are the model's until the G-code is written: there they become the machine's (the origin, the axes).
//
// MIT licence (Onyx).
//
#ifndef _3dforge_fcam_h
#define _3dforge_fcam_h

#include "fdoc.h"

namespace forge {

struct CamTool { char name[32]; double dia, flute, rpm, feed, plunge, travel; };
struct CamMachine { char name[32]; double tx, ty, tz, rpmMax, dwell; };	// travel (mm), the spindle's top speed, seconds to wait for it
static const CamTool CAM_TOOL0 = { "6 mm flat", 6, 22, 12000, 1200, 300, 3000 };
static const CamMachine CAM_MACHINE0 = { "Two Trees TTC 450", 460, 460, 80, 24000, 3 };

enum { CAM_CLEAR, CAM_CONTOUR };
struct CamOp
{
	int kind; char name[24];
	bool climb;
	double stepdown;			// both: a level's, a pass's depth
	double stepover, leaveR, leaveZ;	// clearing: between passes; left on the walls, on the floors
	double lowest; bool lowestAuto;		// clearing: the lowest level (the model's Z); auto: the lowest flat face turned up
	bool inside;				// contour: in the holes rather than around the body
	double from; bool fromAuto;		// contour: its first pass starts from (auto: the stock's top)
	double under;				// contour: how far under the body its last pass goes
	bool tabs; double ntabs, tabw, tabh;
	bool useFace; V3 facePt;		// on one face rather than the body: a point of it (found again at each computation)
	// (made by cam_compute)
	double length, minutes; int nmoves;
	bool failed; char err[72];
	CamOp () : kind (CAM_CLEAR), climb (true), stepdown (3), stepover (2.4), leaveR (0.3), leaveZ (0.2), lowest (0), lowestAuto (true), inside (false),
		   from (0), fromAuto (true), under (0.5), tabs (true), ntabs (4), tabw (6), tabh (1.5), useFace (false), length (0), minutes (0), nmoves (0), failed (false)
	{ name[0] = 0; err[0] = 0; }
};
struct CamSetup
{
	bool on;				// a setup was made
	int body;				// the body cut
	bool fixed; double side, top, under;	// the stock: margins around the body
	V3 size;				// ... or its own size (fixed: centred on the body, its underside on the body's)
	int origin;				// 0..26: ix + 3 iy + 9 iz (0 low / left / front, 1 middle, 2 high)
	int xdir;				// where the machine's X points: 0 the model's +X, 1 +Y, 2 -X, 3 -Y
	double safe, retract;			// above the stock: between operations, between passes
	CamTool tool; CamMachine machine;
	std::vector<CamOp> ops;
	CamSetup () : on (false), body (-1), fixed (false), side (5), top (2), under (0), size (100, 100, 20), origin (18), xdir (0), safe (10), retract (3),
		 tool (CAM_TOOL0), machine (CAM_MACHINE0) {}
};

// A move of the tool's tip (the centre of its flat end), in the model. kind: 0 fast, 1 cutting, 2 going down (the
// plunge feed).
struct CamMove { V3 p; unsigned char kind, op; };
struct CamPaths
{
	std::vector<CamMove> moves;
	V3 lo, hi;				// the stock
	double minutes, length;
	// the checks, made by cam_simulate
	std::vector<float> hm, part; int nx, ny; double cell;	// the stock's height where it was cut; the body's, from above
	bool hitFast, gouge, outside, tooDeep; double lowestZ;
	CamPaths () : minutes (0), length (0), nx (0), ny (0), cell (0.5), hitFast (false), gouge (false), outside (false), tooDeep (false), lowestZ (0) {}
};

static void cam_stock (const Doc &d, const CamSetup &c, V3 *lo, V3 *hi)
{
	const Body *b = 0; for (const Body &q : d.bodies) if (q.id == c.body) b = &q;
	if (!b) { *lo = V3 (); *hi = V3 (10, 10, 10); return; }
	if (!c.fixed) { *lo = b->mesh.lo - V3 (c.side, c.side, c.under); *hi = b->mesh.hi + V3 (c.side, c.side, c.top); return; }
	V3 m = (b->mesh.lo + b->mesh.hi) * 0.5;
	*lo = V3 (m.x - c.size.x / 2, m.y - c.size.y / 2, b->mesh.lo.z); *hi = V3 (m.x + c.size.x / 2, m.y + c.size.y / 2, b->mesh.lo.z + c.size.z);
}
static V3 cam_origin (const CamSetup &c, const V3 &lo, const V3 &hi)
{
	int ix = c.origin % 3, iy = c.origin / 3 % 3, iz = c.origin / 9;
	return V3 (lo.x + (hi.x - lo.x) * ix / 2, lo.y + (hi.y - lo.y) * iy / 2, lo.z + (hi.z - lo.z) * iz / 2);
}
// The machine's axes in the model: X as chosen, Y a quarter turn to its left, Z up.
static void cam_axes (const CamSetup &c, V3 *x, V3 *y)
{
	static const double D[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
	*x = V3 (D[c.xdir & 3][0], D[c.xdir & 3][1], 0); *y = V3 (-x->y, x->x, 0);
}
static V3 cam_work (const CamSetup &c, const V3 &o, const V3 &p) { V3 x, y; cam_axes (c, &x, &y); V3 q = p - o; return V3 (dot (q, x), dot (q, y), q.z); }

// ---- the passes ------------------------------------------------------------------------------------------------------
typedef std::vector<V2> CamLoop;
static double loop_area (const CamLoop &l) { double a = 0; for (size_t i = 0; i < l.size (); i++) { const V2 &p = l[i], &q = l[(i + 1) % l.size ()]; a += p.x * q.y - q.x * p.y; } return a / 2; }
static double loop_len (const CamLoop &l) { double s = 0; for (size_t i = 0; i < l.size (); i++) { const V2 &p = l[i], &q = l[(i + 1) % l.size ()]; s += hypot (q.x - p.x, q.y - p.y); } return s; }
static void loops_of (const CrossSection &cs, std::vector<CamLoop> *out)
{
	for (const SimplePolygon &p : cs.ToPolygons ())
	{
		if (p.size () < 3) continue;
		CamLoop l; for (const auto &q : p) l.push_back (V2 (q.x, q.y));
		if (loop_len (l) > 0.5) out->push_back (l);
	}
}
// What the tool must stay out of at height z: the body's shadow from there up.
static CrossSection cam_shadow (const Manifold &m, double z)
{
	Manifold top = m.TrimByPlane ({0, 0, 1}, z + 1e-4);
	return top.IsEmpty () ? CrossSection () : CrossSection (top.Project ());
}

// The flat face turned up that holds p: its face of the mesh, -1 none.
static int cam_face (const RMesh &m, const V3 &p)
{
	int best = -1; double bd = 1e30;
	for (int i = 0; i < m.tris (); i++)
	{
		if (m.n[i].z < 0.9999 || !m.flat[m.grp[i]]) continue;
		const V3 &a = m.v[m.t[i * 3]], &b = m.v[m.t[i * 3 + 1]], &c = m.v[m.t[i * 3 + 2]];
		if (fabs (a.z - p.z) > 0.05) continue;
		V3 mid = (a + b + c) * (1 / 3.0); double d = hypot (mid.x - p.x, mid.y - p.y);
		double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
		if (fabs (den) > 1e-12)
		{
			double w0 = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / den, w1 = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / den;
			if (w0 >= -1e-6 && w1 >= -1e-6 && w0 + w1 <= 1 + 1e-6) return m.grp[i];
		}
		if (d < bd) { bd = d; best = m.grp[i]; }
	}
	return bd < 2 ? best : -1;
}
// That face seen from above: its outline and its holes; *z its height.
static bool cam_face_shape (const RMesh &m, int g, CrossSection *out, double *z)
{
	Polygons tris;
	for (int i = 0; i < m.tris (); i++)
	{
		if (m.grp[i] != g) continue;
		SimplePolygon t; for (int k = 0; k < 3; k++) { const V3 &p = m.v[m.t[i * 3 + k]]; t.push_back ({p.x, p.y}); *z = p.z; }
		tris.push_back (t);
	}
	if (tris.empty ()) return false;
	*out = CrossSection (tris, CrossSection::FillRule::NonZero);
	return !out->IsEmpty ();
}
// The highest flat face turned up that is lower than z: the floor around a face (the body's underside if none).
static double cam_floor_under (const RMesh &m, double z)
{
	double best = m.lo.z;
	for (int i = 0; i < m.tris (); i++) if (m.n[i].z > 0.9999) { double h = m.v[m.t[i * 3]].z; if (h < z - 1e-6 && h > best) best = h; }
	return best;
}

struct CamEmit
{
	CamPaths *out; int op; double retractZ, safeZ;
	V3 at; bool has;
	CamEmit (CamPaths *o, int op_, double r, double s) : out (o), op (op_), retractZ (r), safeZ (s), has (false) {}
	void move (const V3 &p, int kind) { CamMove m; m.p = p; m.kind = (unsigned char) kind; m.op = (unsigned char) op; out->moves.push_back (m); at = p; has = true; }
	// Above (x, y), at the height fast moves are made at.
	void above (double x, double y)
	{
		if (has && at.z < retractZ - 1e-9) move (V3 (at.x, at.y, retractZ), 0);
		else if (!has) move (V3 (x, y, safeZ), 0);
		move (V3 (x, y, has ? at.z : safeZ), 0);
	}
	// A closed pass at height z, come to from zAbove (cleared already) by a ramp along its own start -- a gentle
	// slope: twelve times as long as it is deep --, then once all round, and over the ramp's stretch again at depth.
	// floor (i, t): a height the pass may not go under at the place t (0..1) of its segment i -- the tabs.
	template <class F> void pass (const CamLoop &l, double zAbove, double z, bool ramp, F floorAt)
	{
		size_t n = l.size (); if (n < 3) return;
		double total = loop_len (l); if (total < 1e-6) return;
		above (l[0].x, l[0].y);
		size_t first = 0;					// the segment the round at depth starts with
		if (zAbove > z + 1e-9 && ramp)
		{
			move (V3 (l[0].x, l[0].y, zAbove), 1);			// (down through what is cleared: at the cutting feed)
			double want = (zAbove - z) * 12, run = 0; if (want > total) want = total;
			size_t i = 0;
			for (; i < n && run < want - 1e-9; i++)
			{
				const V2 &q = l[(i + 1) % n]; run += hypot (q.x - l[i].x, q.y - l[i].y);
				double zz = run >= want ? z : zAbove + (z - zAbove) * run / want;
				move (V3 (q.x, q.y, std::max (zz, floorAt (i, 1.0, true))), 2);
			}
			first = i % n;
		}
		else { move (V3 (l[0].x, l[0].y, zAbove > z ? zAbove : z), 1); if (zAbove > z + 1e-9) move (V3 (l[0].x, l[0].y, z), 2); }
		for (size_t k = 0; k < n; k++) { size_t i = (first + k) % n; cut (l[i], l[(i + 1) % n], z, i, floorAt); }
		for (size_t i = 0; i < first; i++) cut (l[i], l[(i + 1) % n], z, i, floorAt);		// (the ramp's stretch, at depth)
	}
	template <class F> void cut (const V2 &a, const V2 &b, double z, size_t i, F floorAt)
	{
		// (a segment is cut in pieces where a tab starts or ends: floorAt is asked at a few places along it)
		double len = hypot (b.x - a.x, b.y - a.y); int k = len > 1.0 ? (int) ceil (len / 1.0) : 1; double last = -1e30;
		for (int j = 1; j <= k; j++)
		{
			double t = (double) j / k, zz = std::max (z, floorAt (i, t, true));
			if (j < k && fabs (zz - last) < 1e-9 && fabs (std::max (z, floorAt (i, (double) (j + 1) / k, true)) - zz) < 1e-9) continue;	// (straight on: one move)
			move (V3 (a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, zz), 1); last = zz;
		}
	}
	// A pass that starts and ends outside the stock (a ring cut short by the stock's edge): down in the air, then along.
	void open (const std::vector<V2> &l, double z)
	{
		if (l.size () < 2) return;
		above (l[0].x, l[0].y); move (V3 (l[0].x, l[0].y, z), 1);
		for (size_t i = 1; i < l.size (); i++) move (V3 (l[i].x, l[i].y, z), 1);
	}
	// ... one that starts in matter (the edge of a face's zone, under stock not yet cut): a ramp along its start,
	// back to where it began, then the whole of it.
	void openRamp (const std::vector<V2> &l, double zAbove, double z)
	{
		if (l.size () < 2) return;
		if (zAbove <= z + 1e-9) { open (l, z); return; }
		above (l[0].x, l[0].y); move (V3 (l[0].x, l[0].y, zAbove), 1);
		double total = 0; for (size_t i = 1; i < l.size (); i++) total += hypot (l[i].x - l[i - 1].x, l[i].y - l[i - 1].y);
		double want = (zAbove - z) * 12, run = 0; if (want > total) want = total; if (want < 1e-6) return;
		size_t i = 1;
		for (; i < l.size () && run < want - 1e-9; i++)
		{
			run += hypot (l[i].x - l[i - 1].x, l[i].y - l[i - 1].y);
			move (V3 (l[i].x, l[i].y, run >= want ? z : zAbove + (z - zAbove) * run / want), 2);
		}
		for (size_t k = i - 1; k-- > 0; ) move (V3 (l[k].x, l[k].y, z), 1);		// back along it, at depth
		for (size_t k = 1; k < l.size (); k++) move (V3 (l[k].x, l[k].y, z), 1);
	}
	void end () { if (has) move (V3 (at.x, at.y, safeZ), 0); }
};
static double no_floor (size_t, double, bool) { return -1e30; }

// The clearing's levels: a step down apart from the stock's top, and one on each flat face turned up, down to `lowest`.
static void cam_levels (const RMesh &m, double top, double lowest, double step, double leaveZ, std::vector<double> *lv)
{
	std::vector<double> faces;
	for (int i = 0; i < m.tris (); i++) if (m.n[i].z > 0.9999) faces.push_back (m.v[m.t[i * 3]].z);
	std::sort (faces.begin (), faces.end ());
	std::vector<double> all;
	for (double z = top - step; z > lowest + leaveZ + 1e-6; z -= step) all.push_back (z);
	for (double f : faces) if (f + leaveZ >= lowest + leaveZ - 1e-6 && f + leaveZ < top - 1e-6) all.push_back (f + leaveZ);
	all.push_back (lowest + leaveZ);
	std::sort (all.begin (), all.end (), [] (double a, double b) { return a > b; });
	for (double z : all) if (lv->empty () || lv->back () - z > 0.05) lv->push_back (z);
}
// The lowest flat face turned up that is not the body's underside's level: where a clearing stops by itself.
static double cam_lowest_face (const RMesh &m)
{
	double best = m.hi.z; bool got = false;
	for (int i = 0; i < m.tris (); i++)
		if (m.n[i].z > 0.9999) { double z = m.v[m.t[i * 3]].z; if (z > m.lo.z + 1e-6 && (!got || z < best)) { best = z; got = true; } }
	return got ? best : m.lo.z;
}

// Is p inside the shape? (its loops crossed an odd number of times)
static bool in_loops (const std::vector<CamLoop> &ls, const V2 &p)
{
	bool in = false;
	for (const CamLoop &l : ls)
		for (size_t i = 0, j = l.size () - 1; i < l.size (); j = i++)
			if ((l[i].y > p.y) != (l[j].y > p.y) && p.x < (l[j].x - l[i].x) * (p.y - l[i].y) / (l[j].y - l[i].y) + l[i].x) in = !in;
	return in;
}
static bool cam_clearing (const Body &b, const CamSetup &c, CamOp &op, int index, const V3 &lo, const V3 &hi, CamPaths *out)
{
	double r = c.tool.dia / 2;
	if (r < 0.05 || op.stepover < 0.05 || op.stepdown < 0.05) { snprintf (op.err, sizeof op.err, "The tool and the steps must be more than 0"); return false; }
	if (op.stepover > c.tool.dia) { snprintf (op.err, sizeof op.err, "The step over is wider than the tool"); return false; }
	double lowest = op.lowestAuto ? cam_lowest_face (b.mesh) : op.lowest; if (lowest < lo.z) lowest = lo.z;
	CrossSection stock = CrossSection::Square ({hi.x - lo.x + 2 * r, hi.y - lo.y + 2 * r}).Translate ({lo.x - r, lo.y - r});
	std::vector<CamLoop> zoneIn;				// (one face: its zone, a hair smaller -- what is not on its edge)
	if (op.useFace)				// only what is above that face, down to it: the walls beside it are kept by the shadow
	{
		int g = cam_face (b.mesh, op.facePt); CrossSection shape; double fz = 0;
		if (g < 0 || !cam_face_shape (b.mesh, g, &shape, &fz)) { snprintf (op.err, sizeof op.err, "Choose a flat face turned up"); return false; }
		stock = stock ^ shape.Offset (r, CrossSection::JoinType::Round, 2.0, 48); lowest = fz;
		loops_of (stock.Offset (-0.02, CrossSection::JoinType::Round), &zoneIn);
	}
	std::vector<double> levels; cam_levels (b.mesh, hi.z, lowest, op.stepdown, op.leaveZ, &levels);
	CamEmit e (out, index, hi.z + c.retract, hi.z + c.safe);
	double above = hi.z + 1;
	for (double z : levels)
	{
		CrossSection keep = cam_shadow (b.m, z - op.leaveZ + 1e-3);
		std::vector<std::vector<CamLoop> > rings;			// from the body outward
		if (keep.IsEmpty ())
		{
			CrossSection ring = stock.Offset (-r, CrossSection::JoinType::Round);
			while (ring.Area () > 0.02) { std::vector<CamLoop> l; loops_of (ring, &l); if (l.empty ()) break; rings.insert (rings.begin (), l); ring = ring.Offset (-op.stepover, CrossSection::JoinType::Round); }
		}
		else
			for (double dd = r + op.leaveR; dd < 4000; dd += op.stepover)
			{
				CrossSection grown = keep.Offset (dd, CrossSection::JoinType::Round, 2.0, 48);
				if ((stock - grown).Area () < 0.02) break;		// (the whole stock is behind it: done)
				std::vector<CamLoop> l; loops_of (grown ^ stock, &l); if (l.empty ()) break;
				rings.push_back (l);
			}
		// (a point on the edge of where the tool may go around the whole stock: there it is in the air)
		double bx0 = lo.x - r, bx1 = hi.x + r, by0 = lo.y - r, by1 = hi.y + r;
		auto side = [&] (const V2 &p) { return fabs (p.x - bx0) < 1e-6 ? 1 : fabs (p.x - bx1) < 1e-6 ? 2 : fabs (p.y - by0) < 1e-6 ? 3 : fabs (p.y - by1) < 1e-6 ? 4 : 0; };
		for (int k = (int) rings.size () - 1; k >= 0; k--)		// from the outside in
			for (CamLoop &l : rings[k])
			{
				// (climb: the matter on the tool's left with a spindle turning right -- clockwise round what is kept)
				bool cw = loop_area (l) < 0; if (cw != op.climb) std::reverse (l.begin (), l.end ());
				// the stretches that only follow the stock's edge cut nothing: left out, the ring becomes open passes
				// that start and end in the air. On one face, the stretches along its zone's edge are not the ring's
				// either (they would run into what is beside the face): left out too -- but there the pass may start in
				// matter: it ramps in.
				size_t n = l.size (); std::vector<char> air (n, 0); bool any = false;
				for (size_t i = 0; i < n; i++)
				{
					const V2 &p0 = l[i], &p1 = l[(i + 1) % n];
					if (op.useFace) { if (!in_loops (zoneIn, V2 ((p0.x + p1.x) / 2, (p0.y + p1.y) / 2))) { air[i] = 1; any = true; } }
					else { int a = side (p0), b2 = side (p1); if (a && a == b2) { air[i] = 1; any = true; } }
				}
				if (!any) { e.pass (l, above, z, true, no_floor); continue; }
				size_t s0 = 0; while (s0 < n && !air[s0]) s0++;		// start after an edge stretch
				for (size_t done = 0; done < n; )
				{
					while (done < n && air[(s0 + done) % n]) done++;
					std::vector<V2> run;
					while (done < n && !air[(s0 + done) % n]) { if (run.empty ()) run.push_back (l[(s0 + done) % n]); run.push_back (l[(s0 + done + 1) % n]); done++; }
					if (op.useFace) e.openRamp (run, above, z); else e.open (run, z);
				}
			}
		above = z;
	}
	e.end ();
	return true;
}

static bool cam_contour (const Body &b, const CamSetup &c, CamOp &op, int index, const V3 &lo, const V3 &hi, CamPaths *out)
{
	double r = c.tool.dia / 2;
	if (r < 0.05 || op.stepdown < 0.05) { snprintf (op.err, sizeof op.err, "The tool and the step down must be more than 0"); return false; }
	CrossSection shape (b.m.Project ()); double faceZ = 0; bool onFace = false;
	if (op.useFace)
	{
		int g = cam_face (b.mesh, op.facePt);
		if (g < 0 || !cam_face_shape (b.mesh, g, &shape, &faceZ)) { snprintf (op.err, sizeof op.err, "Choose a flat face turned up"); return false; }
		onFace = true;
	}
	std::vector<CamLoop> raw, loops; loops_of (shape, &raw);
	for (const CamLoop &l : raw)					// (outside: the outlines; inside: the holes -- they turn the other way)
	{
		// (a face: around its outline, or inside it -- its holes are left; the body: its outline, or its holes)
		bool hole = loop_area (l) < 0; if (onFace ? hole : hole != op.inside) continue;
		SimplePolygon p; for (const V2 &q : l) p.push_back ({q.x, q.y});
		if (hole) std::reverse (p.begin (), p.end ());
		bool inward = onFace ? op.inside : hole;
		loops_of (CrossSection (Polygons { p }).Offset (inward ? -r : r, CrossSection::JoinType::Round, 2.0, 96), &loops);
	}
	if (loops.empty ()) { snprintf (op.err, sizeof op.err, op.inside ? "No hole the tool fits in" : "The body has no outline"); return false; }
	double top = op.fromAuto ? hi.z : op.from, bottom = b.mesh.lo.z - op.under; if (top > hi.z) top = hi.z;
	if (onFace) bottom = op.inside ? faceZ : cam_floor_under (b.mesh, faceZ);	// (inside it: to its own height; around it: to the floor)
	if (top <= bottom) { snprintf (op.err, sizeof op.err, "It starts under where it ends"); return false; }
	CamEmit e (out, index, hi.z + c.retract, hi.z + c.safe);
	for (CamLoop &l : loops)
	{
		bool cw = loop_area (l) < 0; if (cw != (op.climb != op.inside)) std::reverse (l.begin (), l.end ());
		// the tabs: ntabs bridges along the pass, each tabw wide (and the tool's width more), left tabh high
		double total = loop_len (l), tabZ = b.mesh.lo.z + op.tabh; std::vector<double> start (l.size () + 1, 0);
		for (size_t i = 0; i < l.size (); i++) { const V2 &q = l[(i + 1) % l.size ()]; start[i + 1] = start[i] + hypot (q.x - l[i].x, q.y - l[i].y); }
		bool tabs = op.tabs && op.ntabs > 0 && !op.inside && !onFace && total > op.ntabs * (op.tabw + c.tool.dia) * 1.5;
		double half = (op.tabw + c.tool.dia) / 2;
		auto floorAt = [&] (size_t i, double t, bool) -> double
		{
			if (!tabs) return -1e30;
			double s = start[i] + (start[i + 1] - start[i]) * t;
			for (int k = 0; k < op.ntabs; k++) { double mid = total * (k + 0.5) / op.ntabs; if (fabs (s - mid) <= half) return tabZ; }
			return -1e30;
		};
		double above = top;
		for (double z = top - op.stepdown; ; z -= op.stepdown)
		{
			if (z < bottom + 1e-6) z = bottom;
			e.pass (l, above, z, true, floorAt);
			above = z; if (z <= bottom + 1e-9) break;
		}
	}
	e.end ();
	return true;
}

// ---- what was cut: the stock's height, a cell at a time ---------------------------------------------------------------
static void cam_simulate (const Body &b, const CamSetup &c, CamPaths &p)
{
	double span = std::max (p.hi.x - p.lo.x, p.hi.y - p.lo.y) + 2 * c.tool.dia;
	p.cell = span > 320 ? span / 640 : 0.5; double r = c.tool.dia / 2, x0 = p.lo.x - c.tool.dia, y0 = p.lo.y - c.tool.dia;
	p.nx = (int) ((p.hi.x - p.lo.x + 2 * c.tool.dia) / p.cell) + 1; p.ny = (int) ((p.hi.y - p.lo.y + 2 * c.tool.dia) / p.cell) + 1;
	p.hm.assign ((size_t) p.nx * p.ny, (float) p.hi.z); p.part.assign ((size_t) p.nx * p.ny, (float) -1e9);
	for (int j = 0; j < p.ny; j++) for (int i = 0; i < p.nx; i++)			// (around the stock: air)
	{
		double x = x0 + (i + 0.5) * p.cell, y = y0 + (j + 0.5) * p.cell;
		if (x < p.lo.x || x > p.hi.x || y < p.lo.y || y > p.hi.y) p.hm[(size_t) j * p.nx + i] = (float) -1e9;
	}
	// the body from above: each triangle turned up laid on the cells it covers
	const RMesh &m = b.mesh;
	for (int t = 0; t < m.tris (); t++)
	{
		if (m.n[t].z <= 1e-6) continue;
		const V3 &a = m.v[m.t[t * 3]], &bb = m.v[m.t[t * 3 + 1]], &cc = m.v[m.t[t * 3 + 2]];
		int i0 = (int) floor ((std::min (a.x, std::min (bb.x, cc.x)) - x0) / p.cell), i1 = (int) ceil ((std::max (a.x, std::max (bb.x, cc.x)) - x0) / p.cell);
		int j0 = (int) floor ((std::min (a.y, std::min (bb.y, cc.y)) - y0) / p.cell), j1 = (int) ceil ((std::max (a.y, std::max (bb.y, cc.y)) - y0) / p.cell);
		double den = (bb.y - cc.y) * (a.x - cc.x) + (cc.x - bb.x) * (a.y - cc.y); if (fabs (den) < 1e-12) continue;
		for (int j = std::max (j0, 0); j <= j1 && j < p.ny; j++) for (int i = std::max (i0, 0); i <= i1 && i < p.nx; i++)
		{
			double x = x0 + (i + 0.5) * p.cell, y = y0 + (j + 0.5) * p.cell;
			double w0 = ((bb.y - cc.y) * (x - cc.x) + (cc.x - bb.x) * (y - cc.y)) / den, w1 = ((cc.y - a.y) * (x - cc.x) + (a.x - cc.x) * (y - cc.y)) / den, w2 = 1 - w0 - w1;
			if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
			float z = (float) (w0 * a.z + w1 * bb.z + w2 * cc.z); float &h = p.part[(size_t) j * p.nx + i]; if (z > h) h = z;
		}
	}
	// the moves: the tool's flat end lowers the cells under it; a fast move must find them lower than itself
	p.hitFast = p.gouge = false; p.lowestZ = p.hi.z; p.length = 0; p.minutes = 0;
	int rc = (int) ceil (r / p.cell);
	auto stamp = [&] (double x, double y, double z, bool cutting)
	{
		int ci = (int) ((x - x0) / p.cell), cj = (int) ((y - y0) / p.cell);
		for (int j = cj - rc; j <= cj + rc; j++) for (int i = ci - rc; i <= ci + rc; i++)
		{
			if (i < 0 || j < 0 || i >= p.nx || j >= p.ny) continue;
			double dx = x0 + (i + 0.5) * p.cell - x, dy = y0 + (j + 0.5) * p.cell - y;
			if (dx * dx + dy * dy > r * r) continue;
			float &h = p.hm[(size_t) j * p.nx + i];
			if (!cutting) { if (h > z + 0.05) p.hitFast = true; continue; }
			if (z < h) h = (float) z;
		}
	};
	for (size_t k = 1; k < p.moves.size (); k++)
	{
		const CamMove &a = p.moves[k - 1], &m2 = p.moves[k]; V3 d = m2.p - a.p; double l = len (d);
		double feed = m2.kind == 0 ? c.tool.travel : m2.kind == 2 ? c.tool.plunge : c.tool.feed;
		if (feed > 1) p.minutes += l / feed;
		if (m2.kind) p.length += l;
		if (m2.p.z < p.lowestZ) p.lowestZ = m2.p.z;
		int n = (int) ceil (l / (p.cell * 0.9)) + 1;
		for (int s = 0; s <= n; s++) { V3 q = a.p + d * ((double) s / n); stamp (q.x, q.y, q.z, m2.kind != 0); }
	}
	// the body under what was cut: a cell lower than the body there by more than its own size is a cut into it
	double tol = p.cell * 1.5 + 0.05;
	for (size_t i = 0; i < p.hm.size (); i++) if (p.part[i] > -1e8 && p.hm[i] > -1e8 && p.hm[i] < p.part[i] - tol) { p.gouge = true; break; }
}

// The operations' moves, and the checks. false: nothing could be made (cam.ops[i].err say why).
static bool cam_compute (Doc &d, CamSetup &c, CamPaths &out)
{
	out.moves.clear (); out.minutes = out.length = 0; out.hitFast = out.gouge = out.outside = out.tooDeep = false;
	Body *b = d.body (c.body);
	if (!b) return false;
	cam_stock (d, c, &out.lo, &out.hi);
	bool any = false;
	for (size_t i = 0; i < c.ops.size (); i++)
	{
		CamOp &op = c.ops[i]; op.failed = false; op.err[0] = 0; size_t before = out.moves.size ();
		bool ok = op.kind == CAM_CLEAR ? cam_clearing (*b, c, op, (int) i, out.lo, out.hi, &out) : cam_contour (*b, c, op, (int) i, out.lo, out.hi, &out);
		op.failed = !ok; op.nmoves = (int) (out.moves.size () - before); op.length = 0; op.minutes = 0;
		for (size_t k = before + 1; k < out.moves.size (); k++)
		{
			const CamMove &m = out.moves[k]; double l = len (m.p - out.moves[k - 1].p);
			double feed = m.kind == 0 ? c.tool.travel : m.kind == 2 ? c.tool.plunge : c.tool.feed;
			if (m.kind) op.length += l; if (feed > 1) op.minutes += l / feed;
		}
		if (ok) any = true;
	}
	if (!any) return false;
	cam_simulate (*b, c, out);
	// inside the machine's travel? (the moves as the machine makes them, from its origin)
	V3 o = cam_origin (c, out.lo, out.hi), wlo (1e30, 1e30, 1e30), whi (-1e30, -1e30, -1e30);
	for (const CamMove &m : out.moves)
	{
		V3 w = cam_work (c, o, m.p);
		wlo = V3 (std::min (wlo.x, w.x), std::min (wlo.y, w.y), std::min (wlo.z, w.z)); whi = V3 (std::max (whi.x, w.x), std::max (whi.y, w.y), std::max (whi.z, w.z));
	}
	out.outside = whi.x - wlo.x > c.machine.tx || whi.y - wlo.y > c.machine.ty || whi.z - wlo.z > c.machine.tz;
	double deepest = 0; for (const CamOp &op : c.ops) if (!op.failed && op.stepdown > deepest) deepest = op.stepdown;
	out.tooDeep = deepest > c.tool.flute + 1e-9;
	return true;
}

// ---- the G-code, as GRBL reads it ------------------------------------------------------------------------------------
static std::string cam_gcode (const CamSetup &c, const CamPaths &p, const char *title, int *lines)
{
	std::string s; char b[160]; int n = 0;
	auto put = [&] (const char *t) { s += t; s += "\n"; n++; };
	V3 o = cam_origin (c, p.lo, p.hi);
	snprintf (b, sizeof b, "(3DForge - %s)", title); put (b);
	snprintf (b, sizeof b, "(T1  %.4g mm flat end mill - %s)", c.tool.dia, c.tool.name); put (b);
	snprintf (b, sizeof b, "(stock %.4g x %.4g x %.4g mm - about %d min)", p.hi.x - p.lo.x, p.hi.y - p.lo.y, p.hi.z - p.lo.z, (int) (p.minutes + 0.5)); put (b);
	put ("G21 G90 G17 G94");
	V3 w0 = cam_work (c, o, V3 (o.x, o.y, p.hi.z + c.safe)); snprintf (b, sizeof b, "G0 Z%.3f", w0.z); put (b);
	double rpm = c.tool.rpm > c.machine.rpmMax ? c.machine.rpmMax : c.tool.rpm;
	snprintf (b, sizeof b, "M3 S%d", (int) rpm); put (b);
	if (c.machine.dwell >= 1) { snprintf (b, sizeof b, "G4 P%d", (int) c.machine.dwell); put (b); }
	int op = -1; double feed = -1; V3 last (1e30, 1e30, 1e30);
	for (const CamMove &m : p.moves)
	{
		if (m.op != op && m.op < c.ops.size ()) { op = m.op; snprintf (b, sizeof b, "(%s)", c.ops[op].name); put (b); }
		V3 w = cam_work (c, o, m.p); std::string l = m.kind ? "G1" : "G0"; char t[40];
		if (fabs (w.x - last.x) > 0.0005) { snprintf (t, sizeof t, " X%.3f", w.x); l += t; }
		if (fabs (w.y - last.y) > 0.0005) { snprintf (t, sizeof t, " Y%.3f", w.y); l += t; }
		if (fabs (w.z - last.z) > 0.0005) { snprintf (t, sizeof t, " Z%.3f", w.z); l += t; }
		if (l.size () == 2) continue;				// (it does not move)
		double f = m.kind == 2 ? c.tool.plunge : c.tool.feed;
		if (m.kind && f != feed) { snprintf (t, sizeof t, " F%d", (int) f); l += t; feed = f; }
		put (l.c_str ()); last = w;
	}
	put ("M5"); put ("G0 X0 Y0"); put ("M2");
	if (lines) *lines = n;
	return s;
}

// ---- in the part's file ------------------------------------------------------------------------------------------------
static std::string cam_save (const CamSetup &c)
{
	if (!c.on) return std::string ();
	std::string s; char b[400];
	snprintf (b, sizeof b, "cam body %d fixed %d margins %.9g %.9g %.9g size %.9g %.9g %.9g origin %d xdir %d heights %.9g %.9g\n", c.body, c.fixed ? 1 : 0, c.side, c.top, c.under,
		  c.size.x, c.size.y, c.size.z, c.origin, c.xdir, c.safe, c.retract); s += b;
	snprintf (b, sizeof b, "camtool %.9g %.9g %.9g %.9g %.9g %.9g %s\n", c.tool.dia, c.tool.flute, c.tool.rpm, c.tool.feed, c.tool.plunge, c.tool.travel, c.tool.name); s += b;
	snprintf (b, sizeof b, "cammachine %.9g %.9g %.9g %.9g %d %s\n", c.machine.tx, c.machine.ty, c.machine.tz, c.machine.rpmMax, (int) c.machine.dwell, c.machine.name); s += b;
	for (const CamOp &o : c.ops)
	{
		snprintf (b, sizeof b, "camop %d climb %d down %.9g over %.9g leave %.9g %.9g lowest %d %.9g inside %d from %d %.9g under %.9g tabs %d %d %.9g %.9g face %d %.9g %.9g %.9g name %s\n", o.kind, o.climb ? 1 : 0,
			  o.stepdown, o.stepover, o.leaveR, o.leaveZ, o.lowestAuto ? 1 : 0, o.lowest, o.inside ? 1 : 0, o.fromAuto ? 1 : 0, o.from, o.under, o.tabs ? 1 : 0, (int) (o.ntabs + 0.5), o.tabw, o.tabh,
			  o.useFace ? 1 : 0, o.facePt.x, o.facePt.y, o.facePt.z, o.name); s += b;
	}
	return s;
}
static void cam_load (CamSetup &c, const char *text)
{
	c = CamSetup ();
	for (const char *p = text; p && *p; )
	{
		const char *e = strchr (p, '\n'); size_t n = e ? (size_t) (e - p) : strlen (p);
		char line[500]; if (n >= sizeof line) n = sizeof line - 1; memcpy (line, p, n); line[n] = 0; if (n && line[n - 1] == '\r') line[n - 1] = 0;
		p = e ? e + 1 : 0; int off = 0, a = 0, b2 = 0, d = 0, f = 0, g = 0, h = 0, nt = 4, dw = 3;
		if (!strncmp (line, "cam body ", 9))
		{
			if (sscanf (line, "cam body %d fixed %d margins %lf %lf %lf size %lf %lf %lf origin %d xdir %d heights %lf %lf", &c.body, &a, &c.side, &c.top, &c.under, &c.size.x, &c.size.y, &c.size.z,
				    &c.origin, &c.xdir, &c.safe, &c.retract) >= 12) { c.on = true; c.fixed = a != 0; }
		}
		else if (!strncmp (line, "camtool ", 8))
		{ if (sscanf (line + 8, "%lf %lf %lf %lf %lf %lf %n", &c.tool.dia, &c.tool.flute, &c.tool.rpm, &c.tool.feed, &c.tool.plunge, &c.tool.travel, &off) >= 6) snprintf (c.tool.name, sizeof c.tool.name, "%s", line + 8 + off); }
		else if (!strncmp (line, "cammachine ", 11))
		{ if (sscanf (line + 11, "%lf %lf %lf %lf %d %n", &c.machine.tx, &c.machine.ty, &c.machine.tz, &c.machine.rpmMax, &dw, &off) >= 5) c.machine.dwell = dw, snprintf (c.machine.name, sizeof c.machine.name, "%s", line + 11 + off); }
		else if (!strncmp (line, "camop ", 6))
		{
			CamOp o;
			if (sscanf (line + 6, "%d climb %d down %lf over %lf leave %lf %lf lowest %d %lf inside %d from %d %lf under %lf tabs %d %d %lf %lf face %d %lf %lf %lf name %n", &o.kind, &a, &o.stepdown, &o.stepover, &o.leaveR, &o.leaveZ,
				    &b2, &o.lowest, &d, &f, &o.from, &o.under, &g, &nt, &o.tabw, &o.tabh, &h, &o.facePt.x, &o.facePt.y, &o.facePt.z, &off) >= 20)
			{ o.climb = a != 0; o.lowestAuto = b2 != 0; o.inside = d != 0; o.fromAuto = f != 0; o.tabs = g != 0; o.ntabs = nt; o.useFace = h != 0; snprintf (o.name, sizeof o.name, "%s", line + 6 + off); c.ops.push_back (o); }
		}
	}
}

// The tool and the machine alone: what is remembered from a part to the next (SD:/apps/3dforge.app/cam.ini).
static std::string cam_presets (const CamSetup &c) { CamSetup t = c; t.on = true; t.ops.clear (); std::string s = cam_save (t); size_t i = s.find ("camtool"); return i == std::string::npos ? std::string () : s.substr (i); }
static void cam_presets_take (CamSetup &c, const char *text) { CamSetup t; cam_load (t, text); c.tool = t.tool; c.machine = t.machine; }

} // namespace forge

#endif
