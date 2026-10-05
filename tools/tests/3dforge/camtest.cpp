/*
 * camtest.cpp -- 3DForge's Manufacture (user/Apps/3dforge/fcam.h) on the PC: the sample bracket set up in its stock,
 * cleared in levels then cut out by a contour with tabs; the checks the app shows before writing the G-code -- no
 * fast move through matter, the body never cut into, the lowest height --, the G-code itself, an operation on one
 * face, the setup saved and loaded. tools/tests/run_3dforge_test.sh; argv[1]: the sample part.
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include "Apps/3dforge/fcam.h"
using namespace forge;

static int fails = 0;
static void check (bool ok, const char *what) { printf ("%-4s %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; }

int main (int argc, char **argv)
{
	FILE *f = fopen (argc > 1 ? argv[1] : "sdcard/docs/3d/bracket.3df", "rb");
	if (!f) { printf ("FAIL the sample part is missing\n"); return 1; }
	std::string text (200000, 0); text.resize (fread (&text[0], 1, text.size (), f)); fclose (f);
	Doc d; check (d.load (text.c_str ()) && d.bodies.size () == 1, "the sample part");

	CamSetup c; c.on = true; c.body = d.bodies[0].id; c.origin = 18;		// top, front left
	V3 lo, hi; cam_stock (d, c, &lo, &hi);
	check (fabs (hi.x - lo.x - 100) < 1e-6 && fabs (hi.y - lo.y - 70) < 1e-6 && fabs (hi.z - lo.z - 47) < 1e-6, "the stock: the body and its margins");
	V3 o = cam_origin (c, lo, hi); check (fabs (o.x + 5) < 1e-9 && fabs (o.y + 5) < 1e-9 && fabs (o.z - 47) < 1e-9, "the origin: top, front left");
	c.xdir = 1; V3 w = cam_work (c, o, V3 (-5, 10, 47)); check (fabs (w.x - 15) < 1e-9 && fabs (w.y) < 1e-9, "X turned to the back: Y follows, to the left"); c.xdir = 0;

	{
		// a stock of a fixed size: the body in its middle, on its underside -- then moved in it
		CamSetup f = c; f.fixed = true; f.size = V3 (120, 80, 50); V3 a, b; cam_stock (d, f, &a, &b);
		bool mid = fabs (a.x + 15) < 1e-9 && fabs (b.y - 70) < 1e-9 && fabs (a.z) < 1e-9 && fabs (b.z - 50) < 1e-9;
		f.off = V3 (10, -5, 3); cam_stock (d, f, &a, &b);
		CamSetup g; cam_load (g, cam_save (f).c_str ());
		check (mid && fabs (a.x + 25) < 1e-9 && fabs (b.x - 95) < 1e-9 && fabs (a.y + 5) < 1e-9 && fabs (a.z + 3) < 1e-9 && fabs (b.z - 47) < 1e-9 && g.off.x == 10 && g.off.z == 3,
		       "a stock of a fixed size: the body in its middle, or moved in it");
	}
	CamOp clear; clear.kind = CAM_CLEAR; snprintf (clear.name, sizeof clear.name, "Clearing 1"); clear.stepdown = 6; c.ops.push_back (clear);
	CamPaths p; bool ok = cam_compute (d, c, p);
	printf ("     clearing: %d moves, %.0f mm cut, %.0f min, lowest Z %.2f%s\n", (int) p.moves.size (), p.length, p.minutes, p.lowestZ, c.ops[0].failed ? c.ops[0].err : "");
	check (ok && !c.ops[0].failed && p.moves.size () > 500, "a clearing in levels");
	check (!p.hitFast, "... no fast move through matter");
	check (!p.gouge, "... the body is never cut into");
	check (fabs (p.lowestZ - 8.2) < 1e-6, "... down to the plate's top, 0.2 mm left");
	// what is left on the plate's top is the 0.2 mm asked, the wall's foot is still in the stock's shadow
	{
		int i = (int) ((45 - (p.lo.x - c.tool.dia)) / p.cell), j = (int) ((35 - (p.lo.y - c.tool.dia)) / p.cell);
		check (fabs (p.hm[(size_t) j * p.nx + i] - 8.2) < 1e-4, "... the plate's top is cleared to its level");
	}

	CamOp ct; ct.kind = CAM_CONTOUR; snprintf (ct.name, sizeof ct.name, "Contour 1"); ct.stepdown = 2; ct.fromAuto = false; ct.from = 8.2; c.ops.push_back (ct);
	ok = cam_compute (d, c, p);
	printf ("     + contour: %d moves in all, %.0f min, lowest Z %.2f%s\n", (int) p.moves.size (), p.minutes, p.lowestZ, c.ops[1].failed ? c.ops[1].err : "");
	check (ok && !c.ops[1].failed && !p.hitFast && !p.gouge, "a contour after it: nothing hit, nothing cut into");
	check (fabs (p.lowestZ + 0.5) < 1e-6, "... half a millimetre under the body");
	int tabMoves = 0; for (const CamMove &m : p.moves) if (m.op == 1 && m.kind && fabs (m.p.z - 1.5) < 1e-6) tabMoves++;
	check (tabMoves >= 8, "... its tabs left 1.5 mm high");
	check (!p.outside && !p.tooDeep, "inside the machine's travel, within the tool's cutting length");

	// a contour that starts from the stock's top without a clearing before: the fast moves are still above the stock
	{
		CamSetup c2 = c; c2.ops.erase (c2.ops.begin ()); c2.ops[0].fromAuto = true; CamPaths p2; cam_compute (d, c2, p2);
		check (!p2.hitFast, "a contour alone, from the stock's top: no fast move through matter");
	}

	int lines = 0; std::string g = cam_gcode (c, p, "bracket.3df", &lines);
	bool form = g.find ("G21 G90 G17 G94\n") != std::string::npos && g.find ("M3 S12000\n") != std::string::npos && g.find ("G4 P3\n") != std::string::npos
		    && g.size () > 20 && g.compare (g.size () - 3, 3, "M2\n") == 0;
	double zmin = 1e9, zmax = -1e9, xmin = 1e9, xmax = -1e9; bool only = true;
	for (size_t i = 0; i < g.size (); )
	{
		size_t e = g.find ('\n', i); std::string l = g.substr (i, e - i); i = e + 1;
		if (l.empty () || l[0] == '(') continue;
		if (l[0] != 'G' && l[0] != 'M') only = false;
		size_t zp = l.find (" Z"), xp = l.find (" X");
		if (zp != std::string::npos && l[1] != '4') { double z = atof (l.c_str () + zp + 2); zmin = std::min (zmin, z); zmax = std::max (zmax, z); }
		if (xp != std::string::npos) { double x = atof (l.c_str () + xp + 2); xmin = std::min (xmin, x); xmax = std::max (xmax, x); }
	}
	printf ("     G-code: %d lines, %d KB, Z %.3f .. %.3f, X %.3f .. %.3f\n", lines, (int) (g.size () / 1024), zmin, zmax, xmin, xmax);
	check (form && only, "the G-code: its start, its end, only G and M words");
	check (fabs (zmax - 10) < 1e-6 && fabs (zmin + 47.5) < 1e-3, "... from the origin on the stock's top: Z from +10 (safe) to -47.5");
	check (xmin > -3.5 && xmax < 103.5, "... X within the stock and the tool's radius around it");

	// one face only: the plate's top cleared, nothing else touched
	{
		CamSetup cf = c; cf.ops.clear (); CamOp a; a.kind = CAM_CLEAR; a.stepdown = 6; a.useFace = true; a.facePt = V3 (45, 35, 8); snprintf (a.name, sizeof a.name, "Clearing"); cf.ops.push_back (a);
		CamPaths pf; bool okf = cam_compute (d, cf, pf);
		int far = 0; for (const CamMove &m : pf.moves) if (m.kind && m.p.y > 52.5) far++;		// (behind the wall's front: not this face's)
		printf ("     on a face: %d moves, %.0f mm, lowest Z %.2f, fast-hit %d gouge %d behind %d %s\n", (int) pf.moves.size (), pf.length, pf.lowestZ, pf.hitFast, pf.gouge, far, cf.ops[0].failed ? cf.ops[0].err : "");
		check (okf && !pf.hitFast && !pf.gouge && fabs (pf.lowestZ - 8.2) < 1e-6 && far == 0, "a clearing on one face: above it only, down to it");
		cf.ops[0].kind = CAM_CONTOUR; cf.ops[0].inside = true; cf.ops[0].facePt = V3 (45, 56, 45); cf.ops[0].fromAuto = true; cf.ops[0].stepdown = 1;
		okf = cam_compute (d, cf, pf);
		check (!okf || cf.ops[0].failed || !pf.gouge, "a contour inside a narrow face: refused, or without cutting into the body");
	}

	CamSetup c3; cam_load (c3, cam_save (c).c_str ());
	check (c3.on && c3.ops.size () == 2 && c3.ops[1].kind == CAM_CONTOUR && c3.ops[1].ntabs == 4 && fabs (c3.tool.dia - 6) < 1e-9 && !strcmp (c3.machine.name, c.machine.name) && c3.origin == 18,
	       "the setup saved and loaded");
	printf (fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
