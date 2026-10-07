//
// terrain.cpp -- Critters' pixel terrain (terrain.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "terrain.h"
#include <string.h>


namespace critters {

uint64_t mix64 (uint64_t z)
{
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

uint32_t mix32 (int x, int y, int k)
{
	uint32_t h = (uint32_t) x * 0x9E3779B1u ^ (uint32_t) y * 0x85EBCA77u ^ (uint32_t) k * 0xC2B2AE3Du;
	h ^= h >> 15; h *= 0x2C1B3C6Du;
	h ^= h >> 12; h *= 0x297A2D39u;
	return h ^ (h >> 15);
}

static uint64_t pix (int i, int mat) { return mix64 ((uint64_t) i * 8 + (uint64_t) mat); }

bool Terrain::alloc (int nw, int nh)
{
	free ();
	m = new uint8_t[(size_t) nw * nh];
	col = new uint32_t[(size_t) nw * nh];
	if (!m || !col) { free (); return false; }
	w = nw; h = nh;
	memset (m, M_EMPTY, (size_t) w * h);
	for (int i = 0; i < w * h; i++) col[i] = bg;
	hash = full_hash ();
	dirty.x0 = 0; dirty.y0 = 0; dirty.x1 = w - 1; dirty.y1 = h - 1;
	return true;
}

void Terrain::free ()
{
	delete[] m; delete[] col;
	m = 0; col = 0; w = h = 0; hash = 0;
	dirty.x0 = dirty.y0 = 0; dirty.x1 = dirty.y1 = -1;
}

void Terrain::touch (int x0, int y0, int x1, int y1)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w - 1) x1 = w - 1;
	if (y1 > h - 1) y1 = h - 1;
	if (x1 < x0 || y1 < y0) return;
	if (dirty.x1 < dirty.x0) { dirty.x0 = x0; dirty.y0 = y0; dirty.x1 = x1; dirty.y1 = y1; return; }
	if (x0 < dirty.x0) dirty.x0 = x0;
	if (y0 < dirty.y0) dirty.y0 = y0;
	if (x1 > dirty.x1) dirty.x1 = x1;
	if (y1 > dirty.y1) dirty.y1 = y1;
}

void Terrain::set (int x, int y, uint8_t mat, uint32_t colour)
{
	if (x < 0 || x >= w || y < 0 || y >= h) return;
	int i = y * w + x;
	if (m[i] != mat) { hash ^= pix (i, m[i]) ^ pix (i, mat); m[i] = mat; }
	col[i] = colour;
	touch (x, y, x, y);
}

int Terrain::dig_rect (int x0, int y0, int x1, int y1)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w - 1) x1 = w - 1;
	if (y1 > h - 1) y1 = h - 1;
	int n = 0;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
			if (m[y * w + x] == M_EARTH) { set (x, y, M_EMPTY, bg); n++; }
	return n;
}

bool Terrain::any_steel (int x0, int y0, int x1, int y1) const
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w - 1) x1 = w - 1;
	if (y1 > h - 1) y1 = h - 1;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
			if (m[y * w + x] == M_STEEL) return true;
	return false;
}

int Terrain::dig_disc (int cx, int cy, int r)
{
	int n = 0;
	for (int y = cy - r; y <= cy + r; y++)
	{
		if (y < 0 || y >= h) continue;
		for (int x = cx - r; x <= cx + r; x++)
		{
			if (x < 0 || x >= w) continue;
			int dx = x - cx, dy = y - cy;
			if (dx * dx + dy * dy <= r * r && m[y * w + x] == M_EARTH) { set (x, y, M_EMPTY, bg); n++; }
		}
	}
	return n;
}

static uint32_t lighter (uint32_t c)
{
	uint32_t r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
	r += (255 - r) / 3; g += (255 - g) / 3; b += (255 - b) / 3;
	return r << 16 | g << 8 | b;
}

int Terrain::brick (int x, int y, int dir)
{
	int n = 0;
	for (int k = -1; k <= 4; k++)
		for (int row = y - 1; row <= y; row++)
		{
			int px = x + k * dir;
			if (px < 0 || px >= w || row < 0 || row >= h || m[row * w + px] != M_EMPTY) continue;
			set (px, row, M_EARTH, row == y - 1 ? lighter (brickColour) : brickColour);
			n++;
		}
	return n;
}

Rect Terrain::take_dirty ()
{
	Rect r = dirty;
	dirty.x0 = dirty.y0 = 0; dirty.x1 = dirty.y1 = -1;
	return r;
}

uint64_t Terrain::full_hash () const
{
	uint64_t s = 0;
	for (int i = 0; i < w * h; i++) s ^= pix (i, m[i]);
	return s;
}

}
