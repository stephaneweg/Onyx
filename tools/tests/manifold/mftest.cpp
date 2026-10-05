/*
 * mftest.cpp -- Manifold built for Onyx (user/Libs/manifold/libmanifold.a), tried in AArch64 under qemu
 * (tools/tests/run_manifold_test.sh): the three operations on two boxes, a hole, and the bracket of
 * 3DForge's mock-ups made step by step as the app will make it (docs/3dforge/README.md) -- each result a
 * closed solid of the volume expected. Prints the triangles and the time of each step.
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include <stdio.h>
#include <math.h>
#include <sys/time.h>
#include "manifold/manifold.h"
#include "manifold/cross_section.h"
using namespace manifold;

static int fails = 0;
static double now () { struct timeval tv; gettimeofday (&tv, 0); return tv.tv_sec + tv.tv_usec / 1e6; }

static void check (const char *name, const Manifold &m, double volume, double tol, double t0)
{
	double v = m.Volume ();
	bool ok = m.Status () == Manifold::Error::NoError && !m.IsEmpty () && fabs (v - volume) <= tol;
	printf ("%-4s %-28s %6d triangles  volume %10.2f (want %10.2f)  %5.1f ms\n", ok ? "ok" : "FAIL", name,
		(int) m.NumTri (), v, volume, (now () - t0) * 1000);
	if (!ok) fails++;
}

static Manifold box (double x, double y, double z, double w, double d, double h) { return Manifold::Cube ({w, d, h}).Translate ({x, y, z}); }
static Manifold cyl_z (double x, double y, double z0, double z1, double r, double r2 = -1)
{
	return Manifold::Cylinder (z1 - z0, r, r2 < 0 ? r : r2).Translate ({x, y, z0});
}
static CrossSection rounded (double w, double d, double r)
{
	return CrossSection::Square ({w - 2 * r, d - 2 * r}).Translate ({r, r}).Offset (r, CrossSection::JoinType::Round, 2.0, 96);
}

int main ()
{
	const double PI = 3.14159265358979323846;
	Quality::SetCircularSegments (96);
	double t = now ();
	Manifold a = box (0, 0, 0, 10, 10, 10), b = box (5, 5, 5, 10, 10, 10);
	check ("union", a + b, 1875, 1e-6, t); t = now ();
	check ("subtract", a - b, 875, 1e-6, t); t = now ();
	check ("intersect", a ^ b, 125, 1e-6, t); t = now ();
	// a 96-sided hole: the polygon's area, not the circle's
	double poly = 0.5 * 96 * sin (2 * PI / 96);
	check ("hole", box (0, 0, 0, 90, 60, 8) - cyl_z (16, 22, -1, 9, 4.5), 90 * 60 * 8 - poly * 4.5 * 4.5 * 8, 1e-3, t);

	// the bracket, its nine steps
	const double PW = 90, PD = 60, PT = 8, WT = 8, WH = 45;
	t = now ();
	Manifold m = Manifold::Extrude (rounded (PW, PD, 8).ToPolygons (), PT);				// Box 1 + Fillet 1
	double want = (PW * PD - (4 - poly) * 64) * PT;						// (a corner: a square less a quarter disc)
	check ("plate, rounded corners", m, want, 0.01, t); t = now ();
	CrossSection prof = rounded (PW, WH + 30, 12).Translate ({0, -30}) ^ CrossSection::Square ({PW, WH});
	m += Manifold::Extrude (prof.ToPolygons (), WT).Rotate (90, 0, 0).Translate ({0, PD, 0});	// Box 2 + Fillet 2
	want += (PW * WH - (2 - poly / 2) * 144) * WT - (PW * WT - (2 - poly / 2) * 64) * PT;		// (less what the plate had there)
	check ("wall joined", m, want, 0.01, t); t = now ();
	m -= Manifold::Cylinder (WT + 2, 11, 11).Rotate (-90, 0, 0).Translate ({45, PD - WT - 1, 27});	// Cylinder 1
	want -= poly * 121 * WT;
	check ("wall's hole", m, want, 0.01, t); t = now ();
	CrossSection slot = CrossSection::Hull (std::vector<CrossSection> {CrossSection::Circle (5).Translate ({36, 22}), CrossSection::Circle (5).Translate ({54, 22})});
	m -= cyl_z (16, 22, -1, PT + 1, 4.5) + cyl_z (74, 22, -1, PT + 1, 4.5) + Manifold::Extrude (slot.ToPolygons (), PT + 2).Translate ({0, 0, -1});
	want -= (2 * poly * 4.5 * 4.5 + poly * 25 + 18 * 10) * PT;
	check ("sketch extruded, cut", m, want, 0.01, t); t = now ();
	for (double hx : {16.0, 74.0}) m -= cyl_z (hx, 22, PT - 2.5, PT + 0.01, 4.5, 7.0);			// Chamfer 1
	want -= 2 * (poly * 2.51 / 3 * (4.5 * 4.5 + 4.5 * 7 + 7 * 7) - poly * 4.5 * 4.5 * 2.5 - poly * 6.995 * 6.995 * 0.01);	// (a cone's slice less the hole, less what is above the plate)
	check ("chamfered holes", m, want, 0.05, t); t = now ();
	double y0 = PD - WT;									// Fillet 3: a prism less a cylinder
	m += box (0, y0 - 6, PT, PW, 6.01, 6) - Manifold::Cylinder (PW + 2, 6, 6).Rotate (0, 90, 0).Translate ({-1, y0 - 6, PT + 6});
	want += PW * 36 - poly / 4 * 36 * PW;
	check ("fillet on the inner edge", m, want, 0.05, t);
	MeshGL g = m.GetMeshGL ();
	printf ("the bracket: %d triangles, %d vertices, genus %d, %.1f cm3\n", (int) g.NumTri (), (int) g.NumVert (), m.Genus (), m.Volume () / 1000);
	if (m.Genus () != 4) { printf ("FAIL genus (want 4: three holes in the plate, one in the wall)\n"); fails++; }
	printf (fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
