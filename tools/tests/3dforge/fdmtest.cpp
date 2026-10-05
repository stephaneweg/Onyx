/*
 * fdmtest.cpp -- 3DForge's filament printing (user/Apps/3dforge/ffdm.h) on the PC: the nozzle's path for a block and
 * for the sample bracket -- the walls, the solid layers above and below, the infill, the skirt --, what is pushed out
 * against the body's volume, and the plain G-code. tools/tests/run_3dforge_test.sh; argv[1]: the sample part;
 * argv[2]: a file to dump a layer's paths into (a picture is made of it: "kind closed x y x y ...", a line a path),
 * argv[3]: which layer.
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include "Apps/3dforge/ffdm.h"
using namespace forge;

static int fails = 0;
static void check (bool ok, const char *what) { printf ("%-4s %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; }
static int count (const FdmLayer &l, int kind) { int n = 0; for (const FdmPath &p : l.paths) if (p.kind == kind) n++; return n; }

int main (int argc, char **argv)
{
	// a block of 20 x 20 x 10: 50 layers of 0.2
	Manifold cube = Manifold::Cube ({20, 20, 10}); FdmSettings s; FdmJob j;
	bool ok = fdm_paths (cube, s, j, [] (int, int) { return true; });
	check (ok && j.layers.size () == 50 && fabs (j.layers[0].z - 0.2) < 1e-9 && fabs (j.layers[49].z - 10) < 1e-9, "a block: 50 layers of 0.2 mm");
	check (count (j.layers[0], FDM_SKIRT) == 2 && count (j.layers[1], FDM_SKIRT) == 0, "... a skirt of two loops around the first layer only");
	check (count (j.layers[10], FDM_OUTER) == 1 && count (j.layers[10], FDM_INNER) == 1, "... two walls a layer");
	check (count (j.layers[0], FDM_SOLID) > 30 && count (j.layers[3], FDM_SOLID) > 30 && count (j.layers[4], FDM_SOLID) == 0 && count (j.layers[4], FDM_SPARSE) > 4
	       && count (j.layers[45], FDM_SOLID) == 0 && count (j.layers[46], FDM_SOLID) > 30, "... four solid layers below and above, the infill between");
	{
		// the outer wall is half a line inside the outline; every point inside the block
		double lo = 1e9, hi = -1e9; bool in = true;
		for (const FdmPath &p : j.layers[10].paths) for (const V2 &q : p.pts) { if (p.kind == FDM_OUTER) { lo = std::min (lo, q.x); hi = std::max (hi, q.x); } in = in && q.x > -1e-6 && q.x < 20 + 1e-6 && q.y > -1e-6 && q.y < 20 + 1e-6; }
		check (fabs (lo - 0.225) < 1e-6 && fabs (hi - 19.775) < 1e-6 && in, "... the outer wall half a line inside; nothing outside the block");
	}
	{
		// the options: three walls, two solid layers, a grid of 40 %: thicker shells, the same share either way
		FdmSettings o; o.walls = 3; o.top = 2; o.bottom = 2; o.infill = 0.4; FdmJob a, b2; fdm_paths (cube, o, a, [] (int, int) { return true; });
		o.pattern = 1; fdm_paths (cube, o, b2, [] (int, int) { return true; });
		check (count (a.layers[10], FDM_INNER) == 2 && count (a.layers[1], FDM_SOLID) > 30 && count (a.layers[2], FDM_SOLID) == 0 && fabs (a.length - b2.length) < a.length * 0.03
		       && count (b2.layers[10], FDM_SPARSE) > count (a.layers[10], FDM_SPARSE) / 2, "the options: the walls, the solid layers, the infill's share and its pattern");
	}
	// filled whole, what is pushed out is the body
	s.infill = 1; fdm_paths (cube, s, j, [] (int, int) { return true; });
	double skirt = 0; for (const FdmPath &p : j.layers[0].paths) if (p.kind == FDM_SKIRT) for (size_t k = 1; k <= p.pts.size (); k++) skirt += hypot (p.pts[k % p.pts.size ()].x - p.pts[k - 1].x, p.pts[k % p.pts.size ()].y - p.pts[k - 1].y);
	double pushed = j.volume - skirt * s.width * s.layer;
	printf ("     a block filled whole: %.0f mm3 pushed out for 4000\n", pushed);
	check (fabs (pushed - 4000) < 4000 * 0.06, "filled whole: what is pushed out is the block's volume");
	check (!fdm_paths (Manifold (), s, j, [] (int, int) { return true; }) && j.err[0] && !fdm_paths (cube, s, j, [] (int i, int) { return i < 5; }), "nothing to print: said; stopped when asked");

	// the sample bracket
	FILE *f = fopen (argc > 1 ? argv[1] : "sdcard/docs/3d/bracket.3df", "rb");
	if (!f) { printf ("FAIL the sample part is missing\n"); return 1; }
	std::string text (200000, 0); text.resize (fread (&text[0], 1, text.size (), f)); fclose (f);
	Doc d; check (d.load (text.c_str ()) && d.bodies.size () == 1, "the sample part");
	s = FdmSettings (); ok = fdm_paths (d.bodies[0].m, s, j, [] (int, int) { return true; });
	size_t paths = 0; for (const FdmLayer &l : j.layers) paths += l.paths.size ();
	printf ("     the bracket: %d layers, %d paths, %.1f m drawn, %.1f cm3 pushed out (the body: %.1f), %.2f m of filament\n", (int) j.layers.size (), (int) paths, j.length / 1000,
		j.volume / 1000, d.bodies[0].m.Volume () / 1000, j.filament / 1000);
	check (ok && j.layers.size () == 225 && j.volume > 0.3 * d.bodies[0].m.Volume () && j.volume < 0.95 * d.bodies[0].m.Volume (), "the bracket: 225 layers, lighter than the body filled whole");
	// the plate's top (z = 8) closes under the open air: the layers under it are solid there, though deep in the body
	check (count (j.layers[38], FDM_SOLID) > 50 && count (j.layers[30], FDM_SOLID) == 0 && count (j.layers[30], FDM_SPARSE) > 5, "... solid under the plate's top, sparse deeper");
	FdmMachine m = FDM_MACHINE0; double minutes = 0; std::string g = fdm_gcode (j, s, m, "bracket.3df", &minutes);
	double emax = 0, xmin = 1e9, xmax = -1e9, zmax = 0; int lines = 0; bool form = g.find ("G28\n") != std::string::npos && g.find ("M109 S210\n") != std::string::npos && g.compare (g.size () - 4, 4, "M84\n") == 0;
	for (size_t i = 0; i < g.size (); )
	{
		size_t e = g.find ('\n', i); std::string l = g.substr (i, e - i); i = e + 1; lines++;
		if (l[0] == ';') continue;
		size_t xp = l.find (" X"), ep = l.find (" E"), zp = l.find (" Z");
		if (xp != std::string::npos && l != "G0 X0 Y0") { double x = atof (l.c_str () + xp + 2); xmin = std::min (xmin, x); xmax = std::max (xmax, x); }
		if (ep != std::string::npos) emax = std::max (emax, atof (l.c_str () + ep + 2));
		if (zp != std::string::npos) zmax = std::max (zmax, atof (l.c_str () + zp + 2));
	}
	printf ("     G-code: %d lines, %.1f MB, X %.1f .. %.1f, E up to %.0f mm, about %d h %02d\n", lines, g.size () / 1048576.0, xmin, xmax, emax, (int) minutes / 60, (int) minutes % 60);
	check (form && fabs (emax - j.filament) < j.filament * 0.01 && xmin > 110 - 45 - 7 && xmax < 110 + 45 + 7 && fabs (zmax - 55) < 1e-6, "the plain G-code: its start and end, the filament it takes, on the bed's middle");

	if (argc > 2)
	{
		int li = argc > 3 ? atoi (argv[3]) : 20; FILE *o = fopen (argv[2], "w");
		if (o && li < (int) j.layers.size ())
		{
			for (const FdmPath &p : j.layers[li].paths) { fprintf (o, "%d %d", p.kind, p.closed ? 1 : 0); for (const V2 &q : p.pts) fprintf (o, " %.3f %.3f", q.x, q.y); fprintf (o, "\n"); }
			fclose (o);
		}
	}
	printf (fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
