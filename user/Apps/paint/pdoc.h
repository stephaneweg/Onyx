//
// pdoc.h -- Paint's picture: layers of 0xAARRGGBB pixels (straight alpha: A = 255 opaque, 0 clear),
// bottom first, each with a name, shown or hidden, an opacity; their composite (kept, recomputed
// where something changed: a floating selection and a shape being drawn are composed with their
// layer); the edits undone and redone -- a stroke keeps the 64 x 64 tiles it touched as they were,
// a change of the picture's size or of its layers keeps the whole picture.
//
#ifndef _paint_pdoc_h
#define _paint_pdoc_h

namespace pd {

template <class T> static inline T pmin (T a, T b) { return a < b ? a : b; }
template <class T> static inline T pmax (T a, T b) { return a > b ? a : b; }
template <class T> static inline T pclamp (T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline void scpy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }

// c over d (straight alpha, c's alpha scaled by a 0..255).
static inline unsigned over (unsigned d, unsigned c, unsigned a)
{
	unsigned ca = ((c >> 24) * a + 127) / 255;
	if (ca == 0) return d;
	unsigned da = d >> 24;
	if (ca == 255 || da == 0) return (c & 0xFFFFFF) | ca << 24;
	unsigned oa = ca + (da * (255 - ca) + 127) / 255;		// (the result's alpha)
	unsigned dw = da * (255 - ca) / 255;			// (the part of d that shows)
	unsigned r = (((c >> 16) & 255) * ca + ((d >> 16) & 255) * dw) / oa;
	unsigned g = (((c >> 8) & 255) * ca + ((d >> 8) & 255) * dw) / oa;
	unsigned b = ((c & 255) * ca + (d & 255) * dw) / oa;
	return oa << 24 | r << 16 | g << 8 | b;
}

struct Layer
{
	char name[32];
	unsigned *px;			// w * h
	bool visible;
	int opacity;			// 0..255
};

// A rectangle [x0, x1) x [y0, y1).
struct Rect
{
	int x0, y0, x1, y1;
	bool empty () const { return x1 <= x0 || y1 <= y0; }
	void add (int x, int y) { if (empty ()) { x0 = x; y0 = y; x1 = x + 1; y1 = y + 1; return; } x0 = pmin (x0, x); y0 = pmin (y0, y); x1 = pmax (x1, x + 1); y1 = pmax (y1, y + 1); }
	void add (const Rect &r) { if (r.empty ()) return; if (empty ()) { *this = r; return; } x0 = pmin (x0, r.x0); y0 = pmin (y0, r.y0); x1 = pmax (x1, r.x1); y1 = pmax (y1, r.y1); }
	void clip (int w, int h) { x0 = pmax (x0, 0); y0 = pmax (y0, 0); x1 = pmin (x1, w); y1 = pmin (y1, h); }
};
static inline Rect mkrect (int x0, int y0, int x1, int y1) { Rect r; r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1; return r; }
static inline Rect norect () { return mkrect (0, 0, 0, 0); }

// A block of pixels floating over a layer (a selection lifted, a paste): drawn in the composite just
// above the current layer, until it is put down.
struct Floater
{
	unsigned *px; int w, h;		// 0: none
	int x, y;			// its place in the picture
};

// A preview drawn over the current layer (a shape being dragged, a stroke's): a layer-sized
// buffer, transparent but where it was drawn (rect).
struct Overlay
{
	unsigned *px; Rect r; bool eraser;	// (eraser: its drawn pixels clear the layer instead)
};

enum { TILE = 64, MAXLAYERS = 32 };

struct Doc
{
	int w, h;
	Layer lay[MAXLAYERS]; int n;	// bottom first
	int cur;			// the layer drawn on
	unsigned *comp;			// the composite (w * h)
	Floater fl;
	Overlay ov;
	unsigned changes;		// counts the edits (the saved state: its count)
};
static Doc D;

// ---- layers -------------------------------------------------------------------------------------------
static unsigned *new_px (int w, int h, unsigned fill)
{
	unsigned *p = new unsigned[(unsigned) w * h];
	for (int i = 0; i < w * h; i++) p[i] = fill;
	return p;
}
static void layer_init (Layer &l, const char *name, unsigned *px)
{
	scpy (l.name, name, sizeof l.name); l.px = px; l.visible = true; l.opacity = 255;
}
// A new picture: w x h, its background layer white (or clear).
static void doc_new (int w, int h, bool white)
{
	for (int i = 0; i < D.n; i++) delete[] D.lay[i].px;
	delete[] D.comp; delete[] D.fl.px; delete[] D.ov.px;
	D.w = w; D.h = h; D.n = 1; D.cur = 0;
	layer_init (D.lay[0], "Background", new_px (w, h, white ? 0xFFFFFFFFu : 0));
	D.comp = new_px (w, h, 0);
	D.fl.px = 0; D.fl.w = D.fl.h = 0; D.fl.x = D.fl.y = 0;
	D.ov.px = new_px (w, h, 0); D.ov.r = norect (); D.ov.eraser = false;
	D.changes = 0;
}

// ---- the composite ------------------------------------------------------------------------------------------
// Recompute r: the visible layers bottom first, the floater and the overlay with the current one.
static void compose (Rect r)
{
	r.clip (D.w, D.h);
	if (r.empty ()) return;
	for (int y = r.y0; y < r.y1; y++)
	{
		unsigned *o = D.comp + (unsigned) y * D.w;
		for (int x = r.x0; x < r.x1; x++) o[x] = 0;
		for (int k = 0; k < D.n; k++)
		{
			const Layer &l = D.lay[k];
			if (!l.visible && k != D.cur) continue;
			const unsigned *s = l.px + (unsigned) y * D.w;
			bool extra = k == D.cur && (D.fl.px || !D.ov.r.empty ());
			if (!extra)
			{
				if (!l.visible) continue;
				if (l.opacity == 255) { for (int x = r.x0; x < r.x1; x++) { unsigned c = s[x]; if ((c >> 24) == 255) o[x] = c; else if (c >> 24) o[x] = over (o[x], c, 255); } }
				else for (int x = r.x0; x < r.x1; x++) if (s[x] >> 24) o[x] = over (o[x], s[x], (unsigned) l.opacity);
				continue;
			}
			// the current layer with what floats over it: made first, then laid on the others
			const unsigned *ov = D.ov.px + (unsigned) y * D.w;
			bool ovRow = y >= D.ov.r.y0 && y < D.ov.r.y1;
			bool flRow = D.fl.px && y >= D.fl.y && y < D.fl.y + D.fl.h;
			for (int x = r.x0; x < r.x1; x++)
			{
				unsigned c = l.visible ? s[x] : 0;
				if (ovRow && x >= D.ov.r.x0 && x < D.ov.r.x1 && (ov[x] >> 24))
					c = D.ov.eraser ? 0 : over (c, ov[x], 255);
				if (flRow && x >= D.fl.x && x < D.fl.x + D.fl.w)
				{
					unsigned f = D.fl.px[(unsigned) (y - D.fl.y) * D.fl.w + (x - D.fl.x)];
					if (f >> 24) c = over (c, f, 255);
				}
				if (c >> 24) o[x] = over (o[x], c, (unsigned) (l.visible ? l.opacity : 255));
			}
		}
	}
}
static void compose_all () { compose (mkrect (0, 0, D.w, D.h)); }

// ---- undo ---------------------------------------------------------------------------------------------------
// A record: the tiles of one layer as they were (TILES), or the whole picture as it was (WHOLE).
enum { U_TILES, U_WHOLE };
struct Tile { int tx, ty; unsigned *px; };
struct Snapshot { int w, h, n, cur; Layer lay[MAXLAYERS]; };
struct Undo
{
	int kind, layer;
	Tile *t; int nt, ct;
	Snapshot *snap;
	unsigned bytes;
};
enum { MAXUNDO = 60 };
static const unsigned UNDO_BYTES = 192u << 20;
static Undo g_undo[MAXUNDO]; static int g_nundo, g_uptr;	// [0, uptr) done, [uptr, nundo) undone
static Undo g_rec; static bool g_recOpen;			// the record being made
static unsigned char *g_seen; static int g_seenW, g_seenH;	// its tiles already kept

static void snap_free (Snapshot *s) { if (!s) return; for (int i = 0; i < s->n; i++) delete[] s->lay[i].px; delete s; }
static void undo_free (Undo &u)
{
	for (int i = 0; i < u.nt; i++) delete[] u.t[i].px;
	delete[] u.t; u.t = 0; u.nt = u.ct = 0;
	snap_free (u.snap); u.snap = 0;
}
static void undo_clear ()
{
	for (int i = 0; i < g_nundo; i++) undo_free (g_undo[i]);
	g_nundo = g_uptr = 0;
	if (g_recOpen) { undo_free (g_rec); g_recOpen = false; }
}

static void push_undo (Undo &u)
{
	for (int i = g_uptr; i < g_nundo; i++) undo_free (g_undo[i]);	// (the redo list is gone)
	g_nundo = g_uptr;
	unsigned total = u.bytes;
	for (int i = 0; i < g_nundo; i++) total += g_undo[i].bytes;
	while (g_nundo > 0 && (g_nundo >= MAXUNDO || total > UNDO_BYTES))	// (the oldest forgotten)
	{
		total -= g_undo[0].bytes;
		undo_free (g_undo[0]);
		for (int i = 1; i < g_nundo; i++) g_undo[i - 1] = g_undo[i];
		g_nundo--;
	}
	g_undo[g_nundo++] = u;
	g_uptr = g_nundo;
	D.changes++;
}

// A stroke begins on the current layer: its tiles will be kept as they are before they change.
static void rec_begin ()
{
	if (g_recOpen) return;
	g_rec.kind = U_TILES; g_rec.layer = D.cur; g_rec.t = 0; g_rec.nt = g_rec.ct = 0; g_rec.snap = 0; g_rec.bytes = 0;
	int tw = (D.w + TILE - 1) / TILE, th = (D.h + TILE - 1) / TILE;
	if (!g_seen || g_seenW != tw || g_seenH != th) { delete[] g_seen; g_seen = new unsigned char[tw * th]; g_seenW = tw; g_seenH = th; }
	for (int i = 0; i < tw * th; i++) g_seen[i] = 0;
	g_recOpen = true;
}
// r of the current layer is about to change.
static void rec_touch (Rect r)
{
	if (!g_recOpen) rec_begin ();
	r.clip (D.w, D.h);
	if (r.empty ()) return;
	const unsigned *px = D.lay[g_rec.layer].px;
	for (int ty = r.y0 / TILE; ty <= (r.y1 - 1) / TILE; ty++)
		for (int tx = r.x0 / TILE; tx <= (r.x1 - 1) / TILE; tx++)
		{
			unsigned char &s = g_seen[ty * g_seenW + tx];
			if (s) continue;
			s = 1;
			if (g_rec.nt == g_rec.ct)
			{
				int c = g_rec.ct ? g_rec.ct * 2 : 16;
				Tile *t = new Tile[c];
				for (int i = 0; i < g_rec.nt; i++) t[i] = g_rec.t[i];
				delete[] g_rec.t; g_rec.t = t; g_rec.ct = c;
			}
			Tile &t = g_rec.t[g_rec.nt++];
			t.tx = tx; t.ty = ty; t.px = new unsigned[TILE * TILE];
			for (int y = 0; y < TILE; y++)
				for (int x = 0; x < TILE; x++)
				{
					int X = tx * TILE + x, Y = ty * TILE + y;
					t.px[y * TILE + x] = X < D.w && Y < D.h ? px[(unsigned) Y * D.w + X] : 0;
				}
			g_rec.bytes += TILE * TILE * 4;
		}
}
static void rec_end ()
{
	if (!g_recOpen) return;
	g_recOpen = false;
	if (g_rec.nt == 0) { undo_free (g_rec); return; }
	push_undo (g_rec);
}

// The whole picture kept (before a change of its size, of its layers).
static Snapshot *snapshot ()
{
	Snapshot *s = new Snapshot;
	s->w = D.w; s->h = D.h; s->n = D.n; s->cur = D.cur;
	for (int i = 0; i < D.n; i++)
	{
		s->lay[i] = D.lay[i];
		s->lay[i].px = new unsigned[(unsigned) D.w * D.h];
		for (int k = 0; k < D.w * D.h; k++) s->lay[i].px[k] = D.lay[i].px[k];
	}
	return s;
}
static void rec_whole ()
{
	Undo u; u.kind = U_WHOLE; u.layer = 0; u.t = 0; u.nt = u.ct = 0;
	u.snap = snapshot (); u.bytes = (unsigned) (D.w * D.h * 4 * D.n);
	push_undo (u);
}

// Swap a record with the picture as it is (undo, then redo, the same record).
static void swap_record (Undo &u)
{
	if (u.kind == U_TILES)
	{
		Layer &l = D.lay[pclamp (u.layer, 0, D.n - 1)];
		for (int i = 0; i < u.nt; i++)
		{
			Tile &t = u.t[i];
			for (int y = 0; y < TILE; y++)
				for (int x = 0; x < TILE; x++)
				{
					int X = t.tx * TILE + x, Y = t.ty * TILE + y;
					if (X >= D.w || Y >= D.h) continue;
					unsigned &p = l.px[(unsigned) Y * D.w + X], v = t.px[y * TILE + x];
					t.px[y * TILE + x] = p; p = v;
				}
		}
		D.cur = pclamp (u.layer, 0, D.n - 1);
		return;
	}
	Snapshot *now = new Snapshot;
	now->w = D.w; now->h = D.h; now->n = D.n; now->cur = D.cur;
	for (int i = 0; i < D.n; i++) now->lay[i] = D.lay[i];
	Snapshot *s = u.snap;
	D.w = s->w; D.h = s->h; D.n = s->n; D.cur = s->cur;
	for (int i = 0; i < s->n; i++) D.lay[i] = s->lay[i];
	delete s;
	u.snap = now;
	delete[] D.comp; D.comp = new_px (D.w, D.h, 0);
	delete[] D.ov.px; D.ov.px = new_px (D.w, D.h, 0); D.ov.r = norect ();
}
static bool undo ()
{
	if (g_uptr == 0) return false;
	swap_record (g_undo[--g_uptr]);
	D.changes++;
	compose_all ();
	return true;
}
static bool redo ()
{
	if (g_uptr >= g_nundo) return false;
	swap_record (g_undo[g_uptr++]);
	D.changes++;
	compose_all ();
	return true;
}

} // namespace pd

#endif
