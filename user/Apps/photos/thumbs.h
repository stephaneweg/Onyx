//
// Apps/photos/thumbs.h -- the pictures made small, and the one shown made big, by a thread of its own so that the
// window stays live: a thumbnail is the photo turned the right way, brought to 320 pixels on its long side, kept on
// the card as a JPEG (SD:/etc/photos/thumbs/<key>.jpg: the next start shows it at once); each size drawn is made once
// from it and kept (a small cache). The one asked last comes first (what is in sight). The photo shown big is decoded
// whole (turned the right way) before any thumbnail. When nothing is asked, the same thread makes the thumbnails still
// missing for the whole library, one after another (the backlog: set_backlog; its progress in blDone / blTotal).
//
//   g_thumbs.draw (canvas, photoIndex, x, y, w, h, radius, bg);   (a soft placeholder until it is there)
//   g_thumbs.want_full (photoIndex);  ... onFull (index, Pix &)
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_thumbs_h
#define _photos_thumbs_h

#include "Apps/photos/lib.h"
#include "Apps/photos/imgops.h"
#include "img/pngsave.hpp"
#include "wtk/wtk.h"

namespace photos {

using namespace wtk;

#define PH_THUMBS PH_DIR "/thumbs"

// rounded corners: the pixels outside laid on bg
static void round_corners (Canvas &cv, int x, int y, int w, int h, int r, unsigned bg)
{
	if (r <= 0) return;
	for (int j = 0; j < r; j++)
		for (int i = 0; i < r; i++)
		{
			int dx = r - i, dy = r - j; int d2 = dx * dx + dy * dy - r * r;	// > 0: outside
			if (d2 <= -2 * r) continue;
			int a = d2 >= 2 * r ? 255 : (d2 + 2 * r) * 255 / (4 * r);
			int px[4] = { x + i, x + w - 1 - i, x + i, x + w - 1 - i }, py[4] = { y + j, y + j, y + h - 1 - j, y + h - 1 - j };
			for (int k = 0; k < 4; k++)
			{
				if (px[k] < 0 || py[k] < 0 || px[k] >= cv.w || py[k] >= cv.h) continue;
				unsigned &d = cv.px[(long) py[k] * cv.stride + px[k]];
				d = wk_mix (d, bg, a);
			}
		}
}

struct FullDone { int serial; int index; char path[400]; Pix pix; bool failed; };

class Thumbs
{
public:
	enum { BASE = 320, NBASE = 360, NSCALED = 700, QN = 256 };
	Library *lib;
	void (*onLoaded) ();				// a thumbnail came
	void (*onFull) (FullDone *);			// the big one came (the window takes d->pix)
	volatile bool pause;
	Thumbs () : lib (0), onLoaded (0), onFull (0), pause (false), m_tick (0), m_lk (0), m_qn (0), m_tid (-1), m_quit (false), m_ev (-1), m_fullSerial (0), m_fullWant (false), blDone (0), blTotal (0), m_bl (0), m_bln (0), m_blGen (0), m_todo (0), m_todoN (0), m_todoAt (0), m_seenGen (0)
	{ memset (m_base, 0, sizeof m_base); memset (m_sc, 0, sizeof m_sc); }

	void start () { m_ev = kapi_event_create (0, 0); m_tid = kapi_thread_create (thread_main, this, 512 * 1024, "photos-thumbs"); }
	void quit () { m_quit = true; if (m_ev > 0) kapi_event_set (m_ev); if (m_tid > 0) kapi_thread_join (m_tid, 3000, 0); }

	// the photo (cut to w x h's shape) at (x, y), its corners rounded over bg
	void draw (Canvas &cv, int pi, int x, int y, int w, int h, int radius, unsigned bg, bool fitInside = false)
	{
		if (!lib || pi < 0 || pi >= lib->ph.n || w <= 0 || h <= 0) return;
		const Photo &p = lib->ph[pi];
		const unsigned *px = scaled (p.key (), pi, w, h, fitInside);
		if (px) blit (cv, px, x, y, w, h);
		else
		{	// (coming) a soft tone
			unsigned c0 = wk_mix (bg, 0x808080, 60), c1 = wk_mix (bg, 0x808080, 90);
			for (int j = 0; j < h; j++) cv.fillRect (x, y + j, w, 1, wk_mix (c0, c1, j * 256 / h));
		}
		if (radius > 0) round_corners (cv, x, y, w, h, radius, bg);
	}
	// the base (BASE on the long side) when it is there, else asked for
	const Pix *base (int pi)
	{
		if (!lib || pi < 0 || pi >= lib->ph.n) return 0;
		unsigned key = lib->ph[pi].key ();
		Base *b = base_of (key);
		if (!b) { b = base_slot (); if (!b) return 0; b->key = key; b->pending = true; b->use = ++m_tick; ask (key, lib->ph[pi]); return 0; }
		b->use = ++m_tick;
		return b->pix.px ? &b->pix : 0;
	}
	// a photo changed (edited): its thumbnails forgotten, on the card too
	void forget (unsigned key)
	{
		Base *b = base_of (key);
		if (b && !b->pending) { b->pix.free_ (); b->key = 0; }
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == key) { delete[] m_sc[i].px; memset (&m_sc[i], 0, sizeof m_sc[i]); }
		char cp[80]; snprintf (cp, sizeof cp, PH_THUMBS "/%08x.jpg", key); kapi_remove (cp);
	}
	// the photo shown big: decoded whole (the latest asked wins)
	void want_full (int pi)
	{
		if (!lib || pi < 0 || pi >= lib->ph.n) return;
		kapi_lock (&m_lk);
		m_fullSerial++; m_full.serial = m_fullSerial; m_full.index = pi; scpy (m_full.path, lib->ph[pi].path, sizeof m_full.path); m_full.orient = lib->ph[pi].orient;
		m_fullWant = true;
		kapi_unlock (&m_lk);
		if (m_ev > 0) kapi_event_set (m_ev);
	}
	int full_serial () const { return m_fullSerial; }
	// the backlog: every photo of the library whose thumbnail is not on the card yet, made in the background (the
	// visible ones still first); progress: blDone of blTotal (blTotal 0: nothing to do, or not counted yet)
	volatile int blDone, blTotal;
	void set_backlog ()
	{
		if (!lib) return;
		int n = 0; for (int i = 0; i < lib->ph.n; i++) if (!lib->ph[i].offline) n++;
		Req *b = (Req *) malloc (sizeof (Req) * (n ? n : 1)); int k = 0;
		for (int i = 0; i < lib->ph.n && b; i++)
		{
			const Photo &p = lib->ph[i]; if (p.offline) continue;
			b[k].key = p.key (); scpy (b[k].path, p.path, sizeof b[k].path); b[k].orient = p.orient; k++;
		}
		kapi_lock (&m_lk);
		free (m_bl); m_bl = b; m_bln = k; m_blGen++;
		kapi_unlock (&m_lk);
		if (m_ev > 0) kapi_event_set (m_ev);
	}

	static void blit (Canvas &cv, const unsigned *px, int x, int y, int w, int h)
	{
		for (int j = 0; j < h; j++)
		{
			int yy = y + j; if (yy < 0 || yy >= cv.h) continue;
			unsigned *row = cv.px + (long) yy * cv.stride;
			int i0 = x < 0 ? -x : 0, i1 = x + w > cv.w ? cv.w - x : w;
			if (i1 > i0) memcpy (row + x + i0, px + (size_t) j * w + i0, (size_t) (i1 - i0) * 4);
		}
	}

private:
	struct Base { unsigned key; Pix pix; unsigned use; bool pending; };
	struct Scaled { unsigned key; int w, h; bool fit; unsigned *px; unsigned use; };
	Base m_base[NBASE]; Scaled m_sc[NSCALED]; unsigned m_tick;
	volatile int m_lk;
	struct Req { unsigned key; char path[400]; int orient; };
	Req m_q[QN]; int m_qn;
	int m_tid; volatile bool m_quit; int m_ev;
	struct FullReq { int serial, index; char path[400]; int orient; };
	FullReq m_full; volatile int m_fullSerial; volatile bool m_fullWant;
	Req *m_bl; int m_bln; volatile int m_blGen;		// the backlog given (the thread takes it)

	Base *base_of (unsigned key) { for (int i = 0; i < NBASE; i++) if (m_base[i].key == key) return &m_base[i]; return 0; }
	Base *base_slot ()
	{
		int best = -1;
		for (int i = 0; i < NBASE; i++) { if (!m_base[i].key) return &m_base[i]; if (!m_base[i].pending && (best < 0 || m_base[i].use < m_base[best].use)) best = i; }
		if (best < 0) return 0;
		Base &b = m_base[best]; b.pix.free_ ();
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == b.key) { delete[] m_sc[i].px; memset (&m_sc[i], 0, sizeof m_sc[i]); }
		b.key = 0; b.use = 0; b.pending = false;
		return &b;
	}
	const unsigned *scaled (unsigned key, int pi, int w, int h, bool fitInside)
	{
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == key && m_sc[i].w == w && m_sc[i].h == h && m_sc[i].fit == fitInside && m_sc[i].px) { m_sc[i].use = ++m_tick; return m_sc[i].px; }
		const Pix *b = base (pi);
		if (!b) return 0;
		int k = -1; unsigned old = ~0u;
		for (int i = 0; i < NSCALED; i++) { if (!m_sc[i].px) { k = i; break; } if (m_sc[i].use < old) { old = m_sc[i].use; k = i; } }
		Scaled &s = m_sc[k]; delete[] s.px;
		s.key = key; s.w = w; s.h = h; s.fit = fitInside; s.use = ++m_tick; s.px = new unsigned[(size_t) w * h];
		if (fitInside) scale_into (b->px, b->w, b->h, 0, 0, b->w, b->h, s.px, w, w, h);
		else cover (*b, s.px, w, h);
		return s.px;
	}
	void ask (unsigned key, const Photo &p)
	{
		kapi_lock (&m_lk);
		if (m_qn == QN) { Base *o = base_of (m_q[0].key); if (o && o->pending) { o->key = 0; o->pending = false; } memmove (m_q, m_q + 1, sizeof (Req) * (QN - 1)); m_qn--; }
		Req &r = m_q[m_qn++]; r.key = key; scpy (r.path, p.path, sizeof r.path); r.orient = p.orient;
		kapi_unlock (&m_lk);
		if (m_ev > 0) kapi_event_set (m_ev);
	}
	static int thread_main (void *p) { ((Thumbs *) p)->run (); return 0; }
	struct Done { Thumbs *t; unsigned key; Pix pix; };
	static void loaded (void *ctx, long)
	{
		Done *d = (Done *) ctx;
		Base *b = d->t->base_of (d->key);
		if (b && b->pending) { b->pending = false; b->pix.take (d->pix); if (!b->pix.px) { b->pix.alloc (2, 2); for (int i = 0; i < 4; i++) b->pix.px[i] = 0x9AA0A8; } }
		d->pix.free_ ();
		if (d->t->onLoaded) d->t->onLoaded ();
		delete d;
	}
	static void full_loaded (void *ctx, long t)
	{
		FullDone *d = (FullDone *) ctx; Thumbs *th = (Thumbs *) t;
		if (d->serial == th->m_fullSerial && th->onFull) th->onFull (d);
		d->pix.free_ ();
		delete d;
	}
	void run ()
	{
		while (!m_quit)
		{
			// the big one first
			FullReq fr; bool full = false;
			kapi_lock (&m_lk);
			if (m_fullWant) { fr = m_full; m_fullWant = false; full = true; }
			kapi_unlock (&m_lk);
			if (full)
			{
				FullDone *d = new FullDone; d->serial = fr.serial; d->index = fr.index; scpy (d->path, fr.path, sizeof d->path); d->failed = false;
				load_full (fr.path, fr.orient, d->pix);
				d->failed = !d->pix.px;
				kapi_post (full_loaded, d, (long) this);
				continue;
			}
			if (pause) { kapi_event_wait (m_ev, 200); continue; }
			Req r; bool have = false;
			kapi_lock (&m_lk);
			if (m_qn > 0) { r = m_q[m_qn - 1]; m_qn--; have = true; }		// (the latest asked first: what is in sight)
			kapi_unlock (&m_lk);
			if (!have) { if (!backlog_step ()) kapi_event_wait (m_ev, 500); continue; }
			Done *d = new Done; d->t = this; d->key = r.key;
			make (r, d->pix);
			kapi_post (loaded, d, 0);
		}
	}
	// the backlog: when a new one is given, the missing thumbnails picked out of it (a file looked for each: fast);
	// then one made at each turn with nothing else to do -> false when there is nothing left
	Req *m_todo; int m_todoN, m_todoAt, m_seenGen;
	static bool thumb_on_card (unsigned key) { char cp[80]; snprintf (cp, sizeof cp, PH_THUMBS "/%08x.jpg", key); void *f = kapi_open (cp); if (f) kapi_close (f); return f != 0; }
	bool backlog_step ()
	{
		if (m_blGen != m_seenGen)
		{
			Req *b = 0; int n = 0;
			kapi_lock (&m_lk); m_seenGen = m_blGen; b = m_bl; n = m_bln; m_bl = 0; m_bln = 0; kapi_unlock (&m_lk);
			free (m_todo); m_todo = b; m_todoN = 0; m_todoAt = 0; blDone = 0; blTotal = 0;
			for (int i = 0; i < n && !m_quit; i++)
			{
				if (m_blGen != m_seenGen) return true;		// (a newer one came meanwhile)
				if (!thumb_on_card (b[i].key)) b[m_todoN++] = b[i];
			}
			blTotal = m_todoN;
		}
		while (m_todoAt < m_todoN && !m_quit)
		{
			Req &r = m_todo[m_todoAt++];
			if (thumb_on_card (r.key)) { blDone = blDone + 1; continue; }	// (made meanwhile, asked by the window)
			Pix px; make (r, px); px.free_ ();
			blDone = blDone + 1;
			if (m_todoAt >= m_todoN && m_ev > 0) kapi_post (backlog_done, this, 0);
			return true;
		}
		return false;
	}
	static void backlog_done (void *t, long) { Thumbs *th = (Thumbs *) t; if (th->onLoaded) th->onLoaded (); }
public:
	// a photo decoded whole, turned the right way, its transparency laid on a grey
	static bool load_full (const char *path, int orientation, Pix &out)
	{
		ImgFrames im; memset (&im, 0, sizeof im);
		if (!img_load (path, &im) || im.n < 1 || im.w <= 0 || im.h <= 0) { img_free (&im); return false; }
		out.free_ (); out.px = im.px[0]; out.w = im.w; out.h = im.h; im.px[0] = 0;
		img_free (&im);
		flatten (out, 0x404448);
		orient (out, orientation);
		return out.px != 0;
	}
private:
	void make (const Req &r, Pix &out)
	{
		char cp[80]; snprintf (cp, sizeof cp, PH_THUMBS "/%08x.jpg", r.key);
		ImgFrames im; memset (&im, 0, sizeof im);
		if (img_load (cp, &im) && im.n >= 1 && im.w > 0 && im.h > 0 && (im.w == BASE || im.h == BASE || (im.w < BASE && im.h < BASE)))
		{
			out.px = im.px[0]; out.w = im.w; out.h = im.h; im.px[0] = 0; img_free (&im);
			for (size_t i = 0, n = (size_t) out.w * out.h; i < n; i++) out.px[i] &= 0xFFFFFF;
			return;
		}
		img_free (&im);
		Pix full;
		if (!load_full (r.path, r.orient, full)) return;
		fit (full, out, BASE, BASE);
		full.free_ ();
		if (!out.px) return;
		opaque (out);
		unsigned n = 0; unsigned char *jpg = pngsave::jpeg_encode (out.px, out.w, out.h, 84, &n);
		for (size_t i = 0, k = (size_t) out.w * out.h; i < k; i++) out.px[i] &= 0xFFFFFF;
		if (jpg) { mkdirs (PH_THUMBS); kapi_save_file (cp, jpg, n); delete[] jpg; }
	}
};

} // namespace photos

#endif
