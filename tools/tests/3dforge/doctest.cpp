/*
 * doctest.cpp -- 3DForge's document (user/Apps/3dforge/fdoc.h) on the PC: the bracket of the mock-ups made by the
 * steps the user would take -- a box, its four corners rounded, a second box joined, its top corners rounded, a
 * hole through the wall, a sketch (two circles and a slot of lines and arcs) cut through, the holes chamfered, the
 * inner edge filled -- then saved, loaded and exported. tools/tests/run_3dforge_test.sh.
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include "Apps/3dforge/fdoc.h"
using namespace forge;

static int fails = 0;
static void check (bool ok, const char *what) { printf ("%-4s %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; }
static void steps (Doc &d)
{
	for (size_t i = 0; i < d.feats.size (); i++)
	{
		char s[96]; describe (d, d.feats[i], s, sizeof s);
		printf ("     %-12s %-22s %s\n", d.feats[i].name, s, d.feats[i].failed ? d.feats[i].err : "");
	}
}
static Feature &add (Doc &d, int kind) { Feature f; f.kind = kind; d.name_new (f); d.feats.push_back (f); d.upto = (int) d.feats.size (); return d.feats.back (); }

int main (int argc, char **argv)
{
	Doc d; const double poly = 0.5 * 96 * sin (2 * PI / 96);
	Feature *f = &add (d, F_BOX); f->centred = false; f->w = 90; f->d = 60; f->h = 8;			// Box 1 on the ground
	d.rebuild ();
	check (d.bodies.size () == 1 && fabs (d.bodies[0].m.Volume () - 43200) < 1e-6, "a box on the ground");
	const RMesh &m0 = d.bodies[0].mesh;
	int straight = 0; for (const Chain &c : m0.chains) if (c.kind == 1) straight++;
	check (m0.ngrp == 6 && straight == 12, "its six faces, its twelve straight edges");

	f = &add (d, F_FILLET); f->target = 0; f->r = 8;								// Fillet 1: the four corners
	for (double x : {0.0, 90.0}) for (double y : {0.0, 60.0}) f->edges.push_back (V3 (x, y, 4));
	d.rebuild ();
	double plate = (5400 - (4 - poly) * 64) * 8;
	check (!d.feats[1].failed && fabs (d.bodies[0].m.Volume () - plate) < 0.01, "four outer edges rounded (cut)");

	f = &add (d, F_BOX); f->target = 0; f->op = OP_UNION; f->centred = false;					// Box 2 on the plate's top face
	f->pl = plane_of (V3 (0, 0, 8), V3 (0, 0, 1)); f->x = 0; f->y = 52; f->w = 90; f->d = 8; f->h = 37;
	d.rebuild ();
	double wall = plate + 90 * 8 * 37;
	check (!d.feats[2].failed && d.bodies.size () == 1 && fabs (d.bodies[0].m.Volume () - wall) < 0.01, "a box joined on a face (one body)");

	f = &add (d, F_FILLET); f->target = 0; f->r = 12;								// Fillet 2: the wall's top corners
	f->edges.push_back (V3 (0, 56, 45)); f->edges.push_back (V3 (90, 56, 45));
	d.rebuild ();
	wall -= (2 - poly / 2) * 144 * 8;
	check (!d.feats[3].failed && fabs (d.bodies[0].m.Volume () - wall) < 0.01, "two more outer edges rounded");

	f = &add (d, F_CYL); f->target = 0; f->op = OP_SUB; f->through = true;					// Cylinder 1: through the wall
	f->pl = plane_of (V3 (0, 52, 0), V3 (0, -1, 0)); { V2 c = f->pl.to (V3 (45, 52, 27)); f->x = c.x; f->y = c.y; } f->w = 22; f->h = -8;
	d.rebuild ();
	wall -= poly * 121 * 8;
	check (!d.feats[4].failed && fabs (d.bodies[0].m.Volume () - wall) < 0.01 && d.bodies[0].m.Genus () == 1, "a cylinder cut through");

	f = &add (d, F_SKETCH); f->pl = plane_of (V3 (0, 0, 8), V3 (0, 0, 1));					// Sketch 1 on the top face
	SkEl e; e.kind = SK_CIRCLE; e.x = 16; e.y = 22; e.w = 9; f->els.push_back (e); e.x = 74; f->els.push_back (e);
	e = SkEl (); e.kind = SK_LINE; e.x = 36; e.y = 17; e.a = 0; e.len = 18; f->els.push_back (e);
	e = SkEl (); e.kind = SK_ARC; e.chain = true; e.r = 5; e.ca = 90; e.sweep = 180; f->els.push_back (e);
	e = SkEl (); e.kind = SK_LINE; e.chain = true; e.a = 180; e.len = 18; f->els.push_back (e);
	e = SkEl (); e.kind = SK_ARC; e.chain = true; e.r = 5; e.ca = -90; e.sweep = 180; f->els.push_back (e);
	SkEval ev; sketch_eval (f->els, 96, ev);
	check (ev.nclosed == 3 && ev.nopen == 0, "a sketch: two circles and a slot, three closed outlines");
	int sk = (int) d.feats.size () - 1;
	f = &add (d, F_EXTRUDE); f->target = 0; f->op = OP_SUB; f->sketch = sk; f->through = true; f->h = -8;	// Extrude 1
	d.rebuild ();
	wall -= (2 * poly * 4.5 * 4.5 + poly * 25 + 180) * 8;
	check (!d.feats[6].failed && fabs (d.bodies[0].m.Volume () - wall) < 0.01 && d.bodies[0].m.Genus () == 4, "the sketch cut through");

	f = &add (d, F_CHAMFER); f->target = 0; f->r = 2.5;								// Chamfer 1: the two holes' rims
	f->edges.push_back (V3 (16 + 4.5, 22, 8)); f->edges.push_back (V3 (74 + 4.5, 22, 8));
	d.rebuild ();
	double cham = 2 * (poly * 2.5 / 3 * (4.5 * 4.5 + 4.5 * 7 + 7 * 7) - poly * 4.5 * 4.5 * 2.5);
	check (!d.feats[7].failed && fabs (d.bodies[0].m.Volume () - (wall - cham)) < 0.05, "two edges on a circle chamfered");
	wall -= cham;

	f = &add (d, F_FILLET); f->target = 0; f->r = 6; f->edges.push_back (V3 (45, 52, 8));			// Fillet 3: the inner edge
	d.rebuild ();
	wall += 90 * 36 - poly / 4 * 36 * 90;
	check (!d.feats[8].failed && fabs (d.bodies[0].m.Volume () - wall) < 0.05, "an inner edge filled");
	steps (d);
	printf ("     the bracket: %d triangles, %.1f cm3, %d faces\n", d.tris (), d.bodies[0].m.Volume () / 1000, d.bodies[0].mesh.ngrp);

	// a value changed up the history: everything after it follows
	d.feats[0].h = 10; d.feats[2].pl = plane_of (V3 (0, 0, 10), V3 (0, 0, 1)); d.feats[5].pl = d.feats[2].pl;
	for (int i : {7}) for (V3 &p : d.feats[i].edges) p.z = 10; for (V3 &p : d.feats[3].edges) p.z = 47;
	d.feats[8].edges[0].z = 10;
	d.rebuild ();
	bool all = true; for (Feature &g : d.feats) if (g.failed) all = false;
	check (all && d.bodies[0].m.Genus () == 4, "the plate made thicker: the eight steps after it replayed");
	d.feats[0].h = 8; d.feats[2].pl = plane_of (V3 (0, 0, 8), V3 (0, 0, 1)); d.feats[5].pl = d.feats[2].pl;
	for (V3 &p : d.feats[7].edges) p.z = 8; d.feats[8].edges[0].z = 8; for (V3 &p : d.feats[3].edges) p.z = 45;
	d.rebuild ();

	// the file, the exports
	snprintf (d.prop (0).name, sizeof d.prop (0).name, "Bracket");
	std::string text = d.save (); Doc d2;
	if (argc > 1) { FILE *o = fopen (argv[1], "wb"); if (o) { fwrite (text.data (), 1, text.size (), o); fclose (o); } }	// (the sample: SD:/docs/3d/bracket.3df)
	check (d2.load (text.c_str ()) && d2.feats.size () == 9 && d2.bodies.size () == 1 && fabs (d2.bodies[0].m.Volume () - wall) < 0.05, "saved and loaded");
	int nt = 0; std::string stl = d.exportMesh (false, true, -1, &nt);
	check ((int) stl.size () == 84 + 50 * nt && nt == d.tris (), "binary STL");
	std::string obj = d.exportMesh (true, false, -1, &nt);
	check (obj.find ("\nf ") != std::string::npos, "OBJ");

	// a second body, moved, then the two combined
	f = &add (d, F_BOX); f->centred = true; f->x = 45; f->y = 20; f->w = 20; f->d = 20; f->h = 30;
	d.rebuild ();
	check (d.bodies.size () == 2, "a second body");
	int id2 = d.bodies[1].id;
	f = &add (d, F_MOVE); f->target = id2; f->mv = V3 (0, 0, 2);
	f = &add (d, F_COMBINE); f->target = 0; f->tool = id2; f->op = OP_SUB;
	d.rebuild ();
	check (d.bodies.size () == 1 && !d.feats.back ().failed, "moved, then cut from the first");
	// a body turned and scaled about its centre, then moved
	{
		Doc s; Feature *g = &add (s, F_BOX); g->centred = false; g->w = 10; g->d = 20; g->h = 30; s.rebuild ();
		g = &add (s, F_MOVE); g->target = 0; g->rot = V3 (0, 0, 90); g->sc = V3 (2, 2, 2); g->mv = V3 (100, 0, 0); s.rebuild ();
		V3 sz = s.bodies[0].mesh.hi - s.bodies[0].mesh.lo, mid = (s.bodies[0].mesh.hi + s.bodies[0].mesh.lo) * 0.5;
		check (!s.feats[1].failed && fabs (s.bodies[0].m.Volume () - 48000) < 1e-3 && fabs (sz.x - 40) < 1e-6 && fabs (sz.y - 20) < 1e-6 && fabs (sz.z - 60) < 1e-6
		       && fabs (mid.x - 105) < 1e-6 && fabs (mid.y - 10) < 1e-6 && fabs (mid.z - 15) < 1e-6, "a body turned a quarter, doubled, moved: about its centre");
		g = &add (s, F_MOVE); g->target = 0; g->clone = true; g->mv = V3 (0, 50, 0); g->sc = V3 (0.5, 0.5, 0.5); s.rebuild ();
		check (s.bodies.size () == 2 && fabs (s.bodies[0].m.Volume () - 48000) < 1e-3 && fabs (s.bodies[1].m.Volume () - 6000) < 1e-3 && !strcmp (s.prop (s.bodies[1].id).name, "Body 1 copy"),
		       "a clone: the original stays, the copy is a new body, halved and moved");
		Doc s2; check (s2.load (s.save ().c_str ()) && s2.bodies.size () == 2 && fabs (s2.bodies[0].m.Volume () - 48000) < 1e-3 && fabs (s2.bodies[1].m.Volume () - 6000) < 1e-3, "... saved and loaded");
	}
	// the other shapes: a pyramid, a prism and a tapered prism of N sides, a torus
	{
		Doc s; auto area = [] (int n, double r) { return 0.5 * n * r * r * sin (2 * PI / n); };
		Feature *g = &add (s, F_PYRAMID); g->w = 20; g->h = 30; g->n = 4; s.rebuild ();
		check (s.bodies.size () == 1 && fabs (s.bodies[0].m.Volume () - area (4, 20) * 30 / 3) < 1e-6 && s.bodies[0].mesh.ngrp == 5, "a pyramid on a square: five faces");
		g = &add (s, F_PRISM); g->x = 60; g->w = 15; g->h = 20; g->n = 6; s.rebuild ();
		check (s.bodies.size () == 2 && fabs (s.bodies[1].m.Volume () - area (6, 15) * 20) < 1e-6 && s.bodies[1].mesh.ngrp == 8, "a prism of six sides: eight faces");
		g = &add (s, F_TAPER); g->x = 120; g->w = 20; g->d = 10; g->h = 25; g->n = 5; s.rebuild ();
		double a1 = area (5, 20), a2 = area (5, 10);
		check (s.bodies.size () == 3 && fabs (s.bodies[2].m.Volume () - 25.0 / 3 * (a1 + a2 + sqrt (a1 * a2))) < 1e-6, "a tapered prism of five sides");
		g = &add (s, F_TORUS); g->x = 60; g->y = 80; g->w = 25; g->d = 6; s.rebuild ();
		double tv = 2 * PI * PI * 25 * 36;
		check (s.bodies.size () == 4 && fabs (s.bodies[3].m.Volume () - tv) < tv * 0.01 && s.bodies[3].m.Genus () == 1 && fabs (s.bodies[3].mesh.lo.z) < 1e-6, "a torus resting on the ground");
		g = &add (s, F_PYRAMID); g->target = s.bodies[1].id; g->op = OP_SUB; g->pl = plane_of (V3 (0, 0, 20), V3 (0, 0, 1)); g->x = 60; g->w = 8; g->h = -12; g->n = 3; s.rebuild ();
		check (!s.feats.back ().failed && fabs (s.bodies[1].m.Volume () - (area (6, 15) * 20 - area (3, 8) * 12 / 3)) < 1e-3, "a pyramid cut into the prism, its base on the face");
		g = &add (s, F_SPHERE); g->x = 120; g->y = 80; g->w = 15; s.rebuild ();
		double sv = 4.0 / 3 * PI * 15 * 15 * 15;
		check (s.bodies.size () == 5 && fabs (s.bodies[4].m.Volume () - sv) < sv * 0.01 && s.bodies[4].mesh.ngrp == 1, "a sphere: one face");
		Doc s2; check (s2.load (s.save ().c_str ()) && s2.bodies.size () == 5 && s2.feats[2].n == 5, "the shapes saved and loaded");
		steps (s);
	}
	printf (fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
