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

struct PrintMachine { char name[32]; int resX, resY; double pixel, sizeX, sizeY, sizeZ; };	// (pixel: micrometres; sizes: mm)
static const PrintMachine PRINT_MACHINE0 = { "Anycubic Photon Mono 2", 4096, 2560, 35, 143.36, 89.1, 165 };
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
struct PrintSetup
{
	bool on; int body;
	PrintMachine machine; PrintResin resin;
	bool mirror;				// the picture flipped left to right
	double x, y;				// where the body's middle is on the plate, from the plate's middle
	PrintSetup () : on (false), body (-1), machine (PRINT_MACHINE0), resin (PRINT_RESIN0), mirror (false), x (0), y (0) {}
};
struct PrintJob { PmFile file; double minutes, volume; bool tooLarge; char err[72]; PrintJob () : minutes (0), volume (0), tooLarge (false) { err[0] = 0; } };
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
// What is printed, `solid` (the body, its supports), cut into its layers. progress (done, of): false stops it.
template <class F> static bool print_slice (const Manifold &solid, const PrintSetup &s, PrintJob &job, F progress)
{
	const PrintResin &r = s.resin; const PrintMachine &m = s.machine; PmFile &f = job.file;
	job.err[0] = 0; f = PmFile (); f.machine = m;
	if (solid.IsEmpty () || r.layer < 0.005) { snprintf (job.err, sizeof job.err, "There is nothing to print"); return false; }
	auto box = solid.BoundingBox ();
	V3 lo (box.min.x, box.min.y, box.min.z), hi (box.max.x, box.max.y, box.max.z), size = hi - lo;
	job.tooLarge = size.x > m.resX * m.pixel / 1000 || size.y > m.resY * m.pixel / 1000 || size.z > m.sizeZ;
	// on the plate: its box's middle at (x, y), its underside on it
	Manifold placed = solid.Translate ({s.x - (lo.x + hi.x) / 2, s.y - (lo.y + hi.y) / 2, -lo.z});
	int n = (int) ceil (size.z / r.layer - 1e-6); if (n < 1) n = 1;
	f.pixel = (float) m.pixel; f.layer = (float) r.layer; f.exposure = (float) r.exposure; f.off = (float) r.off; f.bottomExposure = (float) r.bottomExposure;
	f.bottomLayers = (float) r.bottomLayers; f.lift = (float) (r.normal.h1 + r.normal.h2); f.liftSpeed = (float) r.liftSpeed; f.retractSpeed = (float) r.retractSpeed;
	f.transition = (unsigned) (r.transition + 0.5); f.bottomLift = r.bottom; f.normalLift = r.normal;
	job.volume = solid.Volume () / 1000; f.volume = (float) job.volume; f.weight = (float) (job.volume * 1.1);
	f.lo = V3 (s.x - size.x / 2, s.y - size.y / 2, 0); f.hi = V3 (s.x + size.x / 2, s.y + size.y / 2, size.z);
	double secs = 0; f.layers.resize (n);
	for (int i = 0; i < n; i++)
	{
		if (!progress (i, n)) return false;
		PmLayer &l = f.layers[i]; bool bottom = i < (int) (r.bottomLayers + 0.5);
		pm_raster (placed.Slice ((i + 0.5) * r.layer), m, s.mirror, l);
		l.exposure = (float) print_exposure (r, i); l.height = (float) r.layer;
		l.lift = (float) ((bottom ? r.bottom : r.normal).h1 + (bottom ? r.bottom : r.normal).h2); l.liftSpeed = (float) r.liftSpeed;
		secs += print_layer_time (r, bottom, l.exposure);
	}
	f.seconds = (unsigned) (secs + 0.5); job.minutes = secs / 60;
	f.preview.assign ((size_t) f.pw * f.ph, 0);
	return true;
}

} // namespace forge

#endif
