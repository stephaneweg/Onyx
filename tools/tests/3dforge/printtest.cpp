/*
 * printtest.cpp -- 3DForge's Print (user/Apps/3dforge/fprint.h) on the PC: the sample bracket cut into layers for a
 * resin printer -- the pictures' sizes, what is lit against the body's volume, the runs read back, the file written
 * and read back. With a file of the printer's own slicer as argv[2] (not in the repository: the user's): it is read,
 * written again and must come back the same to the byte, and so must its first layer's picture, decoded and coded
 * again. tools/tests/run_3dforge_test.sh; argv[1]: the sample part.
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include "Apps/3dforge/fprint.h"
using namespace forge;

static int fails = 0;
static void check (bool ok, const char *what) { printf ("%-4s %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; }
static bool slurp (const char *path, std::string &s)
{
	FILE *f = fopen (path, "rb"); if (!f) return false;
	char b[65536]; size_t n; s.clear (); while ((n = fread (b, 1, sizeof b, f)) > 0) s.append (b, n);
	fclose (f); return true;
}

int main (int argc, char **argv)
{
	std::string text;
	if (!slurp (argc > 1 ? argv[1] : "sdcard/docs/3d/bracket.3df", text)) { printf ("FAIL the sample part is missing\n"); return 1; }
	Doc d; check (d.load (text.c_str ()) && d.bodies.size () == 1, "the sample part");

	PrintSetup s; s.on = true; s.body = d.bodies[0].id;
	PrintJob job; int calls = 0;
	Manifold flat = print_place (d.bodies[0].m, s);
	bool ok = print_slice (flat, s, job, [&] (int, int) { calls++; return true; });
	const PmFile &f = job.file; size_t pixels = (size_t) s.machine.resX * s.machine.resY;
	check (ok && f.layers.size () == 900 && calls == 900, "the bracket, 45 mm high: 900 layers of 0.05 mm");
	// what is lit, layer by layer, is the body's volume (a pixel: 35 x 35 micrometres)
	double lit = 0; bool sized = true; std::vector<unsigned char> px;
	for (size_t i = 0; i < f.layers.size (); i++)
	{
		lit += f.layers[i].lit;
		if (i % 150 == 3) { sized = sized && pm_decode (f.layers[i].rle, pixels, px); unsigned c = 0; for (unsigned char v : px) c += v; sized = sized && c == f.layers[i].lit; }
	}
	double vol = lit * 0.035 * 0.035 * 0.05, real = d.bodies[0].m.Volume ();
	printf ("     lit: %.0f mm3, the body: %.0f mm3; %.0f min, %.1f ml\n", vol, real, job.minutes, job.volume);
	check (sized, "... every picture is the screen's size, its lit pixels counted");
	check (fabs (vol - real) < real * 0.004, "... what is lit is the body's volume");
	check (!job.tooLarge && job.onPlate && !job.islands && fabs (f.hi.z - 45) < 1e-6 && fabs (f.lo.x + 45) < 1e-6 && fabs (f.hi.y - 30) < 1e-6, "... on the plate's middle, inside the printer");
	check (f.layers[0].exposure == 25 && f.layers[4].exposure == 25 && f.layers[5].exposure < 25 && f.layers[14].exposure > 2.5f && f.layers[15].exposure == 2.5f,
	       "... the first layers long, then down to the normal exposure");
	// a pixel's place: the plate's top face seen from above -- the round hole at (16, 22) of the body, 4.5 mm in radius
	{
		pm_decode (f.layers[100].rle, pixels, px);		// (z = 5: in the plate)
		auto at = [&] (double x, double y) { int c = (int) floor ((x - 45) / 0.035 + 2048), r = (int) floor (1280 - (y - 30) / 0.035); return px[(size_t) r * 4096 + c]; };
		check (!at (16, 22) && at (16, 28) && at (2, 30) && !at (-1, 30) && !at (16, 22 + 4.4) && at (16, 22 + 4.6), "... X to the right, the rows from +Y down: a hole where it is");
		PrintSetup m2 = s; m2.mirror = true; PmLayer l; Manifold pl = d.bodies[0].m.Translate ({-45, -30, 0}); pm_raster (pl.Slice (5), m2.machine, true, l);
		std::vector<unsigned char> q; pm_decode (l.rle, pixels, q);
		int c = (int) floor (-(16 - 45) / 0.035 + 2048), r = (int) floor (1280 - (22 - 30) / 0.035);
		check (!q[(size_t) r * 4096 + c] && l.lit == f.layers[100].lit, "... mirrored: the same, flipped left to right");
	}
	// the file, written and read back
	std::string bytes = pm_write (f); PmFile g;
	printf ("     the file: %.1f MB\n", bytes.size () / 1048576.0);
	check (pm_read (bytes, g) && g.layers.size () == 900 && g.layers[450].rle == f.layers[450].rle && g.machine.resX == 4096 && g.exposure == 2.5f && g.hi.z == 45
	       && !strcmp (g.machine.name, "Anycubic Photon Mono 2") && pm_write (g) == bytes, "the file written, read back, written again: the same");
	check (!print_slice (Manifold (), s, job, [] (int, int) { return true; }) && job.err[0], "nothing to print: said");
	check (!print_slice (flat, s, job, [] (int i, int) { return i < 10; }), "stopped when asked");
	{
		// tilted and lifted: it hangs in the air until it is held -- pillars under what leans, a raft
		PrintSetup t = s; t.tiltX = 24; t.lift = 5; t.resin.layer = 0.2; Manifold up = print_place (d.bodies[0].m, t); auto b = up.BoundingBox ();
		PrintJob j; print_slice (up, t, j, [] (int, int) { return true; });
		check (fabs (b.min.z - 5) < 1e-6 && fabs (b.min.x + b.max.x) < 1e-6 && !j.onPlate, "tilted, lifted, in the plate's middle: nothing lies on the plate, and it is said");
		print_supports_auto (up, t, t.tips); Manifold sup = print_support_solid (up, t);
		printf ("     %d pillars, %.1f ml of supports\n", (int) t.tips.size (), sup.Volume () / 1000);
		check (t.tips.size () > 20 && t.tips.size () < 400 && sup.Status () == Manifold::Error::NoError && sup.Volume () > 500, "its supports: pillars and a raft");
		Manifold all = up + sup; print_slice (all, t, j, [] (int, int) { return true; });
		printf ("     held: on the plate %d, islands %d (the first at %.2f), too large %d\n", j.onPlate, j.islands, j.islandAt, j.tooLarge);
		check (j.onPlate && !j.islands && !j.tooLarge && j.file.preview.size () == 224 * 168, "held: on the plate, nothing starts in mid-air");
		// a ball hung above the plate beside it: an island
		Manifold ball = all + Manifold::Sphere (3, 24).Translate ({40, 30, 20}); print_slice (ball, t, j, [] (int, int) { return true; });
		printf ("     with a ball: islands %d (the first at %.2f)\n", j.islands, j.islandAt);
		check (j.islands == 1 && fabs (j.islandAt - 17) < 0.25, "a piece that starts in mid-air is found, with its height");
		PrintSetup u; print_load (u, print_save (t).c_str ());
		check (u.on && u.tips.size () == t.tips.size () && u.tiltX == 24 && u.lift == 5 && u.resin.layer == 0.2 && !strcmp (u.resin.name, "Standard") && u.raft, "the setup saved and loaded");
	}

	if (argc > 2)		// a file of the printer's own slicer
	{
		std::string ref; PmFile h;
		if (!slurp (argv[2], ref) || !pm_read (ref, h)) check (false, "the reference file read");
		else
		{
			std::string again = pm_write (h); size_t diff = 0, first = 0;
			for (size_t i = 0; i < ref.size () && i < again.size (); i++) if (ref[i] != again[i]) { if (!diff) first = i; diff++; }
			printf ("     reference: %d layers, %s, %d x %d, %d bytes; written again: %d bytes, %d differ (the first at %d)\n", (int) h.layers.size (), h.machine.name,
				h.machine.resX, h.machine.resY, (int) ref.size (), (int) again.size (), (int) diff, (int) first);
			check (again == ref, "the printer's slicer's file, read and written again: the same to the byte");
			size_t n = (size_t) h.machine.resX * h.machine.resY; bool same = true;
			for (size_t i : { (size_t) 0, h.layers.size () / 2, h.layers.size () - 1 })
			{
				std::vector<unsigned char> p; same = same && pm_decode (h.layers[i].rle, n, p);
				PmRuns r; size_t a = 0; while (a < n) { size_t b = a; while (b < n && p[b] == p[a]) b++; r.add (p[a] != 0, b - a); a = b; } r.flush ();
				same = same && r.out == h.layers[i].rle && r.count == h.layers[i].lit;
			}
			check (same, "... its pictures decoded and coded again: the same runs, the same count");
		}
	}
	printf (fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
