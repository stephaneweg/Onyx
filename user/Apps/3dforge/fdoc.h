//
// 3dforge/fdoc.h -- the document: a history of steps (a box, a cylinder, a sketch and its extrusion, fillets and
// chamfers, a move, two bodies combined), each with its values, replayed from the top by Manifold into the bodies.
// Nothing here draws: frender.h shows the bodies, fview.h is the tools. Portable (tools/tests/3dforge runs it on
// the PC).
//
//   * A step applies to a body (its target) with an operation: new body, union, subtract, intersect.
//   * A sketch has no solver: its elements are recipes -- where one starts (the end of the one before, or a point),
//     an angle, a length; an arc by its centre, its radius, its sweep -- evaluated in the order they were drawn.
//   * A fillet or a chamfer is a solid added (an inner edge) or cut (an outer one): the corner's profile extruded
//     along a straight edge between two flat faces, or turned around the axis of an edge on a circle.
//   * A face or an edge chosen by the user is kept by where it is (a plane, a point on the edge), and found again
//     in the body at each replay.
//
// MIT licence (Onyx).
//
#ifndef _3dforge_fdoc_h
#define _3dforge_fdoc_h

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include "manifold/manifold.h"
#include "manifold/cross_section.h"

namespace forge {

using manifold::Manifold;
using manifold::CrossSection;
using manifold::MeshGL;
using manifold::Polygons;
using manifold::SimplePolygon;

static const double PI = 3.14159265358979323846;
static const double EPS = 0.01;			// (mm) how far a cutting solid sticks out of the face it starts on

// ---- vectors ---------------------------------------------------------------------------------------------------
struct V3
{
	double x, y, z;
	V3 () : x (0), y (0), z (0) {}
	V3 (double a, double b, double c) : x (a), y (b), z (c) {}
	V3 operator+ (const V3 &o) const { return V3 (x + o.x, y + o.y, z + o.z); }
	V3 operator- (const V3 &o) const { return V3 (x - o.x, y - o.y, z - o.z); }
	V3 operator* (double k) const { return V3 (x * k, y * k, z * k); }
	V3 operator- () const { return V3 (-x, -y, -z); }
};
static inline double dot (const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross (const V3 &a, const V3 &b) { return V3 (a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline double len (const V3 &a) { return sqrt (dot (a, a)); }
static inline V3 unit (const V3 &a) { double l = len (a); return l > 1e-12 ? a * (1 / l) : V3 (0, 0, 1); }
struct V2 { double x, y; V2 () : x (0), y (0) {} V2 (double a, double b) : x (a), y (b) {} };

// A plane with its own axes: a point of it is o + a u + b v; n = u x v points out of the body it lies on.
struct Plane
{
	V3 o, u, v, n;
	Plane () : o (0, 0, 0), u (1, 0, 0), v (0, 1, 0), n (0, 0, 1) {}
	V3 at (double a, double b, double c = 0) const { return o + u * a + v * b + n * c; }
	V2 to (const V3 &p) const { V3 q = p - o; return V2 (dot (q, u), dot (q, v)); }
	bool ground () const { return fabs (n.z - 1) < 1e-9 && len (o) < 1e-9; }
};
// The plane of normal n through p, its axes chosen so that a drawing reads the right way up: u horizontal.
static inline Plane plane_of (const V3 &p, const V3 &n_)
{
	Plane pl; pl.n = unit (n_);
	if (fabs (pl.n.z) > 0.999) pl.u = V3 (1, 0, 0);
	else pl.u = unit (cross (V3 (0, 0, 1), pl.n));
	pl.v = cross (pl.n, pl.u);
	pl.o = pl.n * dot (p, pl.n);			// (the plane's point nearest to the origin: the drawing's numbers
	return pl;					//  do not depend on where the face was clicked)
}

// ---- the steps -------------------------------------------------------------------------------------------------
enum { F_BOX, F_CYL, F_SKETCH, F_EXTRUDE, F_FILLET, F_CHAMFER, F_MOVE, F_COMBINE, F_PYRAMID, F_PRISM, F_TAPER, F_TORUS, F_SPHERE, F_KINDS };
// The steps that make a solid of their own, joined to a body or cut from it by their operation.
static inline bool makes_solid (int k) { return k == F_BOX || k == F_CYL || k == F_EXTRUDE || k == F_PYRAMID || k == F_PRISM || k == F_TAPER || k == F_TORUS || k == F_SPHERE; }
// ... those drawn from a centre: a base's radius, then a height (the torus: its ring, then its tube).
static inline bool round_kind (int k) { return k == F_CYL || k == F_PYRAMID || k == F_PRISM || k == F_TAPER || k == F_TORUS || k == F_SPHERE; }
enum { OP_NEW, OP_UNION, OP_SUB, OP_INT };
enum { SK_LINE, SK_ARC, SK_CIRCLE, SK_RECT, SK_CLOSE, SK_SPLINE, SK_POINT };
static const char *const KIND_NAME[F_KINDS] = { "Box", "Cylinder", "Sketch", "Extrude", "Fillet", "Chamfer", "Move", "Combine", "Pyramid", "Prism", "Taper", "Torus", "Sphere" };
static const char *const KIND_KEY[F_KINDS] = { "box", "cyl", "sketch", "extrude", "fillet", "chamfer", "move", "combine", "pyramid", "prism", "taper", "torus", "sphere" };
static const char *const OP_NAME[4] = { "New body", "Union", "Subtract", "Intersect" };

// A sketch's element. chain: it starts where the one before ended; else at (x, y).
struct SkEl
{
	int kind; bool chain, rel;
	double x, y;			// the start (line, arc) -- the centre (circle) -- a corner (rectangle)
	double a, len;			// a line: its angle (degrees; rel: from the line before), its length
	double r, ca, sweep;		// an arc: its radius, the direction from its start to its centre, its sweep (+: clockwise;
					// in the file: + anticlockwise, as it was first)
	double w, h;			// a rectangle's sizes (rel: (x, y) is its centre, not a corner); w: a circle's diameter
	std::vector<V2> pts;		// a spline: the points it goes through after its start
					// (a point: a mark to snap to, part of no outline)
	SkEl () : kind (SK_LINE), chain (false), rel (false), x (0), y (0), a (0), len (0), r (0), ca (0), sweep (0), w (0), h (0) {}
};

struct Feature
{
	int kind; char name[24];
	int op, target;			// the operation and the body it applies to (-1: none: a new body)
	Plane pl;			// box, cylinder, sketch: the plane drawn on
	double x, y, w, d, h;		// box: the first click, the sizes, the height -- cylinder: the centre, w the diameter, h
					// pyramid, prism, taper: the centre, w the base's radius, d the top's (taper), h -- torus: w
					// the ring's radius, d the tube's
	double n;			// pyramid, prism, taper: the base's sides (less than 3: round)
	double turn = 0;		// a box, a base with sides: turned about its plane's normal (degrees), round where it was clicked
	double z = 0;			// a shape: how far off its plane (along its normal) its base is -- a sphere, a torus: its centre
	bool centred, through;		// box: (x, y) is its centre -- cylinder, extrude: through the whole body
	std::vector<SkEl> els;		// sketch
	int sketch;			// extrude: its sketch's step
	std::vector<V3> edges;		// fillet, chamfer: a point on each edge
	double r;			// ... the radius, the chamfer's size
	V3 mv; int tool;		// move: by how much -- combine: the body used (it is consumed)
	V3 rot, sc;			// move: turned around X, Y, Z (degrees, about the body's centre), scaled along each (1: as it is)
	bool clone;			// move: the body stays as it is, a copy of it is what moves (a new body)
	bool failed; char err[72];	// (set by the replay)
	Feature () : kind (F_BOX), op (OP_NEW), target (-1), x (0), y (0), w (0), d (0), h (0), n (4), centred (true), through (false),
		     sketch (-1), r (2), tool (-1), sc (1, 1, 1), clone (false), failed (false) { name[0] = 0; err[0] = 0; }
	bool turned () const { return fabs (rot.x) > 1e-9 || fabs (rot.y) > 1e-9 || fabs (rot.z) > 1e-9; }
	bool scaled () const { return fabs (sc.x - 1) > 1e-9 || fabs (sc.y - 1) > 1e-9 || fabs (sc.z - 1) > 1e-9; }
};

// ---- a body's mesh, as the view and the tools want it ----------------------------------------------------------------
// Faces: the triangles joined across an edge when nearly coplanar (a curved wall is one face). Chains: the lines
// where two faces meet, each an ordered run of vertices.
struct Chain
{
	std::vector<int> pts; bool closed; int g0, g1;
	int kind;			// 0 other, 1 straight between two flat faces, 2 a circle on a flat face, 3 an arc of one
};
struct RMesh
{
	std::vector<V3> v; std::vector<int> t; std::vector<V3> n; std::vector<int> grp;
	int ngrp;
	std::vector<char> flat; std::vector<V3> gn;	// a face: flat?, its normal
	std::vector<Chain> chains;
	std::vector<int> smooth;		// the edges inside a curved face (pairs of vertices, then the two triangles): 4 ints each
	V3 lo, hi;
	int tris () const { return (int) t.size () / 3; }
	void clear () { v.clear (); t.clear (); n.clear (); grp.clear (); flat.clear (); gn.clear (); chains.clear (); smooth.clear (); ngrp = 0; lo = hi = V3 (); }
};

static inline unsigned long long ekey (int a, int b) { if (a > b) { int c = a; a = b; b = c; } return ((unsigned long long) a << 32) | (unsigned) b; }

static bool circle_of (const V3 &p0, const V3 &p1, const V3 &p2, V3 *ctr);
static void build_mesh (const Manifold &m, RMesh &r)
{
	r.clear ();
	if (m.IsEmpty ()) return;
	MeshGL g = m.GetMeshGL ();
	int nv = (int) g.NumVert (), nt = (int) g.NumTri (), np = (int) g.numProp;
	r.v.resize (nv); r.t.resize (nt * 3); r.n.resize (nt); r.grp.resize (nt);
	for (int i = 0; i < nv; i++) r.v[i] = V3 (g.vertProperties[i * np], g.vertProperties[i * np + 1], g.vertProperties[i * np + 2]);
	for (int i = 0; i < nt * 3; i++) r.t[i] = (int) g.triVerts[i];
	r.lo = r.hi = nv ? r.v[0] : V3 ();
	for (int i = 0; i < nv; i++)
	{
		const V3 &p = r.v[i];
		if (p.x < r.lo.x) r.lo.x = p.x; if (p.y < r.lo.y) r.lo.y = p.y; if (p.z < r.lo.z) r.lo.z = p.z;
		if (p.x > r.hi.x) r.hi.x = p.x; if (p.y > r.hi.y) r.hi.y = p.y; if (p.z > r.hi.z) r.hi.z = p.z;
	}
	std::vector<int> orig (nt, 0);
	for (size_t k = 0; k + 1 < g.runIndex.size (); k++)
		for (unsigned i = g.runIndex[k] / 3; i < g.runIndex[k + 1] / 3 && (int) i < nt; i++) orig[i] = k < g.runOriginalID.size () ? (int) g.runOriginalID[k] : 0;
	for (int i = 0; i < nt; i++) r.n[i] = unit (cross (r.v[r.t[i * 3 + 1]] - r.v[r.t[i * 3]], r.v[r.t[i * 3 + 2]] - r.v[r.t[i * 3]]));
	// the edges, each with its two triangles
	struct E { int a, b, t0, t1; };
	std::vector<E> es; es.reserve (nt * 3 / 2);
	std::unordered_map<unsigned long long, int> at; at.reserve (nt * 2);
	for (int i = 0; i < nt; i++)
		for (int k = 0; k < 3; k++)
		{
			int a = r.t[i * 3 + k], b = r.t[i * 3 + (k + 1) % 3];
			unsigned long long key = ekey (a, b);
			auto it = at.find (key);
			if (it == at.end ()) { at[key] = (int) es.size (); E e = { a, b, i, -1 }; es.push_back (e); }
			else es[it->second].t1 = i;
		}
	// the faces. First the flat patches: triangles joined when coplanar. Then the curved faces: patches joined
	// across a slight fold when they are strips of about the same width (a cylinder's facets) -- a wide flat
	// patch next to a fillet's first facet stays a face of its own, the line between them an edge.
	// A boolean operation leaves triangles without area along some edges: they are no face's; the triangles
	// around such a run are neighbours of one another through it.
	(void) orig;
	std::vector<char> thin (nt, 0);
	for (int i = 0; i < nt; i++)
	{
		V3 a = r.v[r.t[i * 3 + 1]] - r.v[r.t[i * 3]], b = r.v[r.t[i * 3 + 2]] - r.v[r.t[i * 3]], c = b - a;
		double longest = sqrt (std::max (dot (a, a), std::max (dot (b, b), dot (c, c))));
		if (len (cross (a, b)) < 2e-4 * longest) thin[i] = 1;
	}
	struct Pair { int t0, t1, a, b; };				// two triangles that touch, along the line a b
	std::vector<Pair> pairs; pairs.reserve (es.size ());
	std::vector<std::vector<int> > tadj (nt);
	for (const E &e : es) if (e.t1 >= 0)
	{
		if (!thin[e.t0] && !thin[e.t1]) { Pair q = { e.t0, e.t1, e.a, e.b }; pairs.push_back (q); }
		tadj[e.t0].push_back (e.t1); tadj[e.t1].push_back (e.t0);
	}
	std::vector<char> done (nt, 0); std::vector<int> ringOf (nt, -1); std::vector<std::vector<int> > rings;
	for (int i = 0; i < nt; i++)
	{
		if (!thin[i] || done[i]) continue;
		std::vector<int> comp (1, i), ring; done[i] = 1;
		for (size_t k = 0; k < comp.size (); k++)
			for (int o : tadj[comp[k]])
			{
				if (thin[o]) { if (!done[o]) { done[o] = 1; comp.push_back (o); } }
				else if (std::find (ring.begin (), ring.end (), o) == ring.end ()) ring.push_back (o);
			}
		int va = r.t[i * 3], vb = r.t[i * 3 + 1]; double far = -1;	// the run's line: its two farthest vertices
		for (int t : comp) for (int k = 0; k < 3; k++) for (int t2 : comp) for (int k2 = 0; k2 < 3; k2++)
		{
			double dd = len (r.v[r.t[t * 3 + k]] - r.v[r.t[t2 * 3 + k2]]);
			if (dd > far) { far = dd; va = r.t[t * 3 + k]; vb = r.t[t2 * 3 + k2]; }
		}
		for (size_t x = 0; x < ring.size (); x++) for (size_t y = x + 1; y < ring.size (); y++) { Pair q = { ring[x], ring[y], va, vb }; pairs.push_back (q); }
		for (int t : comp) ringOf[t] = (int) rings.size ();
		rings.push_back (ring);
	}
	std::vector<int> par (nt); for (int i = 0; i < nt; i++) par[i] = i;
	auto find = [&] (int i) { while (par[i] != i) { par[i] = par[par[i]]; i = par[i]; } return i; };
	const double c25 = cos (25 * PI / 180), c05 = cos (0.5 * PI / 180);
	for (const Pair &q : pairs) if (dot (r.n[q.t0], r.n[q.t1]) > c05) par[find (q.t0)] = find (q.t1);
	std::vector<int> pid (nt, -1), patch (nt, -1); int np2 = 0;
	for (int i = 0; i < nt; i++) if (!thin[i]) { int f = find (i); if (pid[f] < 0) pid[f] = np2++; patch[i] = pid[f]; }
	std::vector<std::vector<int> > pverts (np2);
	for (int i = 0; i < nt; i++) if (!thin[i]) for (int k = 0; k < 3; k++) pverts[patch[i]].push_back (r.t[i * 3 + k]);
	std::vector<int> par2 (np2); for (int i = 0; i < np2; i++) par2[i] = i;
	auto find2 = [&] (int i) { while (par2[i] != i) { par2[i] = par2[par2[i]]; i = par2[i]; } return i; };
	auto width = [&] (int p, const V3 &a, const V3 &b)
	{
		V3 dir = unit (b - a); double w = 0;
		for (int vi : pverts[p]) { V3 q = r.v[vi] - a; double d = len (q - dir * dot (q, dir)); if (d > w) w = d; }
		return w;
	};
	for (const Pair &q : pairs)
	{
		int pa = patch[q.t0], pb = patch[q.t1];
		if (pa == pb || dot (r.n[q.t0], r.n[q.t1]) <= c25 || find2 (pa) == find2 (pb)) continue;
		double wa = width (pa, r.v[q.a], r.v[q.b]), wb = width (pb, r.v[q.a], r.v[q.b]);
		if (wa <= wb * 3 && wb <= wa * 3) par2[find2 (pa)] = find2 (pb);
	}
	std::vector<int> id (np2, -1), npatch; r.ngrp = 0;
	for (int i = 0; i < np2; i++) { int f = find2 (i); if (id[f] < 0) { id[f] = r.ngrp++; npatch.push_back (0); } npatch[id[f]]++; }
	if (r.ngrp == 0) { r.ngrp = 1; npatch.push_back (1); }
	r.flat.assign (r.ngrp, 1); r.gn.assign (r.ngrp, V3 (0, 0, 1));
	for (int i = nt - 1; i >= 0; i--) if (!thin[i]) { int gi = id[find2 (patch[i])]; r.grp[i] = gi; r.gn[gi] = r.n[i]; r.flat[gi] = npatch[gi] == 1; }
	// (a triangle without area is of the face it lies along: its neighbour across its longest edge; a run of
	//  them is settled from its ends inward)
	std::vector<char> placed_ (nt, 0);
	for (int pass = 0; pass < 6; pass++)
		for (int i = 0; i < nt; i++)
		{
			if (!thin[i] || placed_[i]) continue;
			int order[3] = { 0, 1, 2 }; double l[3];
			for (int k = 0; k < 3; k++) l[k] = len (r.v[r.t[i * 3 + (k + 1) % 3]] - r.v[r.t[i * 3 + k]]);
			std::sort (order, order + 3, [&] (int a, int b) { return l[a] > l[b]; });
			for (int oi = 0; oi < 3 && !placed_[i]; oi++)
			{
				int k = order[oi]; auto it = at.find (ekey (r.t[i * 3 + k], r.t[i * 3 + (k + 1) % 3]));
				if (it == at.end ()) continue;
				const E &e = es[it->second]; int o = e.t0 == i ? e.t1 : e.t0;
				if (o < 0 || (thin[o] && !placed_[o])) { if (pass < 5) break; else continue; }
				r.grp[i] = r.grp[o]; r.n[i] = r.n[o]; placed_[i] = 1;
			}
		}
	for (int i = 0; i < nt; i++) if (thin[i] && !placed_[i])
	{
		const std::vector<int> &ring = rings[ringOf[i]];
		r.grp[i] = ring.empty () ? 0 : r.grp[ring[0]]; if (!ring.empty ()) r.n[i] = r.n[ring[0]];
	}
	// the chains: the edges between two faces, sorted by the pair, walked end to end
	std::unordered_map<unsigned long long, std::vector<int> > byPair;
	for (int i = 0; i < (int) es.size (); i++)
	{
		const E &e = es[i];
		if (e.t1 < 0) continue;
		int g0 = r.grp[e.t0], g1 = r.grp[e.t1];
		if (g0 != g1) byPair[ekey (g0, g1)].push_back (i);
		else if (!r.flat[g0] && !thin[e.t0] && !thin[e.t1]) { r.smooth.push_back (e.a); r.smooth.push_back (e.b); r.smooth.push_back (e.t0); r.smooth.push_back (e.t1); }
	}
	for (auto &kv : byPair)
	{
		std::vector<int> &list = kv.second;
		std::unordered_map<int, std::vector<int> > adj;			// vertex -> its edges of this pair
		for (int ei : list) { adj[es[ei].a].push_back (ei); adj[es[ei].b].push_back (ei); }
		std::unordered_map<int, char> used;
		for (int pass = 0; pass < 2; pass++)					// (open runs first, from an end; then the loops)
			for (int ei : list)
			{
				if (used[ei]) continue;
				int start = -1;
				if (pass == 0) { if (adj[es[ei].a].size () == 1) start = es[ei].a; else if (adj[es[ei].b].size () == 1) start = es[ei].b; else continue; }
				else start = es[ei].a;
				Chain c; c.closed = false; c.kind = 0;
				c.g0 = (int) (kv.first >> 32); c.g1 = (int) (kv.first & 0xFFFFFFFFu);
				int cur = start; c.pts.push_back (cur);
				for (;;)
				{
					int next = -1;
					for (int e2 : adj[cur]) if (!used[e2]) { next = e2; break; }
					if (next < 0) break;
					used[next] = 1; cur = es[next].a == cur ? es[next].b : es[next].a;
					if (cur == start) { c.closed = true; break; }
					c.pts.push_back (cur);
				}
				if (c.pts.size () >= 2) r.chains.push_back (c);
			}
	}
	// what each chain is
	for (Chain &c : r.chains)
	{
		const V3 &a = r.v[c.pts[0]], &b = r.v[c.pts.back ()];
		if (!c.closed && r.flat[c.g0] && r.flat[c.g1])
		{
			V3 dir = unit (b - a); bool straight = len (b - a) > 1e-6;
			for (int p : c.pts) { V3 q = r.v[p] - a; if (len (q - dir * dot (q, dir)) > 1e-4) { straight = false; break; } }
			if (straight) c.kind = 1;
		}
		else if (c.closed && c.pts.size () >= 8 && (r.flat[c.g0] != r.flat[c.g1]))
		{
			V3 ctr; for (int p : c.pts) ctr = ctr + r.v[p]; ctr = ctr * (1.0 / c.pts.size ());
			V3 nn = r.gn[r.flat[c.g0] ? c.g0 : c.g1];
			double R = len (r.v[c.pts[0]] - ctr); bool round = R > 1e-4;
			for (int p : c.pts) { V3 q = r.v[p] - ctr; if (fabs (len (q) - R) > 1e-3 * (1 + R) || fabs (dot (q, nn)) > 1e-4) { round = false; break; } }
			if (round) c.kind = 2;
		}
		else if (!c.closed && c.pts.size () >= 4 && (r.flat[c.g0] != r.flat[c.g1]))	// an arc: a rounded corner's rim
		{
			V3 nn = r.gn[r.flat[c.g0] ? c.g0 : c.g1], ctr;
			if (circle_of (a, r.v[c.pts[c.pts.size () / 2]], b, &ctr))
			{
				double R = len (a - ctr); bool round = R > 1e-4;
				for (int p : c.pts) { V3 q = r.v[p] - ctr; if (fabs (len (q) - R) > 1e-3 * (1 + R) || fabs (dot (q, nn)) > 1e-4) { round = false; break; } }
				if (round) c.kind = 3;
			}
		}
	}
}

// The centre of the circle through three points (false: they are in line).
static bool circle_of (const V3 &p0, const V3 &p1, const V3 &p2, V3 *ctr)
{
	V3 a = p1 - p0, b = p2 - p0, ab = cross (a, b); double d = 2 * dot (ab, ab);
	if (d < 1e-12) return false;
	*ctr = p0 + cross (b * dot (a, a) - a * dot (b, b), ab) * (1 / d);
	return true;
}
// The distance from p to the segment a b.
static inline double seg_dist (const V3 &p, const V3 &a, const V3 &b)
{
	V3 ab = b - a; double l2 = dot (ab, ab), k = l2 > 1e-18 ? dot (p - a, ab) / l2 : 0;
	if (k < 0) k = 0; if (k > 1) k = 1;
	return len (p - (a + ab * k));
}
static int chain_near (const RMesh &r, const V3 &p, double most = 1.0)
{
	int best = -1; double bd = most;
	for (int i = 0; i < (int) r.chains.size (); i++)
	{
		const Chain &c = r.chains[i]; int n = (int) c.pts.size ();
		for (int k = 0; k < (c.closed ? n : n - 1); k++)
		{
			double d = seg_dist (p, r.v[c.pts[k]], r.v[c.pts[(k + 1) % n]]);
			if (d < bd) { bd = d; best = i; }
		}
	}
	return best;
}
static V3 chain_mid (const RMesh &r, const Chain &c)
{
	if (c.kind == 1) return (r.v[c.pts[0]] + r.v[c.pts.back ()]) * 0.5;
	return r.v[c.pts[c.pts.size () / 4]];
}
static double chain_len (const RMesh &r, const Chain &c)
{
	double l = 0; int n = (int) c.pts.size ();
	for (int k = 0; k < (c.closed ? n : n - 1); k++) l += len (r.v[c.pts[(k + 1) % n]] - r.v[c.pts[k]]);
	return l;
}

// ---- the corner's solid: a fillet's or a chamfer's -------------------------------------------------------------------
static Manifold placed (const Manifold &m, const V3 &ex, const V3 &ey, const V3 &ez, const V3 &o)
{
	return m.Transform (manifold::mat3x4 (manifold::vec3 (ex.x, ex.y, ex.z), manifold::vec3 (ey.x, ey.y, ey.z),
					      manifold::vec3 (ez.x, ez.y, ez.z), manifold::vec3 (o.x, o.y, o.z)));
}
// The profile between the corner E and the arc (or the cut) joining the two faces at r from it, in the plane across
// the edge: d1, d2 the faces' directions away from the edge, o1, o2 their normals away from the profile's side.
static SimplePolygon corner_profile (V2 E, V2 d1, V2 d2, V2 o1, V2 o2, double r, bool fillet, int segs)
{
	double cs = d1.x * d2.x + d1.y * d2.y; if (cs > 1) cs = 1; if (cs < -1) cs = -1;
	double phi = acos (cs), t = fillet ? r / tan (phi / 2) : r;
	V2 T1 (E.x + d1.x * t, E.y + d1.y * t), T2 (E.x + d2.x * t, E.y + d2.y * t);
	SimplePolygon p;
	p.push_back ({T1.x, T1.y}); p.push_back ({T1.x + o1.x * EPS, T1.y + o1.y * EPS});
	p.push_back ({E.x + (o1.x + o2.x) * EPS, E.y + (o1.y + o2.y) * EPS});
	p.push_back ({T2.x + o2.x * EPS, T2.y + o2.y * EPS}); p.push_back ({T2.x, T2.y});
	if (fillet)
	{
		double bl = sqrt ((d1.x + d2.x) * (d1.x + d2.x) + (d1.y + d2.y) * (d1.y + d2.y)), k = r / sin (phi / 2) / (bl > 1e-9 ? bl : 1);
		V2 C (E.x + (d1.x + d2.x) * k, E.y + (d1.y + d2.y) * k);
		double a2 = atan2 (T2.y - C.y, T2.x - C.x), a1 = atan2 (T1.y - C.y, T1.x - C.x), da = a1 - a2;
		while (da > PI) da -= 2 * PI; while (da < -PI) da += 2 * PI;
		int n = (int) ceil (segs * fabs (da) / (2 * PI)); if (n < 2) n = 2;
		for (int i = 1; i < n; i++) { double a = a2 + da * i / n; p.push_back ({C.x + r * cos (a), C.y + r * sin (a)}); }
	}
	double area = 0;
	for (size_t i = 0; i < p.size (); i++) { const auto &a = p[i], &b = p[(i + 1) % p.size ()]; area += a.x * b.y - b.x * a.y; }
	if (area < 0) std::reverse (p.begin (), p.end ());
	return p;
}
// The third vertex of a triangle of face g that has the edge a b.
static bool third_of (const RMesh &r, int a, int b, int g, V3 *out)
{
	for (int i = 0; i < r.tris (); i++)
	{
		if (r.grp[i] != g) continue;
		const int *t = &r.t[i * 3];
		for (int k = 0; k < 3; k++)
			if ((t[k] == a && t[(k + 1) % 3] == b) || (t[k] == b && t[(k + 1) % 3] == a)) { *out = r.v[t[(k + 2) % 3]]; return true; }
	}
	return false;
}
// Is p inside the body? (a ray from it: an odd number of faces crossed)
static bool inside_mesh (const RMesh &r, const V3 &p)
{
	const V3 dir = unit (V3 (0.5377, 0.2917, 0.7911)); int n = 0;
	for (int i = 0; i < r.tris (); i++)
	{
		const V3 &a = r.v[r.t[i * 3]]; V3 e1 = r.v[r.t[i * 3 + 1]] - a, e2 = r.v[r.t[i * 3 + 2]] - a;
		V3 pv = cross (dir, e2); double det = dot (e1, pv);
		if (fabs (det) < 1e-14) continue;
		V3 tv = p - a; double u = dot (tv, pv) / det; if (u < 0 || u > 1) continue;
		V3 qv = cross (tv, e1); double v = dot (dir, qv) / det; if (v < 0 || u + v > 1) continue;
		if (dot (e2, qv) / det > 1e-9) n++;
	}
	return (n & 1) != 0;
}
// How far the cut of an outer edge may go on past one of its ends (from `end`, along `out`), so that it meets
// what was cut before across the corner -- as two skirting boards are mitred -- instead of stopping short of
// it. An edge rounded after its neighbour ends where the neighbour's curve begins: the corner itself is still
// there beyond. Followed along the edge's own line: a wall rising there (matter outside one of the two faces'
// planes) stops it; so does matter met again after a gap (another part of the body), or the edge going on as a
// sharp corner (then it simply ended: nothing to reach).
static double corner_reach (const RMesh &r, const V3 &end, const V3 &out, const V3 &n1, const V3 &n2, double rad, bool *wall)
{
	double most = 4 * rad, step = rad / 8; if (step > 0.5) step = 0.5; if (step < 0.05) step = 0.05;
	V3 in = (n1 + n2) * -0.02; bool gap = false;
	V3 q0 = end + out * 0.02;				// (a wall right at the end: the cut must not even stick out)
	*wall = inside_mesh (r, q0 + n1 * 0.05 - n2 * 0.05) || inside_mesh (r, q0 + n2 * 0.05 - n1 * 0.05);
	if (*wall) return 0;
	for (double t = step; t <= most; t += step)
	{
		V3 q = end + out * t;
		if (inside_mesh (r, q + n1 * 0.05 - n2 * 0.05) || inside_mesh (r, q + n2 * 0.05 - n1 * 0.05)) return t - step;	// a wall
		bool solid = inside_mesh (r, q + in);
		if (!solid) gap = true;
		else if (gap) return t - step;				// matter again
		else if (t > 2.5) return 0;				// the corner goes on, sharp
	}
	return gap ? most : 0;
}
// Where an outer edge ends on two faces already rounded (a box's upright edge under its two rounded top edges):
// the piece that turns those rounds around the corner -- their profile revolved a quarter turn about the new
// round's axis: a quarter of a torus, a piece of a sphere when the radii are the same. vi: the end's vertex;
// out: the edge's direction past it. false: the end is not such a corner (then the straight cut is all).
static bool corner_blend (const RMesh &r, const Chain &edge, int vi, const V3 &end, const V3 &out, const V3 &n1, const V3 &n2, double R, int segs, Manifold *res)
{
	if (fabs (dot (n1, n2)) > 1e-6) return false;
	double rr[2] = { -1, -1 };
	for (const Chain &c : r.chains)
	{
		if (&c == &edge || (c.pts[0] != vi && c.pts.back () != vi)) continue;
		// (the line where a side face turns into its round runs across the edge: a round that merely ends here -- the
		//  neighbour's, seen from its end -- is not one to turn around the corner)
		if (fabs (dot (unit (r.v[c.pts.back ()] - r.v[c.pts[0]]), out)) > 0.02) continue;
		for (int k = 0; k < 2; k++)
		{
			int side = k == 0 ? edge.g0 : edge.g1, other = c.g0 == side ? c.g1 : c.g1 == side ? c.g0 : -1;
			if (other < 0 || r.flat[other]) continue;
			double top = 0;					// how high the round next to this side face rises: its radius
			for (int i = 0; i < r.tris (); i++) if (r.grp[i] == other) for (int j = 0; j < 3; j++) top = std::max (top, dot (r.v[r.t[i * 3 + j]] - end, out));
			rr[k] = top;
		}
	}
	if (rr[0] < 0.01 || fabs (rr[0] - rr[1]) > 1e-3 || R < rr[0] - 1e-6) return false;		// (the two rounds may be one face: mitred, they run into each other)
	double q = rr[0];
	SimplePolygon poly = corner_profile (V2 (R, q), V2 (-1, 0), V2 (0, -1), V2 (0, 1), V2 (1, 0), q, true, segs);
	for (auto &p : poly) if (p.x < 0) p.x = 0;
	bool right = dot (cross (n1, n2), out) > 0; V3 e1 = right ? n1 : n2, e2 = right ? n2 : n1;
	*res = placed (Manifold::Revolve (Polygons { poly }, segs, 90), e1, e2, out, end - (n1 + n2) * R).AsOriginal ();
	return true;
}
// The solid for one edge: true and *add (an inner edge: it is added; else cut). why: what stops it.
static bool corner_solid (const RMesh &r, const Chain &c, double rad, bool fillet, int segs, Manifold *out, bool *add, const char **why)
{
	*why = "This edge cannot be rounded: only straight edges, circles and arcs";
	if (rad <= 1e-6) { *why = "The size must be more than 0"; return false; }
	int a = c.pts[0], b = c.pts[1];
	V3 A = r.v[a], B = r.v[c.pts.back ()], p1, p2;
	if (c.kind == 1)
	{
		V3 dir = unit (B - A), n1 = r.gn[c.g0], n2 = r.gn[c.g1];
		if (!third_of (r, a, b, c.g0, &p1) || !third_of (r, a, b, c.g1, &p2)) return false;
		V3 d1 = unit (cross (n1, dir)); if (dot (p1 - A, d1) < 0) d1 = -d1;
		V3 d2 = unit (cross (n2, dir)); if (dot (p2 - A, d2) < 0) d2 = -d2;
		bool convex = dot (d1, n2) < 0;
		V3 ex = d1, ey = unit (d2 - ex * dot (d2, ex)), ez = cross (ex, ey);
		double L = len (B - A), cphi = dot (d2, ex);
		if (fabs (cphi) > 0.9999) return false;
		auto to2 = [&] (const V3 &q) { return V2 (dot (q, ex), dot (q, ey)); };
		V2 o1 = to2 (n1), o2 = to2 (n2);
		if (!convex) { o1 = V2 (-o1.x, -o1.y); o2 = V2 (-o2.x, -o2.y); }
		SimplePolygon poly = corner_profile (V2 (0, 0), V2 (1, 0), to2 (d2), o1, o2, rad, fillet, segs);
		// (an outer edge's cut sticks out of its ends, and goes on to the corner where a neighbour was cut before)
		bool wallA = false, wallB = false;
		double pastA = convex ? corner_reach (r, A, -dir, n1, n2, rad, &wallA) : 0, pastB = convex ? corner_reach (r, B, dir, n1, n2, rad, &wallB) : 0;
		if (convex) { pastA += wallA ? 0 : EPS; pastB += wallB ? 0 : EPS; }
		bool fromA = dot (ez, dir) > 0;
		Manifold s = Manifold::Extrude (Polygons { poly }, L + pastA + pastB).Translate ({0, 0, -(fromA ? pastA : pastB)});
		s = placed (s, ex, ey, ez, fromA ? A : B).AsOriginal ();
		if (convex && fillet)				// ... and the rounds it ends on are turned around the corner
		{
			Manifold bl;
			if (corner_blend (r, c, c.pts[0], A, -dir, n1, n2, rad, segs, &bl)) s += bl;
			if (corner_blend (r, c, c.pts.back (), B, dir, n1, n2, rad, segs, &bl)) s += bl;
		}
		*out = s; *add = !convex;
		return true;
	}
	if (c.kind == 2 || c.kind == 3)
	{
		int gp = r.flat[c.g0] ? c.g0 : c.g1, gw = r.flat[c.g0] ? c.g1 : c.g0;
		V3 nn = r.gn[gp], ctr;
		if (c.kind == 2) { for (int p : c.pts) ctr = ctr + r.v[p]; ctr = ctr * (1.0 / c.pts.size ()); }
		else if (!circle_of (A, r.v[c.pts[c.pts.size () / 2]], B, &ctr)) return false;
		double R = len (A - ctr); V3 er = unit (A - ctr), et = cross (nn, er);
		if (!third_of (r, a, b, gp, &p1) || !third_of (r, a, b, gw, &p2)) return false;
		V3 nw; bool got = false;				// the wall's normal at this edge
		for (int i = 0; i < r.tris () && !got; i++)
		{
			if (r.grp[i] != gw) continue;
			const int *t = &r.t[i * 3];
			for (int k = 0; k < 3; k++) if ((t[k] == a && t[(k + 1) % 3] == b) || (t[k] == b && t[(k + 1) % 3] == a)) { nw = r.n[i]; got = true; }
		}
		if (!got) return false;
		nw = unit (nw - et * dot (nw, et));
		V3 d1 = dot (p1 - A, er) > 0 ? er : -er;
		V3 d2 = unit (cross (et, nw)); if (dot (p2 - A, d2) < 0) d2 = -d2;
		bool convex = dot (d1, nw) < 0;
		auto to2 = [&] (const V3 &q) { return V2 (dot (q, er), dot (q, nn)); };
		V2 o1 = to2 (nn), o2 = to2 (nw);
		if (!convex) { o1 = V2 (-o1.x, -o1.y); o2 = V2 (-o2.x, -o2.y); }
		SimplePolygon poly = corner_profile (V2 (R, 0), to2 (d1), to2 (d2), o1, o2, rad, fillet, segs);
		for (auto &q : poly) if (q.x < 0) { *why = "Too large for this edge"; return false; }
		double sweep = 360; V3 e1 = er;
		if (c.kind == 3)				// an arc: from its first point, the way its middle goes
		{
			auto ang = [&] (const V3 &p) { V3 q = p - ctr; return atan2 (dot (q, et), dot (q, er)) * 180 / PI; };
			double am = ang (r.v[c.pts[c.pts.size () / 2]]), ab = ang (B);
			if (am < 0 && ab > 0) ab -= 360; if (am > 0 && ab < 0) ab += 360;
			sweep = fabs (ab); if (ab < 0) e1 = unit (B - ctr);		// (turned the other way: from its last point)
			if (sweep < 0.5) return false;
		}
		Manifold s = Manifold::Revolve (Polygons { poly }, segs, sweep);
		*out = placed (s, e1, cross (nn, e1), nn, ctr).AsOriginal (); *add = !convex;
		return true;
	}
	return false;
}

// ---- a sketch, evaluated ------------------------------------------------------------------------------------------
struct SkShape
{
	std::vector<V2> pts;		// the element's drawing (a polyline in the plane)
	V2 start, end, centre;		// (centre: an arc's, a circle's)
	int outline;			// the outline it belongs to (-1: an open run)
};
struct SkEval
{
	std::vector<SkShape> shapes;	// one per element
	Polygons closed;		// the closed outlines
	int nclosed, nopen;
	V2 cur; bool has;		// where the next chained element starts
	double dir;			// the last line's direction (degrees)
};
static void sketch_eval (const std::vector<SkEl> &els, int segs, SkEval &ev)
{
	ev.shapes.clear (); ev.closed.clear (); ev.nclosed = ev.nopen = 0; ev.has = false; ev.dir = 0; ev.cur = V2 ();
	SimplePolygon run; std::vector<int> runEls; V2 first;
	auto flush = [&] (bool closed)
	{
		if (run.size () >= 3 && closed) { for (int i : runEls) ev.shapes[i].outline = ev.nclosed; ev.closed.push_back (run); ev.nclosed++; }
		else if (!runEls.empty ()) ev.nopen++;
		run.clear (); runEls.clear ();
	};
	for (int i = 0; i < (int) els.size (); i++)
	{
		const SkEl &e = els[i]; SkShape s; s.outline = -1;
		if (e.kind == SK_POINT) { s.start = s.end = s.centre = V2 (e.x, e.y); s.pts.push_back (s.start); ev.shapes.push_back (s); continue; }
		if (e.kind == SK_CIRCLE || e.kind == SK_RECT)
		{
			flush (false); ev.has = false;
			SimplePolygon p;
			if (e.kind == SK_CIRCLE)
			{
				double R = e.w / 2; s.centre = V2 (e.x, e.y); s.start = s.end = V2 (e.x + R, e.y);
				for (int k = 0; k < segs; k++) { double a = 2 * PI * k / segs; p.push_back ({e.x + R * cos (a), e.y + R * sin (a)}); }
			}
			else
			{
				double x0 = e.w < 0 ? e.x + e.w : e.x, y0 = e.h < 0 ? e.y + e.h : e.y, w = fabs (e.w), h = fabs (e.h);
				if (e.rel) { x0 = e.x - w / 2; y0 = e.y - h / 2; s.centre = V2 (e.x, e.y); }
				p = { {x0, y0}, {x0 + w, y0}, {x0 + w, y0 + h}, {x0, y0 + h} }; s.start = V2 (x0, y0); s.end = V2 (x0 + w, y0 + h);
			}
			for (auto &q : p) s.pts.push_back (V2 (q.x, q.y));
			s.pts.push_back (s.pts[0]);
			if ((e.kind == SK_CIRCLE && e.w > 1e-6) || (e.kind == SK_RECT && fabs (e.w) > 1e-6 && fabs (e.h) > 1e-6)) { s.outline = ev.nclosed++; ev.closed.push_back (p); }
			ev.shapes.push_back (s); continue;
		}
		if (e.kind == SK_CLOSE)
		{
			s.start = ev.cur; s.end = first; s.pts.push_back (ev.cur); s.pts.push_back (first);
			ev.shapes.push_back (s); runEls.push_back (i); flush (true); ev.has = false; continue;
		}
		bool chained = e.chain && ev.has;
		if (!chained) { flush (false); ev.cur = V2 (e.x, e.y); first = ev.cur; run.push_back ({ev.cur.x, ev.cur.y}); }
		s.start = ev.cur;
		if (e.kind == SK_LINE)
		{
			double ang = (e.rel && chained ? ev.dir + e.a : e.a) * PI / 180;
			ev.dir = ang * 180 / PI;
			ev.cur = V2 (ev.cur.x + e.len * cos (ang), ev.cur.y + e.len * sin (ang));
			s.pts.push_back (s.start); s.pts.push_back (ev.cur);
			run.push_back ({ev.cur.x, ev.cur.y});
		}
		else if (e.kind == SK_SPLINE)			// a smooth curve through its points (Catmull-Rom)
		{
			std::vector<V2> c; c.push_back (ev.cur); for (const V2 &q : e.pts) c.push_back (q);
			int n = (int) c.size (), per = segs / 8 < 4 ? 4 : segs / 8;
			s.pts.push_back (s.start);
			for (int k = 0; k + 1 < n; k++)
			{
				const V2 &p0 = c[k ? k - 1 : 0], &p1 = c[k], &p2 = c[k + 1], &p3 = c[k + 2 < n ? k + 2 : n - 1];
				if (hypot (p2.x - p1.x, p2.y - p1.y) < 1e-9) continue;
				for (int j = 1; j <= per; j++)
				{
					double t = (double) j / per, t2 = t * t, t3 = t2 * t;
					V2 q (0.5 * (2 * p1.x + (p2.x - p0.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (3 * p1.x - p0.x - 3 * p2.x + p3.x) * t3),
					      0.5 * (2 * p1.y + (p2.y - p0.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (3 * p1.y - p0.y - 3 * p2.y + p3.y) * t3));
					if (j == per) q = p2;
					s.pts.push_back (q); run.push_back ({q.x, q.y});
				}
			}
			if (s.pts.size () > 1) { const V2 &a = s.pts[s.pts.size () - 2], &b = s.pts.back (); ev.dir = atan2 (b.y - a.y, b.x - a.x) * 180 / PI; ev.cur = b; }
		}
		else
		{
			double ca = e.ca * PI / 180; V2 C (ev.cur.x + e.r * cos (ca), ev.cur.y + e.r * sin (ca));
			double a0 = ca + PI, sw = -e.sweep * PI / 180;
			int n = (int) ceil (segs * fabs (sw) / (2 * PI)); if (n < 2) n = 2;
			s.centre = C; s.pts.push_back (s.start);
			for (int k = 1; k <= n; k++)
			{
				double a = a0 + sw * k / n; V2 q (C.x + e.r * cos (a), C.y + e.r * sin (a));
				s.pts.push_back (q); run.push_back ({q.x, q.y});
			}
			ev.cur = s.pts.back (); ev.dir = (a0 + sw) * 180 / PI + (sw >= 0 ? 90 : -90);
		}
		s.end = ev.cur; ev.has = true;
		ev.shapes.push_back (s); runEls.push_back (i);
		if (run.size () >= 4 && hypot (ev.cur.x - first.x, ev.cur.y - first.y) < 1e-6) { run.pop_back (); flush (true); ev.has = false; }
	}
	flush (false);
}

// ---- the bodies, the document ---------------------------------------------------------------------------------------
struct Body
{
	int id; Manifold m; RMesh mesh;
	Body () : id (-1) {}
};
// A body as a Move step leaves it: scaled and turned about its centre (the middle of its box), then moved.
static Manifold moved (const Body &b, const Feature &f)
{
	Manifold m = b.m;
	if (f.turned () || f.scaled ())
	{
		V3 c = (b.mesh.lo + b.mesh.hi) * 0.5; m = m.Translate ({-c.x, -c.y, -c.z});
		if (f.scaled ()) m = m.Scale ({f.sc.x, f.sc.y, f.sc.z});
		if (f.turned ()) m = m.Rotate (f.rot.x, f.rot.y, f.rot.z);
		m = m.Translate ({c.x, c.y, c.z});
	}
	return m.Translate ({f.mv.x, f.mv.y, f.mv.z});
}
struct BodyProp { int id; char name[24]; unsigned colour; bool visible; };
static const unsigned BODY_COLOURS[5] = { 0x92AACC, 0xC4C8D0, 0xD6AA78, 0x96BE96, 0xD4847C };

struct Doc
{
	std::vector<Feature> feats;
	int upto;				// the steps replayed (feats.size (): all)
	int segs;				// sides to a circle
	std::vector<BodyProp> props;
	std::vector<Body> bodies;		// (made by rebuild)
	unsigned changes;
	Doc () : upto (0), segs (96), changes (0) {}

	Body *body (int id) { for (Body &b : bodies) if (b.id == id) return &b; return 0; }
	BodyProp &prop (int id)
	{
		for (BodyProp &p : props) if (p.id == id) return p;
		BodyProp p; p.id = id; p.colour = BODY_COLOURS[0]; p.visible = true;
		int n = 0; for (BodyProp &q : props) { (void) q; n++; }
		snprintf (p.name, sizeof p.name, "Body %d", n + 1);
		props.push_back (p); return props.back ();
	}
	int count (int kind, int before) const { int n = 0; for (int i = 0; i < before && i < (int) feats.size (); i++) if (feats[i].kind == kind) n++; return n; }
	void name_new (Feature &f) const { snprintf (f.name, sizeof f.name, "%s %d", KIND_NAME[f.kind], count (f.kind, (int) feats.size ()) + 1); }
	int tris () const { int n = 0; for (const Body &b : bodies) n += b.mesh.tris (); return n; }
	bool used (int sketch) const { for (int i = 0; i < upto && i < (int) feats.size (); i++) if (feats[i].kind == F_EXTRUDE && feats[i].sketch == sketch) return true; return false; }

	// The solid a step makes, before its operation (a box, a cylinder, an extrusion): what the tools preview.
	// bodies: the ones the step meets (a cut "through" is as long as its target).
	bool solid (const Feature &f, Manifold *out, const char **why) const
	{
		*why = "";
		const Body *tb = 0; for (const Body &b : bodies) if (b.id == f.target) tb = &b;
		double reach = 0;
		if (tb) reach = len (tb->mesh.hi - tb->mesh.lo) + 2;
		if (f.kind == F_BOX)
		{
			double w = fabs (f.w), d = fabs (f.d), h = fabs (f.h);
			if (w < 1e-6 || d < 1e-6 || h < 1e-6) { *why = "The box has no size"; return false; }
			double x0 = f.centred ? f.x - w / 2 : (f.w < 0 ? f.x + f.w : f.x), y0 = f.centred ? f.y - d / 2 : (f.d < 0 ? f.y + f.d : f.y);
			double z0 = f.h < 0 ? f.h : 0, over = f.op == OP_SUB && f.h < 0 ? EPS : 0;
			*out = placed (Manifold::Cube ({w, d, h + over}).Translate ({x0 - f.x, y0 - f.y, z0}).Rotate (0, 0, f.turn).Translate ({f.x, f.y, 0}), f.pl.u, f.pl.v, f.pl.n, f.pl.o + f.pl.n * f.z).AsOriginal ();
			return true;
		}
		if (f.kind == F_CYL)
		{
			double h = f.through && reach > 0 ? -reach : f.h;
			if (f.w < 1e-6 || fabs (h) < 1e-6) { *why = "The cylinder has no size"; return false; }
			double over = f.op == OP_SUB && h < 0 ? EPS : 0;
			*out = placed (Manifold::Cylinder (fabs (h) + over, f.w / 2, f.w / 2, segs).Translate ({f.x, f.y, h < 0 ? h : 0}), f.pl.u, f.pl.v, f.pl.n, f.pl.o + f.pl.n * f.z).AsOriginal ();
			return true;
		}
		if (f.kind == F_PYRAMID || f.kind == F_PRISM || f.kind == F_TAPER)
		{
			// a cylinder of n sides: the top a point (pyramid), as the base (prism) or of its own size (taper). Cut
			// into a body it is turned over: its base stays on the face it starts on.
			double h = f.through && reach > 0 && f.kind != F_PYRAMID ? -reach : f.h, lo = f.w, hi = f.kind == F_PYRAMID ? 0 : f.kind == F_PRISM ? lo : f.d;
			int sides = f.n >= 2.5 ? (int) (f.n + 0.5) : segs;
			if (lo < 1e-6 || fabs (h) < 1e-6) { *why = "The shape has no size"; return false; }
			if (hi < 0) hi = 0;
			double over = f.op == OP_SUB && h < 0 ? EPS : 0;
			Manifold m = Manifold::Cylinder (fabs (h), lo, hi, sides);
			if (h < 0) m = m.Mirror ({0, 0, 1});
			if (over > 0) m += Manifold::Cylinder (over, lo, lo, sides);		// (the cut starts just above the face)
			*out = placed (m.Rotate (0, 0, f.turn).Translate ({f.x, f.y, 0}), f.pl.u, f.pl.v, f.pl.n, f.pl.o + f.pl.n * f.z).AsOriginal ();
			return true;
		}
		if (f.kind == F_SPHERE)				// (its centre on the plane, where it was clicked)
		{
			if (f.w < 1e-6) { *why = "The sphere has no size"; return false; }
			*out = placed (Manifold::Sphere (f.w, segs).Translate ({f.x, f.y, 0}), f.pl.u, f.pl.v, f.pl.n, f.pl.o + f.pl.n * f.z).AsOriginal ();
			return true;
		}
		if (f.kind == F_TORUS)
		{
			// a circle of the tube's radius turned around the axis: it rests on the plane
			if (f.d < 1e-6 || f.w < 1e-6) { *why = "The torus has no size"; return false; }
			if (f.w <= f.d) { *why = "The ring must be larger than its tube"; return false; }
			int ts = segs / 2 < 12 ? 12 : segs / 2; SimplePolygon c;
			for (int i = 0; i < ts; i++) { double a = 2 * PI * i / ts; c.push_back ({f.w + f.d * cos (a), f.d + f.d * sin (a)}); }
			*out = placed (Manifold::Revolve (Polygons { c }, segs).Translate ({f.x, f.y, 0}), f.pl.u, f.pl.v, f.pl.n, f.pl.o + f.pl.n * f.z).AsOriginal ();
			return true;
		}
		if (f.kind == F_EXTRUDE)
		{
			if (f.sketch < 0 || f.sketch >= (int) feats.size () || feats[f.sketch].kind != F_SKETCH) { *why = "Its sketch is missing"; return false; }
			const Feature &sk = feats[f.sketch];
			SkEval ev; sketch_eval (sk.els, segs, ev);
			if (ev.closed.empty ()) { *why = "The sketch has no closed outline"; return false; }
			double h = f.through && reach > 0 ? -reach : f.h;
			if (fabs (h) < 1e-6) { *why = "The extrusion has no height"; return false; }
			double over = f.op == OP_SUB && h < 0 ? EPS : 0;
			CrossSection cs (ev.closed, CrossSection::FillRule::EvenOdd);
			*out = placed (Manifold::Extrude (cs.ToPolygons (), fabs (h) + over).Translate ({0, 0, h < 0 ? h : 0}), sk.pl.u, sk.pl.v, sk.pl.n, sk.pl.o).AsOriginal ();
			return true;
		}
		return false;
	}
	// The solids of a fillet / chamfer step on its body as it is now.
	bool corners (const Feature &f, std::vector<Manifold> *adds, std::vector<Manifold> *cuts, const char **why)
	{
		Body *b = body (f.target);
		*why = "Its body is missing";
		if (!b) return false;
		if (f.edges.empty ()) { *why = "No edge chosen"; return false; }
		for (const V3 &p : f.edges)
		{
			int ci = chain_near (b->mesh, p);
			*why = "An edge is no longer there";
			if (ci < 0) return false;
			Manifold s; bool add;
			if (!corner_solid (b->mesh, b->mesh.chains[ci], f.r, f.kind == F_FILLET, segs, &s, &add, why)) return false;
			(add ? adds : cuts)->push_back (s);
		}
		return true;
	}

	void apply (Feature &f, int index)
	{
		f.failed = false; f.err[0] = 0;
		const char *why = "";
		auto fail = [&] (const char *s) { f.failed = true; snprintf (f.err, sizeof f.err, "%s", s); };
		if (f.kind == F_SKETCH) return;
		if (makes_solid (f.kind))
		{
			Manifold s;
			if (!solid (f, &s, &why)) { fail (why); return; }
			Body *t = body (f.target);
			if (f.op == OP_NEW || !t) { Body nb; nb.id = index; nb.m = s; bodies.push_back (nb); prop (index); return; }
			Manifold res = f.op == OP_UNION ? t->m + s : f.op == OP_SUB ? t->m - s : (t->m ^ s);
			if (res.Status () != Manifold::Error::NoError || res.IsEmpty ()) { fail (res.IsEmpty () ? "Nothing would be left of the body" : "The operation failed"); return; }
			t->m = res; build_mesh (t->m, t->mesh);
			return;
		}
		if (f.kind == F_FILLET || f.kind == F_CHAMFER)
		{
			std::vector<Manifold> adds, cuts;
			if (!corners (f, &adds, &cuts, &why)) { fail (why); return; }
			Body *t = body (f.target); Manifold res = t->m;
			for (Manifold &s : cuts) res -= s;
			for (Manifold &s : adds) res += s;
			if (res.Status () != Manifold::Error::NoError || res.IsEmpty ()) { fail ("The operation failed"); return; }
			t->m = res; build_mesh (t->m, t->mesh);
			return;
		}
		if (f.kind == F_MOVE)
		{
			Body *t = body (f.target);
			if (!t) { fail ("Its body is missing"); return; }
			if (fabs (f.sc.x) < 1e-6 || fabs (f.sc.y) < 1e-6 || fabs (f.sc.z) < 1e-6) { fail ("A scale of 0 would leave nothing"); return; }
			Manifold res = moved (*t, f);
			if (res.Status () != Manifold::Error::NoError || res.IsEmpty ()) { fail ("The operation failed"); return; }
			if (f.clone)				// (the copy: a body of its own, named and coloured after the original at first)
			{
				bool known = false; for (BodyProp &p : props) if (p.id == index) known = true;
				if (!known) { BodyProp from = prop (f.target); BodyProp &np = prop (index); np.colour = from.colour; snprintf (np.name, sizeof np.name, "%.17s copy", from.name); }
				Body nb; nb.id = index; nb.m = res; bodies.push_back (nb);
				return;
			}
			t->m = res; build_mesh (t->m, t->mesh);
			return;
		}
		if (f.kind == F_COMBINE)
		{
			Body *t = body (f.target), *u = body (f.tool);
			if (!t || !u || t == u) { fail ("A body is missing"); return; }
			Manifold res = f.op == OP_SUB ? t->m - u->m : f.op == OP_INT ? (t->m ^ u->m) : t->m + u->m;
			if (res.Status () != Manifold::Error::NoError || res.IsEmpty ()) { fail (res.IsEmpty () ? "Nothing would be left of the body" : "The operation failed"); return; }
			t->m = res; build_mesh (t->m, t->mesh);
			int gone = f.tool;
			for (size_t i = 0; i < bodies.size (); i++) if (bodies[i].id == gone) { bodies.erase (bodies.begin () + i); break; }
			return;
		}
	}
	// Replay the history from the top.
	void rebuild ()
	{
		bodies.clear ();
		if (upto > (int) feats.size ()) upto = (int) feats.size ();
		if (upto < 0) upto = 0;
		for (int i = 0; i < upto; i++)
		{
			size_t before = bodies.size ();
			apply (feats[i], i);
			if (bodies.size () > before) build_mesh (bodies.back ().m, bodies.back ().mesh);
		}
	}
	void touch () { changes++; rebuild (); }

	// ---- the file: a line a step ----------------------------------------------------------------------------------
	std::string save () const
	{
		std::string s = "3dforge 1\n"; char b[400];
		snprintf (b, sizeof b, "segs %d\nupto %d\n", segs, upto); s += b;
		for (const BodyProp &p : props) { snprintf (b, sizeof b, "body %d %06X %d %s\n", p.id, p.colour, p.visible ? 1 : 0, p.name); s += b; }
		for (const Feature &f : feats)
		{
			snprintf (b, sizeof b, "step %s op %d target %d xy %.9g %.9g size %.9g %.9g %.9g flags %d %d sketch %d r %.9g mv %.9g %.9g %.9g tool %d name %s\n",
				  KIND_KEY[f.kind], f.op, f.target, f.x, f.y, f.w, f.d, f.h, f.centred ? 1 : 0, f.through ? 1 : 0, f.sketch, f.r,
				  f.mv.x, f.mv.y, f.mv.z, f.tool, f.name); s += b;
			if (f.kind >= F_PYRAMID) { snprintf (b, sizeof b, "sides %.9g\n", f.n); s += b; }
			if (f.z != 0) { snprintf (b, sizeof b, "lift %.9g\n", f.z); s += b; }
			if (f.turn != 0) { snprintf (b, sizeof b, "turned %.9g\n", f.turn); s += b; }
			if (f.kind == F_MOVE && f.clone) s += "clone 1\n";
			if (f.kind == F_MOVE && (f.turned () || f.scaled ())) { snprintf (b, sizeof b, "turn %.9g %.9g %.9g scale %.9g %.9g %.9g\n", f.rot.x, f.rot.y, f.rot.z, f.sc.x, f.sc.y, f.sc.z); s += b; }
			snprintf (b, sizeof b, "plane %.9g %.9g %.9g  %.9g %.9g %.9g  %.9g %.9g %.9g\n", f.pl.o.x, f.pl.o.y, f.pl.o.z, f.pl.u.x, f.pl.u.y, f.pl.u.z, f.pl.n.x, f.pl.n.y, f.pl.n.z); s += b;
			for (const SkEl &e : f.els)
			{
				snprintf (b, sizeof b, "el %d %d %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", e.kind, e.chain ? 1 : 0, e.rel ? 1 : 0, e.x, e.y, e.a, e.len, e.r, e.ca, e.sweep ? -e.sweep : 0, e.w, e.h); s += b;
				for (const V2 &q : e.pts) { snprintf (b, sizeof b, "sp %.9g %.9g\n", q.x, q.y); s += b; }
			}
			for (const V3 &p : f.edges) { snprintf (b, sizeof b, "edge %.9g %.9g %.9g\n", p.x, p.y, p.z); s += b; }
		}
		return s;
	}
	bool load (const char *text)
	{
		if (strncmp (text, "3dforge ", 8)) return false;
		feats.clear (); props.clear (); bodies.clear (); upto = -1; segs = 96;
		const char *p = text;
		while (*p)
		{
			const char *e = strchr (p, '\n'); size_t n = e ? (size_t) (e - p) : strlen (p);
			char line[500]; if (n >= sizeof line) n = sizeof line - 1; memcpy (line, p, n); line[n] = 0;
			if (n && line[n - 1] == '\r') line[n - 1] = 0;
			p = e ? e + 1 : p + n;
			if (!strncmp (line, "segs ", 5)) segs = atoi (line + 5);
			else if (!strncmp (line, "upto ", 5)) upto = atoi (line + 5);
			else if (!strncmp (line, "body ", 5))
			{
				BodyProp bp; int vis = 1, off = 0; bp.name[0] = 0;
				if (sscanf (line + 5, "%d %x %d %n", &bp.id, &bp.colour, &vis, &off) >= 3) { bp.visible = vis != 0; snprintf (bp.name, sizeof bp.name, "%s", line + 5 + off); props.push_back (bp); }
			}
			else if (!strncmp (line, "step ", 5))
			{
				Feature f; char kind[16] = ""; int c = 0, t = 0, off = 0;
				if (sscanf (line + 5, "%15s op %d target %d xy %lf %lf size %lf %lf %lf flags %d %d sketch %d r %lf mv %lf %lf %lf tool %d name %n",
					    kind, &f.op, &f.target, &f.x, &f.y, &f.w, &f.d, &f.h, &c, &t, &f.sketch, &f.r, &f.mv.x, &f.mv.y, &f.mv.z, &f.tool, &off) < 16) return false;
				f.kind = -1; for (int k = 0; k < F_KINDS; k++) if (!strcmp (kind, KIND_KEY[k])) f.kind = k;
				if (f.kind < 0) return false;
				f.centred = c != 0; f.through = t != 0; snprintf (f.name, sizeof f.name, "%s", line + 5 + off);
				feats.push_back (f);
			}
			else if (!strncmp (line, "sides ", 6) && !feats.empty ()) feats.back ().n = atof (line + 6);
			else if (!strncmp (line, "lift ", 5) && !feats.empty ()) feats.back ().z = atof (line + 5);
			else if (!strncmp (line, "turned ", 7) && !feats.empty ()) feats.back ().turn = atof (line + 7);
			else if (!strncmp (line, "clone ", 6) && !feats.empty ()) feats.back ().clone = atoi (line + 6) != 0;
			else if (!strncmp (line, "turn ", 5) && !feats.empty ())
			{ Feature &g = feats.back (); sscanf (line + 5, "%lf %lf %lf scale %lf %lf %lf", &g.rot.x, &g.rot.y, &g.rot.z, &g.sc.x, &g.sc.y, &g.sc.z); }
			else if (!strncmp (line, "plane ", 6) && !feats.empty ())
			{
				Plane &pl = feats.back ().pl;
				sscanf (line + 6, "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &pl.o.x, &pl.o.y, &pl.o.z, &pl.u.x, &pl.u.y, &pl.u.z, &pl.n.x, &pl.n.y, &pl.n.z);
				pl.v = cross (pl.n, pl.u);
			}
			else if (!strncmp (line, "el ", 3) && !feats.empty ())
			{
				SkEl el; int c = 0, r = 0;
				sscanf (line + 3, "%d %d %d %lf %lf %lf %lf %lf %lf %lf %lf %lf", &el.kind, &c, &r, &el.x, &el.y, &el.a, &el.len, &el.r, &el.ca, &el.sweep, &el.w, &el.h);
				el.chain = c != 0; el.rel = r != 0; el.sweep = -el.sweep; feats.back ().els.push_back (el);
			}
			else if (!strncmp (line, "sp ", 3) && !feats.empty () && !feats.back ().els.empty ())
			{
				V2 q; sscanf (line + 3, "%lf %lf", &q.x, &q.y); feats.back ().els.back ().pts.push_back (q);
			}
			else if (!strncmp (line, "edge ", 5) && !feats.empty ())
			{
				V3 q; sscanf (line + 5, "%lf %lf %lf", &q.x, &q.y, &q.z); feats.back ().edges.push_back (q);
			}
		}
		if (upto < 0 || upto > (int) feats.size ()) upto = (int) feats.size ();
		rebuild ();
		return true;
	}

	// ---- export ---------------------------------------------------------------------------------------------------
	// STL (binary or text) or OBJ of every shown body (only >= 0: that one alone).
	std::string exportMesh (bool obj, bool binary, int only, int *ntris) const
	{
		std::string s; int total = 0;
		std::vector<const Body *> list;
		for (const Body &b : bodies)
		{
			bool vis = true; for (const BodyProp &p : props) if (p.id == b.id) vis = p.visible;
			if (only >= 0 ? b.id == only : vis) { list.push_back (&b); total += b.mesh.tris (); }
		}
		if (ntris) *ntris = total;
		char b[200];
		if (obj)
		{
			s = "# 3DForge (Onyx)\n"; int base = 1;
			for (const Body *bd : list)
			{
				const char *nm = "body"; for (const BodyProp &p : props) if (p.id == bd->id) nm = p.name;
				snprintf (b, sizeof b, "o %s\n", nm); s += b;
				for (const V3 &p : bd->mesh.v) { snprintf (b, sizeof b, "v %.6g %.6g %.6g\n", p.x, p.y, p.z); s += b; }
				for (int i = 0; i < bd->mesh.tris (); i++) { snprintf (b, sizeof b, "f %d %d %d\n", bd->mesh.t[i * 3] + base, bd->mesh.t[i * 3 + 1] + base, bd->mesh.t[i * 3 + 2] + base); s += b; }
				base += (int) bd->mesh.v.size ();
			}
			return s;
		}
		if (binary)
		{
			s.assign (80, ' '); memcpy (&s[0], "3DForge (Onyx) binary STL", 25);
			unsigned n = (unsigned) total; s.append ((const char *) &n, 4);
			for (const Body *bd : list)
				for (int i = 0; i < bd->mesh.tris (); i++)
				{
					float f[12]; const V3 &nn = bd->mesh.n[i];
					f[0] = (float) nn.x; f[1] = (float) nn.y; f[2] = (float) nn.z;
					for (int k = 0; k < 3; k++) { const V3 &p = bd->mesh.v[bd->mesh.t[i * 3 + k]]; f[3 + k * 3] = (float) p.x; f[4 + k * 3] = (float) p.y; f[5 + k * 3] = (float) p.z; }
					s.append ((const char *) f, 48); s.append ("\0\0", 2);
				}
			return s;
		}
		s = "solid 3dforge\n";
		for (const Body *bd : list)
			for (int i = 0; i < bd->mesh.tris (); i++)
			{
				const V3 &nn = bd->mesh.n[i];
				snprintf (b, sizeof b, "facet normal %.6g %.6g %.6g\n outer loop\n", nn.x, nn.y, nn.z); s += b;
				for (int k = 0; k < 3; k++) { const V3 &p = bd->mesh.v[bd->mesh.t[i * 3 + k]]; snprintf (b, sizeof b, "  vertex %.6g %.6g %.6g\n", p.x, p.y, p.z); s += b; }
				s += " endloop\nendfacet\n";
			}
		s += "endsolid 3dforge\n";
		return s;
	}
};

// What a step says of itself in the history (its values in a few words).
static void describe (const Doc &d, const Feature &f, char *out, int cap)
{
	auto num = [] (double v, char *b) { snprintf (b, 24, "%.4g", fabs (v) < 1e-9 ? 0.0 : v); };
	char a[24], b[24], c[24];
	const char *op = f.op == OP_SUB ? " \xC2\xB7 cut" : f.op == OP_INT ? " \xC2\xB7 intersect" : "";
	switch (f.kind)
	{
	case F_BOX: num (fabs (f.w), a); num (fabs (f.d), b); num (fabs (f.h), c); snprintf (out, cap, "%s \xC3\x97 %s \xC3\x97 %s%s", a, b, c, op); break;
	case F_CYL: num (f.w, a); num (fabs (f.h), b); if (f.through) snprintf (out, cap, "\xC3\x98 %s \xC2\xB7 through%s", a, op); else snprintf (out, cap, "\xC3\x98 %s \xC3\x97 %s%s", a, b, op); break;
	case F_PYRAMID: case F_PRISM: case F_TAPER:
	{
		char s[24]; num (f.w, a); num (fabs (f.h), b); if (f.n >= 2.5) snprintf (s, sizeof s, "%d sides", (int) (f.n + 0.5)); else snprintf (s, sizeof s, "round");
		if (f.kind == F_TAPER) { num (f.d, c); snprintf (out, cap, "R %s\xE2\x80\x93%s \xC3\x97 %s \xC2\xB7 %s%s", a, c, b, s, op); }
		else snprintf (out, cap, "R %s \xC3\x97 %s \xC2\xB7 %s%s", a, b, s, op);
		break;
	}
	case F_TORUS: num (f.w, a); num (f.d, b); snprintf (out, cap, "R %s \xC2\xB7 tube %s%s", a, b, op); break;
	case F_SPHERE: num (f.w, a); snprintf (out, cap, "R %s%s", a, op); break;
	case F_SKETCH: { SkEval ev; sketch_eval (f.els, 24, ev); snprintf (out, cap, "%d outline%s%s", ev.nclosed, ev.nclosed == 1 ? "" : "s", ev.nopen ? " \xC2\xB7 open" : ""); break; }
	case F_EXTRUDE: num (fabs (f.h), a); if (f.through) snprintf (out, cap, "through%s", op); else snprintf (out, cap, "%s%s", a, op); break;
	case F_FILLET: num (f.r, a); snprintf (out, cap, "R %s \xC2\xB7 %d edge%s", a, (int) f.edges.size (), f.edges.size () == 1 ? "" : "s"); break;
	case F_CHAMFER: num (f.r, a); snprintf (out, cap, "%s \xC2\xB7 %d edge%s", a, (int) f.edges.size (), f.edges.size () == 1 ? "" : "s"); break;
	case F_MOVE: num (f.mv.x, a); num (f.mv.y, b); num (f.mv.z, c); snprintf (out, cap, "%s, %s, %s%s%s", a, b, c, f.turned () ? " \xC2\xB7 turned" : "", f.scaled () ? " \xC2\xB7 scaled" : ""); if (f.clone) { size_t k = strlen (out); snprintf (out + k, cap - k, " \xC2\xB7 a copy"); } break;
	case F_COMBINE: snprintf (out, cap, "%s", OP_NAME[f.op]); break;
	default: out[0] = 0;
	}
	(void) d;
}

} // namespace forge

#endif
