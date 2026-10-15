//
// mesh.h -- the models: a Wavefront .obj (+ its .mtl) read into flat-shaded coloured triangles, lit once with the
// game's toon light, and the black outline around them (the "inverted hull": the faces pushed out along the
// normals, turned inside out). Plain C++ (stdio), the Pi and the PC tests.
//
// What is read: v x y z [r g b] (a colour 0..1 by vertex wins over the material's), vt, f (v, v/t, v//n, v/t/n,
// negative indices, polygons fanned), usemtl / mtllib (newmtl, Kd, Ke > 0: not lit -- lights, flames --, d < 1:
// see-through), o / g (each triangle remembers its group: the game moves some -- a propeller, a flame), and the
// game's own comment "#@ name x y z": a named point (where the wings and the engines are fixed on a hull).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_MESH_H
#define CZ_MESH_H

#include "math3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cz {

// A growable array of plain structures (no STL in the apps).
template <typename T> struct Arr
{
	T *p = 0; int n = 0, cap = 0;
	Arr () {}
	Arr (const Arr &) = delete;
	Arr &operator= (const Arr &) = delete;
	~Arr () { free (p); }
	void clear () { n = 0; }
	void reserve (int c) { if (c > cap) { int nc = cap ? cap : 16; while (nc < c) nc *= 2; T *q = (T *) realloc (p, sizeof (T) * (size_t) nc); if (!q) return; p = q; cap = nc; } }
	T *push () { if (n >= cap) reserve (n + 1); if (n >= cap) return 0; memset (&p[n], 0, sizeof (T)); return &p[n++]; }
	void push (const T &v) { T *q = push (); if (q) *q = v; }
	T &operator[] (int i) { return p[i]; }
	const T &operator[] (int i) const { return p[i]; }
	void release () { free (p); p = 0; n = cap = 0; }
};

enum { TRI_UNLIT = 1, TRI_ALPHA = 2, TRI_OUTLINE = 4, TRI_GLOW = 8 };

struct Tri
{
	V3 p[3];
	float s[3], t[3];
	unsigned col[3];		// 0xRRGGBB, lit (after light ())
	unsigned base;			// the material's colour, unlit
	unsigned char alpha;		// 255 opaque
	unsigned char flags;		// TRI_*
	short group;			// the "o" / "g" it is in (Mesh::groups), -1 none
	unsigned char paint;		// its material: 0 its own colour, 1 "paint1" / 2 "paint2" (the player's colours), 3 "glow"
};

struct Mesh
{
	enum { MAXG = 24, MAXPT = 24 };
	Arr<Tri> tris;
	char groups[MAXG][24]; int ngroups = 0;
	char ptName[MAXPT][24]; V3 pt[MAXPT]; int npt = 0;
	V3 lo, hi;			// the bounding box

	int group (const char *name) const { for (int i = 0; i < ngroups; i++) if (!strcmp (groups[i], name)) return i; return -1; }
	bool point (const char *name, V3 *out) const
	{
		for (int i = 0; i < npt; i++) if (!strcmp (ptName[i], name)) { *out = pt[i]; return true; }
		return false;
	}
	void bounds ()
	{
		lo = V3 (1e9f, 1e9f, 1e9f); hi = V3 (-1e9f, -1e9f, -1e9f);
		for (int i = 0; i < tris.n; i++) for (int k = 0; k < 3; k++)
		{
			const V3 &p = tris[i].p[k];
			if (p.x < lo.x) lo.x = p.x; if (p.y < lo.y) lo.y = p.y; if (p.z < lo.z) lo.z = p.z;
			if (p.x > hi.x) hi.x = p.x; if (p.y > hi.y) hi.y = p.y; if (p.z > hi.z) hi.z = p.z;
		}
		if (tris.n == 0) lo = hi = V3 ();
	}
};

// ---- reading ----------------------------------------------------------------------------------------------------
// The whole file into memory (0: none). The caller frees it. stdio, or the game's own reader (on Onyx: AppKit's
// kapi_open / kapi_read, which know the card's volumes everywhere -- the PC's tests too).
typedef char *(*FileReader) (const char *path, long *size);
static FileReader g_fileReader = 0;
static char *readFile (const char *path, long *size = 0)
{
	if (g_fileReader) return g_fileReader (path, size);
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	if (n < 0) { fclose (f); return 0; }
	char *b = (char *) malloc ((size_t) n + 1);
	if (!b) { fclose (f); return 0; }
	long got = (long) fread (b, 1, (size_t) n, f);
	fclose (f);
	b[got > 0 ? got : 0] = 0;
	if (size) *size = got;
	return b;
}

static inline unsigned col01 (float r, float g, float b)
{
	int R = (int) (clampf (r, 0, 1) * 255 + 0.5f), G = (int) (clampf (g, 0, 1) * 255 + 0.5f), B = (int) (clampf (b, 0, 1) * 255 + 0.5f);
	return (unsigned) (R << 16 | G << 8 | B);
}

struct ObjMaterial { char name[32]; unsigned kd; bool unlit, glow; unsigned char alpha, paint; };

static int objMaterials (const char *path, ObjMaterial *mats, int max)
{
	char *b = readFile (path);
	if (!b) return 0;
	int n = -1;
	for (char *line = b; line && *line; )
	{
		char *e = strchr (line, '\n'); if (e) *e = 0;
		while (*line == ' ' || *line == '\t') line++;
		if (!strncmp (line, "newmtl ", 7) && n + 1 < max)
		{
			n++; memset (&mats[n], 0, sizeof mats[n]); mats[n].kd = 0xC0C0C0; mats[n].alpha = 255;
			char *nm = line + 7; while (*nm == ' ') nm++;
			int k = 0; while (nm[k] && nm[k] != '\r' && nm[k] != ' ' && k < 31) { mats[n].name[k] = nm[k]; k++; }
			mats[n].name[k] = 0;
			mats[n].paint = !strncmp (mats[n].name, "paint2", 6) ? 2 : !strncmp (mats[n].name, "paint", 5) ? 1 : !strncmp (mats[n].name, "glow", 4) ? 3 : 0;
			if (mats[n].paint == 3) { mats[n].unlit = true; mats[n].glow = true; mats[n].paint = 0; }
			int nl = (int) strlen (mats[n].name);
			if (nl > 5 && !strcmp (mats[n].name + nl - 5, "_glow")) { mats[n].unlit = true; mats[n].glow = true; }
			if (!strcmp (mats[n].name, "glass")) mats[n].alpha = 90;
		}
		else if (n >= 0 && !strncmp (line, "Kd ", 3)) { float r = 0, g = 0, bl = 0; sscanf (line + 3, "%f %f %f", &r, &g, &bl); mats[n].kd = col01 (r, g, bl); }
		else if (n >= 0 && !strncmp (line, "Ke ", 3)) { float r = 0, g = 0, bl = 0; sscanf (line + 3, "%f %f %f", &r, &g, &bl); if (r + g + bl > 0.01f) mats[n].unlit = true; }
		else if (n >= 0 && !strncmp (line, "d ", 2)) { float d = 1; sscanf (line + 2, "%f", &d); mats[n].alpha = (unsigned char) (clampf (d, 0, 1) * 255); }
		line = e ? e + 1 : 0;
	}
	free (b);
	return n + 1;
}

// Reads path (.obj) into m (added to what it has). false: no such file / nothing in it.
static bool loadObj (const char *path, Mesh &m)
{
	char *b = readFile (path);
	if (!b) return false;
	Arr<V3> vs; Arr<unsigned> vc; Arr<float> vt;	// positions, their colours (0xFF000000: none), uv pairs
	enum { MAXM = 96 };
	static ObjMaterial mats[MAXM]; int nmat = 0;
	unsigned cur = 0xC0C0C0; bool unlit = false, glow = false; unsigned char alpha = 255, paint = 0; int grp = -1;
	for (char *line = b; line && *line; )
	{
		char *e = strchr (line, '\n'); if (e) *e = 0;
		while (*line == ' ' || *line == '\t') line++;
		int L = (int) strlen (line); while (L > 0 && (line[L - 1] == '\r' || line[L - 1] == ' ')) line[--L] = 0;
		if (line[0] == 'v' && line[1] == ' ')
		{
			float x = 0, y = 0, z = 0, r = -1, g = -1, bl = -1;
			int k = sscanf (line + 2, "%f %f %f %f %f %f", &x, &y, &z, &r, &g, &bl);
			vs.push (V3 (x, y, z));
			vc.push (k >= 6 ? col01 (r, g, bl) : 0xFF000000u);
		}
		else if (line[0] == 'v' && line[1] == 't' && line[2] == ' ')
		{
			float s = 0, t = 0; sscanf (line + 3, "%f %f", &s, &t);
			vt.push (s); vt.push (1 - t);
		}
		else if (line[0] == 'f' && line[1] == ' ')
		{
			int vi[32], ti[32], nv = 0;
			char *p = line + 2;
			while (*p && nv < 32)
			{
				while (*p == ' ') p++;
				if (!*p) break;
				int a = (int) strtol (p, &p, 10), t = 0;
				if (*p == '/') { p++; if (*p != '/') t = (int) strtol (p, &p, 10); if (*p == '/') { p++; strtol (p, &p, 10); } }
				while (*p && *p != ' ') p++;
				vi[nv] = a < 0 ? vs.n + a : a - 1;
				ti[nv] = t < 0 ? vt.n / 2 + t : t - 1;
				nv++;
			}
			for (int k = 1; k + 1 < nv; k++)
			{
				int idx[3] = { vi[0], vi[k], vi[k + 1] }, tdx[3] = { ti[0], ti[k], ti[k + 1] };
				bool ok = true;
				for (int j = 0; j < 3; j++) if (idx[j] < 0 || idx[j] >= vs.n) ok = false;
				if (!ok) continue;
				Tri *T = m.tris.push ();
				if (!T) break;
				for (int j = 0; j < 3; j++)
				{
					T->p[j] = vs[idx[j]];
					bool hasT = tdx[j] >= 0 && tdx[j] * 2 + 1 < vt.n;
					T->s[j] = hasT ? vt[tdx[j] * 2] : 0; T->t[j] = hasT ? vt[tdx[j] * 2 + 1] : 0;
					T->col[j] = vc[idx[j]] != 0xFF000000u ? vc[idx[j]] : cur;
				}
				T->base = T->col[0]; T->alpha = alpha; T->flags = (unsigned char) ((unlit ? TRI_UNLIT : 0) | (glow ? TRI_GLOW : 0) | (alpha < 255 && !glow ? TRI_ALPHA : 0));
				T->group = (short) grp; T->paint = paint;
			}
		}
		else if (!strncmp (line, "mtllib ", 7))
		{
			char mp[256]; const char *slash = strrchr (path, '/');
			int dl = slash ? (int) (slash - path) + 1 : 0;
			if (dl > 200) dl = 200;
			memcpy (mp, path, (size_t) dl);
			snprintf (mp + dl, sizeof mp - (size_t) dl, "%s", line + 7);
			nmat += objMaterials (mp, mats + nmat, MAXM - nmat);
		}
		else if (!strncmp (line, "usemtl ", 7))
		{
			cur = 0xC0C0C0; unlit = false; glow = false; alpha = 255; paint = 0;
			for (int i = 0; i < nmat; i++) if (!strcmp (mats[i].name, line + 7)) { cur = mats[i].kd; unlit = mats[i].unlit; glow = mats[i].glow; alpha = mats[i].alpha; paint = mats[i].paint; }
		}
		else if ((line[0] == 'o' || line[0] == 'g') && line[1] == ' ')
		{
			const char *nm = line + 2;
			grp = m.group (nm);
			if (grp < 0 && m.ngroups < Mesh::MAXG) { grp = m.ngroups++; snprintf (m.groups[grp], sizeof m.groups[grp], "%s", nm); }
		}
		else if (line[0] == '#' && line[1] == '@' && m.npt < Mesh::MAXPT)
		{
			char nm[24] = ""; float x = 0, y = 0, z = 0;
			const char *q = line + 2; while (*q == ' ') q++;
			if (!strncmp (q, "socket ", 7)) q += 7;
			if (sscanf (q, "%23s %f %f %f", nm, &x, &y, &z) == 4)
			{ snprintf (m.ptName[m.npt], sizeof m.ptName[0], "%s", nm); m.pt[m.npt++] = V3 (x, y, z); }
		}
		line = e ? e + 1 : 0;
	}
	free (b);
	m.bounds ();
	return m.tris.n > 0;
}

// ---- building ---------------------------------------------------------------------------------------------------
// Adds src's triangles to dst, through M (positions); recolor: 0 keep, else a function of the base colour.
static void appendMesh (Mesh &dst, const Mesh &src, const M4 &M, unsigned (*recolor) (unsigned, void *) = 0, void *ctx = 0)
{
	int g0 = dst.ngroups;
	for (int i = 0; i < src.ngroups && dst.ngroups < Mesh::MAXG; i++)
	{
		int g = dst.group (src.groups[i]);
		if (g < 0) { g = dst.ngroups++; memcpy (dst.groups[g], src.groups[i], sizeof dst.groups[g]); }
	}
	(void) g0;
	dst.tris.reserve (dst.tris.n + src.tris.n);
	for (int i = 0; i < src.tris.n; i++)
	{
		Tri *T = dst.tris.push (); if (!T) return;
		*T = src.tris[i];
		for (int k = 0; k < 3; k++) T->p[k] = M.point (src.tris[i].p[k]);
		// a mirroring matrix turns the faces inside out: put them back
		V3 c = cross (M.dir (V3 (1, 0, 0)), M.dir (V3 (0, 1, 0)));
		if (dot (c, M.dir (V3 (0, 0, 1))) < 0)
		{
			V3 p = T->p[1]; T->p[1] = T->p[2]; T->p[2] = p;
			float s = T->s[1]; T->s[1] = T->s[2]; T->s[2] = s; s = T->t[1]; T->t[1] = T->t[2]; T->t[2] = s;
			unsigned c1 = T->col[1]; T->col[1] = T->col[2]; T->col[2] = c1;
		}
		if (recolor) { T->base = recolor (T->base, ctx); for (int k = 0; k < 3; k++) T->col[k] = recolor (T->col[k], ctx); }
		T->group = src.tris[i].group >= 0 ? (short) dst.group (src.groups[src.tris[i].group]) : (short) -1;
	}
	dst.bounds ();
}

// The game's light: a sun from up-front-left, a soft ramp (never black), a little sky light from above.
static inline float toonShade (const V3 &n, const V3 &sun)
{
	float d = dot (n, sun);
	float k = 0.64f + 0.36f * smooth01 (d * 1.25f + 0.35f);
	k += 0.07f * clampf (n.y, 0, 1);
	return k;
}
static inline V3 sunDir () { return norm (V3 (-0.45f, 0.8f, 0.4f)); }

// Each corner's normal: the faces around the same point whose normals are within `crease` of its own face's
// (area-weighted) -- a rounded model lit smoothly, its sharp edges kept. nrm: 3 a triangle. A grid of hashed
// positions finds the neighbours.
static void cornerNormals (const Mesh &m, V3 *nrm, float creaseCos)
{
	int n = m.tris.n;
	int nc = n * 3;
	int H = 1; while (H < nc * 2) H <<= 1;
	int *head = (int *) malloc (sizeof (int) * (size_t) H), *next = (int *) malloc (sizeof (int) * (size_t) nc);
	V3 *fn = (V3 *) malloc (sizeof (V3) * (size_t) n);
	if (!head || !next || !fn) { free (head); free (next); free (fn); for (int i = 0; i < nc; i++) nrm[i] = V3 (0, 1, 0); return; }
	for (int i = 0; i < H; i++) head[i] = -1;
	auto key = [&] (const V3 &p) -> unsigned
	{
		int x = (int) floorf (p.x * 1000 + 0.5f), y = (int) floorf (p.y * 1000 + 0.5f), z = (int) floorf (p.z * 1000 + 0.5f);
		return ((unsigned) x * 73856093u ^ (unsigned) y * 19349663u ^ (unsigned) z * 83492791u) & (unsigned) (H - 1);
	};
	for (int i = 0; i < n; i++) fn[i] = cross (m.tris[i].p[1] - m.tris[i].p[0], m.tris[i].p[2] - m.tris[i].p[0]);
	for (int c = 0; c < nc; c++) { unsigned k = key (m.tris[c / 3].p[c % 3]); next[c] = head[k]; head[k] = c; }
	for (int c = 0; c < nc; c++)
	{
		const V3 &p = m.tris[c / 3].p[c % 3];
		V3 own = norm (fn[c / 3]), sum;
		for (int o = head[key (p)]; o >= 0; o = next[o])
		{
			V3 d = m.tris[o / 3].p[o % 3] - p;
			if (fabsf (d.x) + fabsf (d.y) + fabsf (d.z) > 2e-3f) continue;
			if (dot (norm (fn[o / 3]), own) < creaseCos) continue;
			sum += fn[o / 3];
		}
		nrm[c] = len (sum) > 1e-12f ? norm (sum) : own;
	}
	free (head); free (next); free (fn);
}

// Every triangle's colours from its base colour and its corners' normals, the unlit ones as they are.
static void lightMesh (Mesh &m, const V3 &sun = sunDir (), float creaseDeg = 40)
{
	V3 *nrm = (V3 *) malloc (sizeof (V3) * (size_t) (m.tris.n * 3 + 1));
	if (!nrm) return;
	cornerNormals (m, nrm, cosf (creaseDeg * PI / 180));
	for (int i = 0; i < m.tris.n; i++)
	{
		Tri &T = m.tris[i];
		if (T.flags & (TRI_UNLIT | TRI_OUTLINE | TRI_GLOW)) continue;
		for (int j = 0; j < 3; j++) T.col[j] = rgbScale (T.col[j], toonShade (nrm[i * 3 + j], sun));
	}
	free (nrm);
}

// The outline: each lit triangle again, pushed out by `w` along its corner's smoothed normal (the corners at
// the same place share it), its winding turned, in `col` -- drawn with the front faces culled, it shows as a
// rim around the model. Added at the end of m.
static void addOutline (Mesh &m, float w, unsigned col = 0x2B2140)
{
	int n0 = m.tris.n;
	if (n0 == 0) return;
	V3 *nrm = (V3 *) malloc (sizeof (V3) * (size_t) n0 * 3);
	if (!nrm) return;
	cornerNormals (m, nrm, -2.0f);			// (every face around a point: one normal, the shell stays closed)
	m.tris.reserve (n0 * 2);
	for (int i = 0; i < n0; i++)
	{
		if (m.tris[i].flags & (TRI_UNLIT | TRI_ALPHA | TRI_GLOW)) continue;
		Tri *T = m.tris.push (); if (!T) break;
		*T = m.tris[i];
		for (int k = 0; k < 3; k++) T->p[k] = m.tris[i].p[k] + nrm[i * 3 + k] * w;
		V3 p = T->p[1]; T->p[1] = T->p[2]; T->p[2] = p;
		T->col[0] = T->col[1] = T->col[2] = T->base = col;
		T->flags = TRI_OUTLINE;
	}
	free (nrm);
}

} // namespace cz

#endif
