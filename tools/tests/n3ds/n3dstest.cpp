//
// n3dstest.cpp -- the 3DS core (user/Emulators/n3ds) run without a screen: loads a program, runs it frame after
// frame until it ends, prints what it says (svcOutputDebugString) and how it ended. Built for AArch64 with
// Onyx's toolchain and run under qemu-aarch64 (tools/tests/run_n3ds_test.sh): the JIT is the real one.
//   n3dstest <program.elf> [frames [picture.ppm]]		(600 frames at most by default)
// The exit status is 0 when the program ended by itself without an error of the emulator. The picture is the two
// screens as they are at the end, the bottom one under the top one; its checksum is printed ("screens <crc>").
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

static void debugOut (void *, const char *text, n3ds::u32 len) { fwrite (text, 1, len, stdout); }

static unsigned crc32 (const unsigned char *p, size_t n)
{
	unsigned c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1))); }
	return ~c;
}

// Both screens in one picture of TOP_W x 2 SCREEN_H (the bottom screen centred), 3 bytes a pixel.
static unsigned char *screens (const n3ds::Machine *m)
{
	using namespace n3ds;
	static u32 top[TOP_W * SCREEN_H], bottom[BOTTOM_W * SCREEN_H];
	static unsigned char rgb[TOP_W * SCREEN_H * 2 * 3];
	m->screenImage (SCREEN_TOP, top); m->screenImage (SCREEN_BOTTOM, bottom);
	memset (rgb, 0, sizeof rgb);
	for (int y = 0; y < SCREEN_H; y++) for (int x = 0; x < TOP_W; x++)
	{
		unsigned char *d = rgb + (y * TOP_W + x) * 3; u32 v = top[y * TOP_W + x];
		d[0] = (unsigned char) (v >> 16); d[1] = (unsigned char) (v >> 8); d[2] = (unsigned char) v;
	}
	for (int y = 0; y < SCREEN_H; y++) for (int x = 0; x < BOTTOM_W; x++)
	{
		unsigned char *d = rgb + ((y + SCREEN_H) * TOP_W + x + (TOP_W - BOTTOM_W) / 2) * 3; u32 v = bottom[y * BOTTOM_W + x];
		d[0] = (unsigned char) (v >> 16); d[1] = (unsigned char) (v >> 8); d[2] = (unsigned char) v;
	}
	return rgb;
}

static bool fileRead (void *user, n3ds::u64 offset, void *dst, n3ds::u32 n)
{
	FILE *f = (FILE *) user;
	return fseek (f, (long) offset, SEEK_SET) == 0 && fread (dst, 1, n, f) == n;
}

int main (int argc, char **argv)
{
	if (argc < 2) { fprintf (stderr, "usage: n3dstest <program.elf> [frames [picture.ppm]]\n"); return 2; }
	int frames = argc > 2 ? atoi (argv[2]) : 600;
	FILE *f = fopen (argv[1], "rb");
	if (!f) { fprintf (stderr, "cannot open %s\n", argv[1]); return 2; }
	fseek (f, 0, SEEK_END); long size = ftell (f); fseek (f, 0, SEEK_SET);
	// (the file is read piece by piece: a game is hundreds of MB)
	n3ds::Source src = { f, (n3ds::u64) size, fileRead };

	n3ds::Machine *m = new n3ds::Machine;
	if (!m->init ()) { fprintf (stderr, "not enough memory for the machine\n"); return 2; }
	m->debugOut = debugOut;
	m->trace = getenv ("N3DS_TRACE") != 0;
	// N3DS_FONT=<sysfont.bcfnt>  the shared system font (tools/n3ds/mkfont.py makes ours)
	if (const char *fontPath = getenv ("N3DS_FONT"))
	{
		FILE *ff = fopen (fontPath, "rb");
		if (ff)
		{
			static unsigned char font[0x332000];
			size_t fn = fread (font, 1, sizeof font, ff); fclose (ff);
			if (!m->setSharedFont (font, (n3ds::u32) fn)) fprintf (stderr, "%s is not a font (BCFNT)\n", fontPath);
		}
	}
	if (!m->loadFrom (src)) { fprintf (stderr, "%s\n", m->lastError); return 2; }
	if (m->title[0]) fprintf (stderr, "%s [%s]\n", m->title, m->productCode);
	// N3DS_KEYS=100-110:1;200-210:8  buttons held over frame ranges, a hex mask each (n3ds.h's BTN_*)
	// N3DS_TOUCH=300-305:160,120      the touch screen pressed there over frames
	// N3DS_SHOTS=<prefix> N3DS_SHOTEVERY=<n>  a picture every n frames (default 60)
	struct Range { int f0, f1; unsigned a, b; } keys[32], touch[32]; int nk = 0, nt = 0;
	for (const char *e = getenv ("N3DS_KEYS"); e && *e && nk < 32; )
	{
		Range r; char *end;
		r.f0 = (int) strtol (e, &end, 10); if (*end != '-') break;
		r.f1 = (int) strtol (end + 1, &end, 10); if (*end != ':') break;
		r.a = (unsigned) strtoul (end + 1, &end, 16); r.b = 0;
		keys[nk++] = r; e = *end == ';' ? end + 1 : end; if (*end != ';') break;
	}
	for (const char *e = getenv ("N3DS_TOUCH"); e && *e && nt < 32; )
	{
		Range r; char *end;
		r.f0 = (int) strtol (e, &end, 10); if (*end != '-') break;
		r.f1 = (int) strtol (end + 1, &end, 10); if (*end != ':') break;
		r.a = (unsigned) strtoul (end + 1, &end, 10); if (*end != ',') break;
		r.b = (unsigned) strtoul (end + 1, &end, 10);
		touch[nt++] = r; e = *end == ';' ? end + 1 : end; if (*end != ';') break;
	}
	const char *shots = getenv ("N3DS_SHOTS");
	const int shotEvery = getenv ("N3DS_SHOTEVERY") ? atoi (getenv ("N3DS_SHOTEVERY")) : 60;
	int n = 0;
	while (n < frames && !m->exited)
	{
		unsigned b = 0; bool down = false; int tx = 0, ty = 0;
		for (int k = 0; k < nk; k++) if (n >= keys[k].f0 && n <= keys[k].f1) b |= keys[k].a;
		for (int k = 0; k < nt; k++) if (n >= touch[k].f0 && n <= touch[k].f1) { down = true; tx = (int) touch[k].a; ty = (int) touch[k].b; }
		m->setInput (b, 0, 0, down, tx, ty);
		m->runFrame (); n++;
		if (shots && shotEvery > 0 && n % shotEvery == 0)
		{
			char name[512]; snprintf (name, sizeof name, "%s%05d.ppm", shots, n);
			FILE *o = fopen (name, "wb");
			if (o) { fprintf (o, "P6\n%d %d\n255\n", n3ds::TOP_W, n3ds::SCREEN_H * 2); fwrite (screens (m), 1, (size_t) n3ds::TOP_W * n3ds::SCREEN_H * 2 * 3, o); fclose (o); }
		}
	}
	fflush (stdout);
	fprintf (stderr, "%s after %d frames (%llu ticks): %llu system calls, %llu thread switches, %u memory faults\n",
		 m->exited ? "ended" : "still running", n, (unsigned long long) m->now, (unsigned long long) m->svcCount,
		 (unsigned long long) m->switchCount, (unsigned) m->mem.faults);
	const unsigned char *rgb = screens (m);
	const size_t rgbSize = (size_t) n3ds::TOP_W * n3ds::SCREEN_H * 2 * 3;
	fprintf (stderr, "screens %08x (%llu VBlanks; the GPU was asked %u fills, %u transfers, %u command lists)\n", crc32 (rgb, rgbSize),
		 (unsigned long long) m->gsp.frames, (unsigned) m->gsp.fills, (unsigned) m->gsp.transfers, (unsigned) m->gsp.cmdLists);
	if (argc > 3)
	{
		FILE *o = fopen (argv[3], "wb");
		if (o) { fprintf (o, "P6\n%d %d\n255\n", n3ds::TOP_W, n3ds::SCREEN_H * 2); fwrite (rgb, 1, rgbSize, o); fclose (o); }
	}
	if (m->notes[0]) fprintf (stderr, "not emulated: %s\n", m->notes);
	if (m->mem.faults) fprintf (stderr, "the last memory fault: %08x\n", (unsigned) m->mem.faultAddr);
	if (m->lastError[0]) fprintf (stderr, "error: %s\n", m->lastError);
	return m->exited && !m->lastError[0] && m->exitCode == 0 ? 0 : 1;
}
