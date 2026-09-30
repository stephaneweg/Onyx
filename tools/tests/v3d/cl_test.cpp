//
// cl_test -- the bytes of the V3D control-list packets kern/v3d_cl.h makes, against the field
// positions of Mesa's src/broadcom/cle/v3d_packet.xml (V3D 4.1+: "Load Tile Buffer General",
// "Store Tile Buffer General"): each field set alone to all ones must land on its bits and no
// other. The packed bit-fields of v3d_cl.h get this wrong silently when a group of them does
// not fill its type (the load's stride sat 3 bits low: the GPU read it divided by 8 -- the
// frames drawn over the target's pixels were wrong on the Pi). sh tools/tests/run_v3d_cl_test.sh
//
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <kern/v3d_cl.h>

static int s_fail = 0, s_tests = 0;

// a packet's bits after its opcode, as a 96-bit number (bit 0 = the byte after the opcode's bit 0)
template <class P> static void bits_of (const P &p, unsigned char out[12])
{
	unsigned char b[sizeof p];
	memcpy (b, &p, sizeof p);
	memset (out, 0, 12);
	for (unsigned i = 1; i < sizeof p && i <= 12; i++) out[i - 1] = b[i];
}

static bool bit (const unsigned char *b, int i) { return (b[i / 8] >> (i % 8)) & 1; }

// the packet p has exactly the bits [start, start + size) set (and its opcode right)
template <class P> static void expect (const char *what, const P &p, unsigned char op, int start, int size)
{
	unsigned char b[12];
	bits_of (p, b);
	unsigned char code; memcpy (&code, &p, 1);
	int wrong = -1;
	for (int i = 0; i < (int) (sizeof p - 1) * 8; i++)
		if (bit (b, i) != (i >= start && i < start + size)) { wrong = i; break; }
	s_tests++;
	if (code != op || wrong >= 0)
	{
		s_fail++;
		printf ("FAIL  %s: expected bits %d..%d (opcode %u), bit %d wrong (opcode %u)\n", what, start, start + size - 1, op, wrong, code);
	}
	else
		printf ("ok    %s: bits %d..%d\n", what, start, start + size - 1);
}

// a field's value read back from the packet's bytes at Mesa's position
template <class P> static unsigned long long field (const P &p, int start, int size)
{
	unsigned char b[12];
	bits_of (p, b);
	unsigned long long v = 0;
	for (int i = 0; i < size; i++) if (bit (b, start + i)) v |= 1ull << i;
	return v;
}

int main ()
{
	// ---- Load Tile Buffer General (opcode 30): Mesa's fields
	//   Buffer to Load 0/4, Memory Format 4/3, Flip Y 7/1, Decimate mode 10/2, Input Image Format 12/6,
	//   Force Alpha 1 18/1, Channel Reverse 19/1, R/B swap 20/1, Height in UB or Stride 28/20, Height 48/16,
	//   Address 64/32
	expect ("load: buffer", LoadTileBufferGeneral (15, 0, false, 0, 0, false, false, false, 0, 0, 0), 30, 0, 4);
	expect ("load: memory format", LoadTileBufferGeneral (0, 7, false, 0, 0, false, false, false, 0, 0, 0), 30, 4, 3);
	expect ("load: flip y", LoadTileBufferGeneral (0, 0, true, 0, 0, false, false, false, 0, 0, 0), 30, 7, 1);
	expect ("load: decimate", LoadTileBufferGeneral (0, 0, false, 3, 0, false, false, false, 0, 0, 0), 30, 10, 2);
	expect ("load: image format", LoadTileBufferGeneral (0, 0, false, 0, 63, false, false, false, 0, 0, 0), 30, 12, 6);
	expect ("load: force alpha 1", LoadTileBufferGeneral (0, 0, false, 0, 0, true, false, false, 0, 0, 0), 30, 18, 1);
	expect ("load: channel reverse", LoadTileBufferGeneral (0, 0, false, 0, 0, false, true, false, 0, 0, 0), 30, 19, 1);
	expect ("load: r/b swap", LoadTileBufferGeneral (0, 0, false, 0, 0, false, false, true, 0, 0, 0), 30, 20, 1);
	expect ("load: stride", LoadTileBufferGeneral (0, 0, false, 0, 0, false, false, false, 0xFFFFF, 0, 0), 30, 28, 20);
	expect ("load: height", LoadTileBufferGeneral (0, 0, false, 0, 0, false, false, false, 0, 0xFFFF, 0), 30, 48, 16);
	expect ("load: address", LoadTileBufferGeneral (0, 0, false, 0, 0, false, false, false, 0, 0, 0xFFFFFFFFu), 30, 64, 32);

	// ---- Store Tile Buffer General (opcode 29)
	//   Buffer to Store 0/4, Memory Format 4/3, Flip Y 7/1, Dither Mode 8/2, Decimate mode 10/2,
	//   Output Image Format 12/6, Clear buffer being stored 18/1, Channel Reverse 19/1, R/B swap 20/1,
	//   Height in UB or Stride 28/20, Height 48/16, Address 64/32
	expect ("store: buffer", StoreTileBufferGeneral (15, 0, false, 0, 0, 0, false, false, false, 0, 0, 0), 29, 0, 4);
	expect ("store: memory format", StoreTileBufferGeneral (0, 7, false, 0, 0, 0, false, false, false, 0, 0, 0), 29, 4, 3);
	expect ("store: flip y", StoreTileBufferGeneral (0, 0, true, 0, 0, 0, false, false, false, 0, 0, 0), 29, 7, 1);
	expect ("store: dither", StoreTileBufferGeneral (0, 0, false, 3, 0, 0, false, false, false, 0, 0, 0), 29, 8, 2);
	expect ("store: decimate", StoreTileBufferGeneral (0, 0, false, 0, 3, 0, false, false, false, 0, 0, 0), 29, 10, 2);
	expect ("store: image format", StoreTileBufferGeneral (0, 0, false, 0, 0, 63, false, false, false, 0, 0, 0), 29, 12, 6);
	expect ("store: clear", StoreTileBufferGeneral (0, 0, false, 0, 0, 0, true, false, false, 0, 0, 0), 29, 18, 1);
	expect ("store: channel reverse", StoreTileBufferGeneral (0, 0, false, 0, 0, 0, false, true, false, 0, 0, 0), 29, 19, 1);
	expect ("store: r/b swap", StoreTileBufferGeneral (0, 0, false, 0, 0, 0, false, false, true, 0, 0, 0), 29, 20, 1);
	expect ("store: stride", StoreTileBufferGeneral (0, 0, false, 0, 0, 0, false, false, false, 0xFFFFF, 0, 0), 29, 28, 20);
	expect ("store: height", StoreTileBufferGeneral (0, 0, false, 0, 0, 0, false, false, false, 0, 0xFFFF, 0), 29, 48, 16);
	expect ("store: address", StoreTileBufferGeneral (0, 0, false, 0, 0, 0, false, false, false, 0, 0, 0xFFFFFFFFu), 29, 64, 32);

	// ---- the kernel's own target packets (sys/v3d.cpp BuildRCL), a canvas of 1920 px at 0x1234000
	{
		LoadTileBufferGeneral L = V3dLoadTarget (true, false, 1920 * 4, 0x1234000);
		StoreTileBufferGeneral S = V3dStoreTarget (true, 1920 * 4, 0x1234000);
		struct { const char *what; unsigned long long got, want; } c[] = {
			{ "the target's load: stride", field (L, 28, 20), 1920 * 4 },
			{ "the target's load: format RGBA8", field (L, 12, 6), 27 },
			{ "the target's load: raster", field (L, 4, 3), 0 },
			{ "the target's load: r/b swap (a canvas)", field (L, 20, 1), 1 },
			{ "the target's load: address", field (L, 64, 32), 0x1234000 },
			{ "the target's store: stride", field (S, 28, 20), 1920 * 4 },
			{ "the target's store: format RGBA8", field (S, 12, 6), 27 },
			{ "the target's store: r/b swap (a canvas)", field (S, 20, 1), 1 },
			{ "the target's store: address", field (S, 64, 32), 0x1234000 },
		};
		for (auto &k : c)
		{
			s_tests++;
			if (k.got != k.want) { s_fail++; printf ("FAIL  %s: %llu, not %llu\n", k.what, k.got, k.want); }
			else printf ("ok    %s\n", k.what);
		}
	}
	printf (s_fail ? "FAILED: %d/%d\n" : "ALL PASS: %d/%d\n", s_tests - s_fail, s_tests);
	return s_fail != 0;
}
