//
// fprint.h -- 3DForge, Print: a body cut into layers for a resin printer with an LCD screen (an Anycubic Photon: its
// .pm3n / .pwmb... files, the "ANYCUBIC" container of Photon Workshop, version 5.17). Portable (the PC tests it:
// tools/tests/3dforge/printtest.cpp).
//
//   * A layer is a picture of the screen, one bit a pixel (lit: the resin hardens), the body's section at that
//     height: Manifold's Slice, filled row by row (even-odd), written as runs -- 16 bits each, big-endian: 4 bits
//     of grey (0 or 15 here), 12 bits of length ("pw0Img").
//   * The file: a mark, a table of where its blocks are, then HEADER (the pixel's size, the layer's height, the
//     exposures, the lift, the screen's size, the volume, the time), PREVIEW (a small RGB565 picture), the greys'
//     table, LAYERDEF (a line a layer: where its picture is, its exposure, its lit pixels), EXTRA (the lift in two
//     stages, for the first layers and the others), MACHINE, SOFTWARE, MODEL (the body's box), the pictures.
//     The layout was read from a file of the user's printer (a Photon Mono 2) and is written back to the byte
//     (the test); what its fields mean follows UVtools' and dbcook/anycubic-sla-printers' notes. The SOFTWARE
//     block's strings are written as that file has them: the printer's firmware is not known to accept others.
//   * The picture's axes: X to the right, from the screen's middle; the rows go from +Y down to -Y.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef FORGE_FPRINT_H
#define FORGE_FPRINT_H

#include "fdoc.h"

namespace forge {

struct PrintMachine { char name[32]; int resX, resY; double pixel, sizeX, sizeY, sizeZ; char ext[8]; };	// (pixel: micrometres; sizes: mm)
static const PrintMachine PRINT_MACHINE0 = { "Anycubic Photon Mono 2", 4096, 2560, 35, 143.36, 89.1, 165, "pm3n" };
// A file's format: what writes a job's layers for a family of printers. (One so far; another printer of the same
// family is a line of printers.ini -- print_machines --, another family a writer added here.)
struct PmFile;
struct PrintFormat { const char *id, *name; std::string (*write) (const PmFile &); };
// A lift in two stages (slow off the film, then fast): heights (mm), speeds up and back down (mm/s).
struct PrintLift { double h1, up1, down1, h2, up2, down2; };
struct PrintResin
{
	char name[32];
	double layer;				// a layer's height (mm)
	double exposure, off;			// seconds lit; seconds waited before
	double bottomExposure, bottomLayers;	// the first layers, that hold on the plate
	double transition;			// layers over which the exposure comes down to the normal one
	double liftSpeed, retractSpeed;		// (the header's: the second stage's)
	PrintLift bottom, normal;
};
static const PrintResin PRINT_RESIN0 = { "Standard", 0.05, 2.5, 2, 25, 5, 10, 4, 3, { 2, 1, 2, 4, 4, 5 }, { 2, 1, 2, 4, 4, 5 } };

// ---- the file ------------------------------------------------------------------------------------------------------
struct PmLayer { std::string rle; unsigned lit; float exposure, lift, liftSpeed, height; };
struct PmFile
{
	PrintMachine machine;
	float pixel, layer, exposure, off, bottomExposure, bottomLayers, lift, liftSpeed, retractSpeed, volume;
	unsigned antialias; float weight, price; unsigned currency, perLayer, seconds, transition, transitionType, advanced, h84, h88;
	int pw, ph; std::vector<unsigned short> preview;	// (RGB565)
	PrintLift bottomLift, normalLift;
	V3 lo, hi;						// the model's box, from the plate's middle
	std::vector<PmLayer> layers;
	PmFile () : pixel (35), layer (0.05f), exposure (2.5f), off (2), bottomExposure (25), bottomLayers (5), lift (6), liftSpeed (4), retractSpeed (3), volume (0),
		    antialias (1), weight (0), price (0), currency ('$'), perLayer (0), seconds (0), transition (10), transitionType (0), advanced (0), h84 (0), h88 (10),
		    pw (224), ph (168) { machine = PRINT_MACHINE0; bottomLift = normalLift = PRINT_RESIN0.normal; }
};
struct PmOut
{
	std::string s;
	void u32 (unsigned v) { char b[4] = { (char) v, (char) (v >> 8), (char) (v >> 16), (char) (v >> 24) }; s.append (b, 4); }
	void f32 (double v) { float f = (float) v; unsigned u; memcpy (&u, &f, 4); u32 (u); }
	void str (const char *t, size_t n) { size_t l = strlen (t); if (l > n) l = n; s.append (t, l); s.append (n - l, (char) 0); }
	void at (size_t o, unsigned v) { for (int i = 0; i < 4; i++) s[o + i] = (char) (v >> (8 * i)); }
};
static std::string pm_write (const PmFile &f)
{
	PmOut o; size_t n = f.layers.size ();
	o.str ("ANYCUBIC", 12); o.u32 (517); o.u32 (9);
	size_t table = o.s.size (); for (int i = 0; i < 9; i++) o.u32 (0);
	// HEADER
	o.at (table + 0, (unsigned) o.s.size ()); o.str ("HEADER", 12); o.u32 (92);
	o.f32 (f.pixel); o.f32 (f.layer); o.f32 (f.exposure); o.f32 (f.off); o.f32 (f.bottomExposure); o.f32 (f.bottomLayers);
	o.f32 (f.lift); o.f32 (f.liftSpeed); o.f32 (f.retractSpeed); o.f32 (f.volume);
	o.u32 (f.antialias); o.u32 (f.machine.resX); o.u32 (f.machine.resY); o.f32 (f.weight); o.f32 (f.price); o.u32 (f.currency); o.u32 (f.perLayer);
	o.u32 (f.seconds); o.u32 (f.transition); o.u32 (f.transitionType); o.u32 (f.advanced); o.u32 (f.h84); o.u32 (f.h88);
	// PREVIEW (its length counts its own heading)
	o.at (table + 8, (unsigned) o.s.size ()); o.str ("PREVIEW", 12); o.u32 (16 + 12 + (unsigned) f.preview.size () * 2);
	o.u32 (f.pw); o.u32 ('x'); o.u32 (f.ph);
	for (unsigned short p : f.preview) { o.s.push_back ((char) p); o.s.push_back ((char) (p >> 8)); }
	// the greys' table
	o.at (table + 12, (unsigned) o.s.size ()); o.u32 (0); o.u32 (16); o.s.append (16, (char) 0xFF); o.u32 (0);
	// LAYERDEF
	o.at (table + 16, (unsigned) o.s.size ()); o.str ("LAYERDEF", 12); o.u32 (4 + 32 * (unsigned) n); o.u32 ((unsigned) n);
	size_t defs = o.s.size ();
	for (const PmLayer &l : f.layers) { o.u32 (0); o.u32 ((unsigned) l.rle.size ()); o.f32 (l.lift); o.f32 (l.liftSpeed); o.f32 (l.exposure); o.f32 (l.height); o.u32 (l.lit); o.u32 (0); }
	// EXTRA: the lift's two stages, the first layers' then the others'
	o.at (table + 20, (unsigned) o.s.size ()); o.str ("EXTRA", 12); o.u32 (24);
	for (const PrintLift *l : { &f.bottomLift, &f.normalLift }) { o.u32 (2); o.f32 (l->h1); o.f32 (l->up1); o.f32 (l->down1); o.f32 (l->h2); o.f32 (l->up2); o.f32 (l->down2); }
	// MACHINE (its length counts its heading too)
	o.at (table + 24, (unsigned) o.s.size ()); o.str ("MACHINE", 12); o.u32 (156);
	o.str (f.machine.name, 96); o.str ("pw0Img", 16); o.u32 (16); o.u32 (7); o.f32 (f.machine.sizeX); o.f32 (f.machine.sizeY); o.f32 (f.machine.sizeZ); o.u32 (517); o.u32 (0x00634701);
	// SOFTWARE (as the printer's own slicer writes it)
	o.at (table + 4, (unsigned) o.s.size ()); o.str ("ANYCUBIC-PC", 32); o.u32 (164); o.str ("3.3.2", 32); o.str ("win-x64", 64); o.str ("3.3-CoreProfile", 32);
	// MODEL: its box
	o.at (table + 32, (unsigned) o.s.size ()); o.str ("MODEL", 12); o.u32 (0);
	o.f32 (f.lo.x); o.f32 (f.lo.y); o.f32 (f.lo.z); o.f32 (f.hi.x); o.f32 (f.hi.y); o.f32 (f.hi.z); o.u32 (0); o.u32 (0);
	// the pictures
	o.at (table + 28, (unsigned) o.s.size ());
	for (size_t i = 0; i < n; i++) { o.at (defs + 32 * i, (unsigned) o.s.size ()); o.s += f.layers[i].rle; }
	return o.s;
}
static const PrintFormat PRINT_FORMATS[] = { { "anycubic", "Anycubic Photon (ANYCUBIC 5.17, pw0Img)", pm_write } };
// The printers: the one the format was read from, then those of `ini` (printers.ini beside the app), a line each:
//   name = columns rows pixel(micrometres) width depth height(mm) ending
static std::vector<PrintMachine> print_machines (const char *ini)
{
	std::vector<PrintMachine> out (1, PRINT_MACHINE0);
	for (const char *p = ini; p && *p; )
	{
		const char *e = strchr (p, '\n'); size_t n = e ? (size_t) (e - p) : strlen (p);
		char line[200]; if (n >= sizeof line) n = sizeof line - 1; memcpy (line, p, n); line[n] = 0; p = e ? e + 1 : 0;
		char *eq = strchr (line, '='); if (!eq || line[0] == '#' || line[0] == ';') continue;
		*eq = 0; char *nm = line; while (*nm == ' ') nm++; size_t l = strlen (nm); while (l && (nm[l - 1] == ' ' || nm[l - 1] == '\t')) nm[--l] = 0;
		PrintMachine m = PRINT_MACHINE0; char ext[16] = "";
		if (!l || sscanf (eq + 1, "%d %d %lf %lf %lf %lf %7s", &m.resX, &m.resY, &m.pixel, &m.sizeX, &m.sizeY, &m.sizeZ, ext) < 7) continue;
		if (m.resX < 16 || m.resY < 16 || m.resX > 16384 || m.resY > 16384 || m.pixel < 1) continue;
		snprintf (m.name, sizeof m.name, "%s", nm); snprintf (m.ext, sizeof m.ext, "%s", ext); out.push_back (m);
	}
	return out;
}
// A file read back (the test's, and to look into one): false when it is not of this kind.
static bool pm_read (const std::string &d, PmFile &f)
{
	auto u32 = [&] (size_t o) { unsigned v = 0; if (o + 4 <= d.size ()) memcpy (&v, d.data () + o, 4); return v; };
	auto f32 = [&] (size_t o) { float v = 0; if (o + 4 <= d.size ()) memcpy (&v, d.data () + o, 4); return v; };
	if (d.size () < 200 || memcmp (d.data (), "ANYCUBIC", 8) || u32 (12) != 517 || u32 (16) != 9) return false;
	size_t h = u32 (20) + 16, pv = u32 (28), ld = u32 (36), ex = u32 (40), mc = u32 (44), md = u32 (52);
	if (ld + 20 > d.size () || mc + 156 > d.size () || md + 48 > d.size ()) return false;
	f.pixel = f32 (h); f.layer = f32 (h + 4); f.exposure = f32 (h + 8); f.off = f32 (h + 12); f.bottomExposure = f32 (h + 16); f.bottomLayers = f32 (h + 20);
	f.lift = f32 (h + 24); f.liftSpeed = f32 (h + 28); f.retractSpeed = f32 (h + 32); f.volume = f32 (h + 36);
	f.antialias = u32 (h + 40); f.machine.resX = u32 (h + 44); f.machine.resY = u32 (h + 48); f.weight = f32 (h + 52); f.price = f32 (h + 56); f.currency = u32 (h + 60);
	f.perLayer = u32 (h + 64); f.seconds = u32 (h + 68); f.transition = u32 (h + 72); f.transitionType = u32 (h + 76); f.advanced = u32 (h + 80); f.h84 = u32 (h + 84); f.h88 = u32 (h + 88);
	f.pw = u32 (pv + 16); f.ph = u32 (pv + 24); f.preview.clear ();
	if (f.pw < 0 || f.ph < 0 || f.pw > 2000 || f.ph > 2000 || pv + 28 + (size_t) f.pw * f.ph * 2 > d.size ()) return false;
	for (int i = 0; i < f.pw * f.ph; i++) f.preview.push_back ((unsigned short) ((unsigned char) d[pv + 28 + 2 * i] | (unsigned char) d[pv + 29 + 2 * i] << 8));
	PrintLift *lf[2] = { &f.bottomLift, &f.normalLift };
	for (int i = 0; i < 2; i++) { size_t e = ex + 16 + 28 * i + 4; lf[i]->h1 = f32 (e); lf[i]->up1 = f32 (e + 4); lf[i]->down1 = f32 (e + 8); lf[i]->h2 = f32 (e + 12); lf[i]->up2 = f32 (e + 16); lf[i]->down2 = f32 (e + 20); }
	snprintf (f.machine.name, sizeof f.machine.name, "%s", d.c_str () + mc + 16);
	f.machine.sizeX = f32 (mc + 136); f.machine.sizeY = f32 (mc + 140); f.machine.sizeZ = f32 (mc + 144); f.machine.pixel = f.pixel;
	f.lo = V3 (f32 (md + 16), f32 (md + 20), f32 (md + 24)); f.hi = V3 (f32 (md + 28), f32 (md + 32), f32 (md + 36));
	unsigned n = u32 (ld + 16); f.layers.clear ();
	if (n > 100000 || ld + 20 + 32 * (size_t) n > d.size ()) return false;
	for (unsigned i = 0; i < n; i++)
	{
		size_t e = ld + 20 + 32 * (size_t) i; PmLayer l; size_t a = u32 (e), len = u32 (e + 4);
		if (a + len > d.size ()) return false;
		l.rle = d.substr (a, len); l.lift = f32 (e + 8); l.liftSpeed = f32 (e + 12); l.exposure = f32 (e + 16); l.height = f32 (e + 20); l.lit = u32 (e + 24);
		f.layers.push_back (l);
	}
	return true;
}

// ---- a layer's picture ---------------------------------------------------------------------------------------------
// The runs of a picture, as they come: a colour (lit or not) and how many pixels, the rows one after the other.
struct PmRuns
{
	std::string out; bool lit; unsigned long long run; unsigned count;
	PmRuns () : lit (false), run (0), count (0) {}
	void flush () { unsigned c = lit ? 0xF000 : 0; while (run) { unsigned r = run > 0xFFF ? 0xFFF : (unsigned) run; out.push_back ((char) ((c | r) >> 8)); out.push_back ((char) r); run -= r; } }
	void add (bool on, unsigned long long n) { if (!n) return; if (on != lit) { flush (); lit = on; } run += n; if (on) count += (unsigned) n; }
};
// A picture back from its runs (the test's; the view's, layer by layer): one byte a pixel, 0 or 1.
static bool pm_decode (const std::string &rle, size_t pixels, std::vector<unsigned char> &px)
{
	px.assign (pixels, 0); size_t at = 0;
	for (size_t i = 0; i + 1 < rle.size (); i += 2)
	{
		unsigned v = (unsigned char) rle[i] << 8 | (unsigned char) rle[i + 1], r = v & 0xFFF;
		if (at + r > pixels) return false;
		if (v >> 12) memset (&px[at], 1, r);
		at += r;
	}
	return at == pixels;
}
// The section `polys` (mm, from the plate's middle) as the screen's picture.
static void pm_raster (const Polygons &polys, const PrintMachine &m, bool mirror, PmLayer &l)
{
	double px = m.pixel / 1000; int W = m.resX, H = m.resY;
	struct Edge { double x0, y0, x1, y1; };
	std::vector<Edge> edges; double ymin = 1e30, ymax = -1e30;
	for (const SimplePolygon &p : polys)
		for (size_t i = 0, j = p.size () - 1; i < p.size (); j = i++)
		{
			if (p[i].y == p[j].y) continue;
			Edge e = { p[j].x, p[j].y, p[i].x, p[i].y }; if (mirror) { e.x0 = -e.x0; e.x1 = -e.x1; }
			edges.push_back (e); ymin = std::min (ymin, std::min (e.y0, e.y1)); ymax = std::max (ymax, std::max (e.y0, e.y1));
		}
	PmRuns r;
	// (row k's middle is at y = (H / 2 - k - 0.5) px: the rows that can hold something)
	int k0 = edges.empty () ? H : (int) floor (H / 2.0 - ymax / px - 0.5), k1 = edges.empty () ? H : (int) ceil (H / 2.0 - ymin / px - 0.5);
	if (k0 < 0) k0 = 0; if (k1 > H - 1) k1 = H - 1;
	r.add (false, (unsigned long long) k0 * W);
	std::vector<double> xs;
	for (int k = k0; k <= k1; k++)
	{
		double y = (H / 2.0 - k - 0.5) * px; xs.clear ();
		for (const Edge &e : edges)
			if ((e.y0 <= y) != (e.y1 <= y)) xs.push_back (e.x0 + (e.x1 - e.x0) * (y - e.y0) / (e.y1 - e.y0));
		std::sort (xs.begin (), xs.end ());
		int at = 0;
		for (size_t i = 0; i + 1 < xs.size (); i += 2)		// (a pixel is lit when its middle is inside)
		{
			int a = (int) ceil (xs[i] / px + W / 2.0 - 0.5), b = (int) ceil (xs[i + 1] / px + W / 2.0 - 0.5);
			if (a < at) a = at; if (b > W) b = W;
			if (b <= a) continue;
			r.add (false, a - at); r.add (true, b - a); at = b;
		}
		r.add (false, W - at);
	}
	if (k1 < H - 1 || edges.empty ()) r.add (false, (unsigned long long) (H - 1 - (edges.empty () ? -1 : k1)) * W);
	r.flush (); l.rle.swap (r.out); l.lit = r.count;
}

// ---- a body, printed ---------------------------------------------------------------------------------------------------
// The resins the printer's maker gives values for (a layer of 0.05 mm; the first layers' as the standard one's).
static const PrintResin PRINT_RESINS[3] = {
	{ "Standard", 0.05, 2.5, 2, 25, 5, 10, 4, 3, { 2, 1, 2, 4, 4, 5 }, { 2, 1, 2, 4, 4, 5 } },
	{ "ABS-like", 0.05, 2.5, 2, 25, 5, 10, 4, 3, { 2, 1, 2, 4, 4, 5 }, { 2, 1, 2, 4, 4, 5 } },
	{ "Plant-based", 0.05, 3, 2, 25, 5, 10, 4, 3, { 2, 1, 2, 4, 4, 5 }, { 2, 1, 2, 4, 4, 5 } } };
struct PrintSetup
{
	bool on; int body;
	PrintMachine machine; PrintResin resin;
	bool mirror;				// the picture flipped left to right
	double x, y, lift;			// where the body's middle is on the plate, from the plate's middle; how high above it
	double tiltX, tiltY, turn;		// turned about X, then Y, then Z (degrees) before it is put there
	// what holds it: a pillar under each tip, a raft under them all
	double supAngle, supEvery; bool supLow;	// found where a face leans more than that from upright, every so many mm; under low points
	double supDia, supTip, supInto;		// a pillar's and its tip's diameters; how far the tip goes into the body
	bool raft; double raftThick, raftMore;
	std::vector<V3> tips;			// the tips, on the body as it is on the plate
	PrintSetup () : on (false), body (-1), machine (PRINT_MACHINE0), resin (PRINT_RESIN0), mirror (false), x (0), y (0), lift (0), tiltX (0), tiltY (0), turn (0),
			supAngle (45), supEvery (6), supLow (true), supDia (1.4), supTip (0.45), supInto (0.2), raft (true), raftThick (0.8), raftMore (3) {}
};
// The body as it is on the plate (the plate's middle is the origin, its top z = 0).
static Manifold print_place (const Manifold &body, const PrintSetup &s)
{
	Manifold r = body.Rotate (s.tiltX, s.tiltY, s.turn); auto b = r.BoundingBox ();
	return r.Translate ({s.x - (b.min.x + b.max.x) / 2, s.y - (b.min.y + b.max.y) / 2, s.lift - b.min.z});
}
// Under (x, y): the lowest point of the mesh there. false: nothing above.
struct PrintMesh
{
	std::vector<V3> v; std::vector<int> t;
	void of (const Manifold &m)
	{
		auto g = m.GetMeshGL (); v.clear (); t.clear ();
		for (size_t i = 0; i + 2 < g.vertProperties.size (); i += g.numProp) v.push_back (V3 (g.vertProperties[i], g.vertProperties[i + 1], g.vertProperties[i + 2]));
		for (unsigned k : g.triVerts) t.push_back ((int) k);
	}
	bool under (double x, double y, double *z, double *nz) const
	{
		bool got = false; double best = 1e30;
		for (size_t k = 0; k + 2 < t.size (); k += 3)
		{
			const V3 &a = v[t[k]], &b = v[t[k + 1]], &c = v[t[k + 2]];
			if ((a.x < x && b.x < x && c.x < x) || (a.x > x && b.x > x && c.x > x) || (a.y < y && b.y < y && c.y < y) || (a.y > y && b.y > y && c.y > y)) continue;
			double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y); if (fabs (den) < 1e-12) continue;
			double w0 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / den, w1 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / den, w2 = 1 - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			double zz = w0 * a.z + w1 * b.z + w2 * c.z;
			if (zz < best) { best = zz; got = true; V3 n = unit (cross (b - a, c - a)); *nz = n.z; }
		}
		*z = best; return got;
	}
};
// Where the body needs holding: under what leans more than the angle from upright, on a grid; under its low points.
static void print_supports_auto (const Manifold &placed, const PrintSetup &s, std::vector<V3> &tips)
{
	tips.clear (); if (placed.IsEmpty ()) return;
	PrintMesh m; m.of (placed); auto b = placed.BoundingBox ();
	double every = s.supEvery < 1 ? 1 : s.supEvery, lim = sin (s.supAngle * PI / 180);
	int nx = (int) floor ((b.max.x - b.min.x) / every) + 1, ny = (int) floor ((b.max.y - b.min.y) / every) + 1;
	double ox = (b.min.x + b.max.x) / 2 - (nx - 1) * every / 2, oy = (b.min.y + b.max.y) / 2 - (ny - 1) * every / 2;
	for (int j = 0; j < ny && tips.size () < 4000; j++) for (int i = 0; i < nx; i++)
	{
		double x = ox + i * every, y = oy + j * every, z, nz;
		if (m.under (x, y, &z, &nz) && z > 0.3 && -nz > lim) tips.push_back (V3 (x, y, z));
	}
	if (!s.supLow) return;
	// a point no higher than those around it: something starts there -- and along an edge that lies level between
	// two such points (the low edge of a tilted block: a strip of it would start in mid-air between two pillars)
	std::vector<double> low (m.v.size (), 1e30);
	for (size_t k = 0; k + 2 < m.t.size (); k += 3)
		for (int e = 0; e < 3; e++) { int a = m.t[k + e], c = m.t[k + (e + 1) % 3]; low[a] = std::min (low[a], m.v[c].z); low[c] = std::min (low[c], m.v[a].z); }
	auto add = [&] (const V3 &q)
	{
		if (tips.size () >= 4000) return;
		for (const V3 &t : tips) if (hypot (t.x - q.x, t.y - q.y) < every / 3 && fabs (t.z - q.z) < 1.5) return;
		tips.push_back (q);
	};
	auto is_low = [&] (int i) { return m.v[i].z > 0.3 && m.v[i].z <= low[i] + 1e-6; };
	for (size_t i = 0; i < m.v.size (); i++) if (is_low ((int) i)) add (m.v[i]);
	for (size_t k = 0; k + 2 < m.t.size (); k += 3)
		for (int e = 0; e < 3; e++)
		{
			int a = m.t[k + e], c = m.t[k + (e + 1) % 3]; if (a > c || !is_low (a) || !is_low (c)) continue;
			double l = len (m.v[c] - m.v[a]); int n = (int) (l / every);
			for (int q = 1; q <= n; q++) add (m.v[a] + (m.v[c] - m.v[a]) * ((double) q / (n + 1)));
		}
}
// The pillars and the raft, one solid. (Each: a column from the raft up to a little under its tip, then a cone to
// the tip, which goes a hair into the body.)
static Manifold print_support_solid (const Manifold &placed, const PrintSetup &s)
{
	std::vector<Manifold> parts; if (s.tips.empty ()) return Manifold ();
	double base = s.raft ? s.raftThick : 0, r = s.supDia / 2, cone = std::max (1.5, s.supDia * 1.3), tr = std::min (s.supTip / 2, r);
	for (const V3 &t : s.tips)
	{
		double top = t.z + s.supInto, neck = t.z - cone;
		if (top - base < 0.05) continue;
		if (neck > base + 0.2)
		{
			parts.push_back (Manifold::Cylinder (neck - base, r, r, 10).Translate ({t.x, t.y, base}));
			parts.push_back (Manifold::Cylinder (top - neck, r, tr, 10).Translate ({t.x, t.y, neck}));
		}
		else parts.push_back (Manifold::Cylinder (top - base, r, tr, 10).Translate ({t.x, t.y, base}));
	}
	if (s.raft && s.raftThick > 0.01)
	{
		CrossSection foot = CrossSection (placed.Project (), CrossSection::FillRule::NonZero).Hull ().Offset (s.raftMore, CrossSection::JoinType::Round, 2.0, 32);
		parts.push_back (Manifold::Extrude (foot.ToPolygons (), s.raftThick));
	}
	return Manifold::BatchBoolean (parts, manifold::OpType::Add);
}

struct PrintJob
{
	PmFile file; double minutes, volume; bool tooLarge; char err[72];
	bool onPlate; int islands; double islandAt;	// its first layer is there; parts that start in mid-air, the first one's height
	int done, total;
	PrintJob () : minutes (0), volume (0), tooLarge (false), onPlate (true), islands (0), islandAt (0), done (0), total (0) { err[0] = 0; }
};
// The seconds a layer takes: waited, lit, lifted and brought back.
static double print_layer_time (const PrintResin &r, bool bottom, double exposure)
{
	const PrintLift &l = bottom ? r.bottom : r.normal;
	auto t = [] (double h, double v) { return v > 1e-6 ? h / v : 0; };
	return r.off + exposure + t (l.h1, l.up1) + t (l.h2, l.up2) + t (l.h2, l.down2) + t (l.h1, l.down1) + 3;	// (and the firmware's own pauses)
}
// A layer's exposure: the first ones long, then coming down to the normal one over the transition's layers.
static double print_exposure (const PrintResin &r, int i)
{
	int nb = (int) (r.bottomLayers + 0.5), nt = (int) (r.transition + 0.5);
	if (i < nb) return r.bottomExposure;
	if (i - nb < nt) return r.bottomExposure + (r.exposure - r.bottomExposure) * (i - nb + 1) / (nt + 1);
	return r.exposure;
}
// The small picture the printer shows: the solid seen from a corner, shaded.
static void print_preview (const Manifold &solid, PmFile &f)
{
	int W = f.pw, H = f.ph; f.preview.assign ((size_t) W * H, 0x6240); if (solid.IsEmpty () || W < 8 || H < 8) return;
	PrintMesh m; m.of (solid); std::vector<float> zb ((size_t) W * H, -1e30f);
	double az = -58 * PI / 180, el = 30 * PI / 180;
	V3 d (cos (el) * cos (az), cos (el) * sin (az), sin (el)), rt (-sin (az), cos (az), 0), up = cross (d, rt);
	double lx = 1e30, hx = -1e30, ly = 1e30, hy = -1e30; std::vector<V3> q (m.v.size ());
	for (size_t i = 0; i < m.v.size (); i++) { q[i] = V3 (dot (m.v[i], rt), dot (m.v[i], up), dot (m.v[i], d)); lx = std::min (lx, q[i].x); hx = std::max (hx, q[i].x); ly = std::min (ly, q[i].y); hy = std::max (hy, q[i].y); }
	double k = std::min ((W - 16) / std::max (hx - lx, 1e-6), (H - 16) / std::max (hy - ly, 1e-6));
	for (V3 &p : q) { p.x = W / 2.0 + (p.x - (lx + hx) / 2) * k; p.y = H / 2.0 - (p.y - (ly + hy) / 2) * k; }
	V3 light = unit (V3 (0.35, -0.5, 0.8));
	for (size_t t = 0; t + 2 < m.t.size (); t += 3)
	{
		const V3 &a = q[m.t[t]], &b = q[m.t[t + 1]], &c = q[m.t[t + 2]];
		V3 n = unit (cross (m.v[m.t[t + 1]] - m.v[m.t[t]], m.v[m.t[t + 2]] - m.v[m.t[t]])); if (dot (n, d) <= 0) continue;
		double sh = 0.45 + 0.55 * std::max (0.0, dot (n, light)); unsigned R = (unsigned) (206 * sh), G = (unsigned) (196 * sh), B = (unsigned) (70 * sh);
		unsigned short col = (unsigned short) ((R >> 3) << 11 | (G >> 2) << 5 | B >> 3);
		int x0 = (int) floor (std::min (a.x, std::min (b.x, c.x))), x1 = (int) ceil (std::max (a.x, std::max (b.x, c.x))), y0 = (int) floor (std::min (a.y, std::min (b.y, c.y))), y1 = (int) ceil (std::max (a.y, std::max (b.y, c.y)));
		double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y); if (fabs (den) < 1e-12) continue;
		for (int y = std::max (y0, 0); y <= y1 && y < H; y++) for (int x = std::max (x0, 0); x <= x1 && x < W; x++)
		{
			double px = x + 0.5, py = y + 0.5, w0 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den, w1 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den, w2 = 1 - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			float z = (float) (w0 * a.z + w1 * b.z + w2 * c.z); size_t at = (size_t) y * W + x;
			if (z > zb[at]) { zb[at] = z; f.preview[at] = col; }
		}
	}
}
// What is printed -- `solid`, already on the plate: the body, its supports -- cut into its layers, a few at a time
// (begin, then step until it says done: the window stays alive meanwhile).
struct PrintSlicer
{
	Manifold solid; PrintSetup s; PrintJob *job; int next, n; double secs; Polygons prev;
	PrintSlicer () : job (0), next (0), n (0), secs (0) {}
	bool running () const { return job && next < n; }
	bool begin (const Manifold &on_plate, const PrintSetup &setup, PrintJob &j)
	{
		solid = on_plate; s = setup; job = &j; next = n = 0; secs = 0; prev.clear ();
		const PrintResin &r = s.resin; const PrintMachine &m = s.machine; PmFile &f = j.file;
		j = PrintJob (); f.machine = m;
		if (solid.IsEmpty () || r.layer < 0.005) { snprintf (j.err, sizeof j.err, "There is nothing to print"); job = 0; return false; }
		auto box = solid.BoundingBox (); double hw = m.resX * m.pixel / 2000, hh = m.resY * m.pixel / 2000;
		j.tooLarge = box.min.x < -hw - 1e-6 || box.max.x > hw + 1e-6 || box.min.y < -hh - 1e-6 || box.max.y > hh + 1e-6 || box.max.z > m.sizeZ + 1e-6;
		j.onPlate = box.min.z < r.layer / 2;
		n = (int) ceil (box.max.z / r.layer - 1e-6); if (n < 1) n = 1;
		f.pixel = (float) m.pixel; f.layer = (float) r.layer; f.exposure = (float) r.exposure; f.off = (float) r.off; f.bottomExposure = (float) r.bottomExposure;
		f.bottomLayers = (float) r.bottomLayers; f.lift = (float) (r.normal.h1 + r.normal.h2); f.liftSpeed = (float) r.liftSpeed; f.retractSpeed = (float) r.retractSpeed;
		f.transition = (unsigned) (r.transition + 0.5); f.bottomLift = r.bottom; f.normalLift = r.normal;
		j.volume = solid.Volume () / 1000; f.volume = (float) j.volume; f.weight = (float) (j.volume * 1.1);
		f.lo = V3 (box.min.x, box.min.y, 0); f.hi = V3 (box.max.x, box.max.y, box.max.z);
		f.layers.resize (n); j.total = n; print_preview (solid, f);
		return true;
	}
	static bool inside (const Polygons &ps, double x, double y)
	{
		bool in = false;
		for (const SimplePolygon &l : ps)
			for (size_t i = 0, k = l.size () - 1; i < l.size (); k = i++)
				if ((l[i].y > y) != (l[k].y > y) && x < (l[k].x - l[i].x) * (y - l[i].y) / (l[k].y - l[i].y) + l[i].x) in = !in;
		return in;
	}
	// `count` more layers. true: all are made.
	bool step (int count)
	{
		if (!job) return true;
		const PrintResin &r = s.resin; PmFile &f = job->file;
		for (; count > 0 && next < n; count--, next++)
		{
			int i = next; PmLayer &l = f.layers[i]; bool bottom = i < (int) (r.bottomLayers + 0.5);
			Polygons sec = solid.Slice ((i + 0.5) * r.layer);
			pm_raster (sec, s.machine, s.mirror, l);
			l.exposure = (float) print_exposure (r, i); l.height = (float) r.layer;
			l.lift = (float) ((bottom ? r.bottom : r.normal).h1 + (bottom ? r.bottom : r.normal).h2); l.liftSpeed = (float) r.liftSpeed;
			secs += print_layer_time (r, bottom, l.exposure);
			// a piece of this layer that touches nothing of the one before starts in mid-air
			if (i > 0)
				for (const SimplePolygon &loop : sec)
				{
					double a = 0; for (size_t q = 0, k = loop.size () - 1; q < loop.size (); k = q++) a += loop[k].x * loop[q].y - loop[q].x * loop[k].y;
					if (a <= 0 || loop.empty ()) continue;				// (a hole)
					bool held = false; Polygons one (1, loop);
					for (size_t q = 0; q < loop.size () && !held; q++) held = inside (prev, loop[q].x, loop[q].y);
					for (size_t pi = 0; pi < prev.size () && !held; pi++) for (size_t q = 0; q < prev[pi].size () && !held; q++) held = inside (one, prev[pi][q].x, prev[pi][q].y);
					if (!held) { if (!job->islands) job->islandAt = i * r.layer; job->islands++; }
				}
			prev.swap (sec);
		}
		job->done = next;
		if (next < n) return false;
		f.seconds = (unsigned) (secs + 0.5); job->minutes = secs / 60; return true;
	}
};
template <class F> static bool print_slice (const Manifold &on_plate, const PrintSetup &s, PrintJob &job, F progress)
{
	PrintSlicer sl; if (!sl.begin (on_plate, s, job)) return false;
	while (sl.running ()) { if (!progress (sl.next, sl.n)) return false; sl.step (1); }
	return true;
}

// ---- kept: in the part's file, and the resin from a part to the next -----------------------------------------------------
static std::string print_resin_line (const PrintResin &r)
{
	char b[400];
	snprintf (b, sizeof b, "printresin %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g  %.9g %.9g %.9g %.9g %.9g %.9g  %.9g %.9g %.9g %.9g %.9g %.9g  %s\n", r.layer, r.exposure, r.off, r.bottomExposure,
		  r.bottomLayers, r.transition, r.liftSpeed, r.retractSpeed, r.bottom.h1, r.bottom.up1, r.bottom.down1, r.bottom.h2, r.bottom.up2, r.bottom.down2,
		  r.normal.h1, r.normal.up1, r.normal.down1, r.normal.h2, r.normal.up2, r.normal.down2, r.name);
	return b;
}
static std::string print_save (const PrintSetup &s)
{
	if (!s.on) return std::string ();
	char b[300]; std::string out;
	snprintf (b, sizeof b, "print body %d at %.9g %.9g %.9g tilt %.9g %.9g %.9g mirror %d\n", s.body, s.x, s.y, s.lift, s.tiltX, s.tiltY, s.turn, s.mirror ? 1 : 0); out += b;
	snprintf (b, sizeof b, "printmachine %d %d %.9g %.9g %.9g %.9g %s %s\n", s.machine.resX, s.machine.resY, s.machine.pixel, s.machine.sizeX, s.machine.sizeY, s.machine.sizeZ, s.machine.ext, s.machine.name); out += b;
	snprintf (b, sizeof b, "printsup %.9g %.9g %d %.9g %.9g %.9g raft %d %.9g %.9g\n", s.supAngle, s.supEvery, s.supLow ? 1 : 0, s.supDia, s.supTip, s.supInto, s.raft ? 1 : 0, s.raftThick, s.raftMore); out += b;
	out += print_resin_line (s.resin);
	for (const V3 &t : s.tips) { snprintf (b, sizeof b, "printtip %.9g %.9g %.9g\n", t.x, t.y, t.z); out += b; }
	return out;
}
static void print_load (PrintSetup &s, const char *text)
{
	const char *p = text;
	while (p && *p)
	{
		const char *e = strchr (p, '\n'); size_t n = e ? (size_t) (e - p) : strlen (p);
		char line[500]; if (n >= sizeof line) n = sizeof line - 1; memcpy (line, p, n); line[n] = 0; if (n && line[n - 1] == '\r') line[n - 1] = 0;
		p = e ? e + 1 : 0; int a = 0, c = 0, off = 0;
		if (!strncmp (line, "print body ", 11))
		{ if (sscanf (line, "print body %d at %lf %lf %lf tilt %lf %lf %lf mirror %d", &s.body, &s.x, &s.y, &s.lift, &s.tiltX, &s.tiltY, &s.turn, &a) >= 7) { s.on = true; s.mirror = a != 0; s.tips.clear (); } }
		else if (!strncmp (line, "printmachine ", 13))
		{
			PrintMachine m = s.machine; char ext[16] = "";
			if (sscanf (line, "printmachine %d %d %lf %lf %lf %lf %7s %n", &m.resX, &m.resY, &m.pixel, &m.sizeX, &m.sizeY, &m.sizeZ, ext, &off) >= 7 && off && m.resX >= 16 && m.resY >= 16 && m.pixel >= 1)
			{ snprintf (m.name, sizeof m.name, "%s", line + off); snprintf (m.ext, sizeof m.ext, "%s", ext); s.machine = m; }
		}
		else if (!strncmp (line, "printsup ", 9))
		{ if (sscanf (line, "printsup %lf %lf %d %lf %lf %lf raft %d %lf %lf", &s.supAngle, &s.supEvery, &a, &s.supDia, &s.supTip, &s.supInto, &c, &s.raftThick, &s.raftMore) >= 9) { s.supLow = a != 0; s.raft = c != 0; } }
		else if (!strncmp (line, "printresin ", 11))
		{
			PrintResin r = s.resin;
			if (sscanf (line, "printresin %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %n", &r.layer, &r.exposure, &r.off, &r.bottomExposure, &r.bottomLayers, &r.transition,
				    &r.liftSpeed, &r.retractSpeed, &r.bottom.h1, &r.bottom.up1, &r.bottom.down1, &r.bottom.h2, &r.bottom.up2, &r.bottom.down2,
				    &r.normal.h1, &r.normal.up1, &r.normal.down1, &r.normal.h2, &r.normal.up2, &r.normal.down2, &off) >= 20)
			{ snprintf (r.name, sizeof r.name, "%s", off ? line + off : "Resin"); s.resin = r; }
		}
		else if (!strncmp (line, "printtip ", 9)) { V3 t; if (sscanf (line + 9, "%lf %lf %lf", &t.x, &t.y, &t.z) == 3 && s.tips.size () < 4000) s.tips.push_back (t); }
	}
}

} // namespace forge

#endif
