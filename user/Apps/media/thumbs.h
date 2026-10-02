//
// Apps/media/thumbs.h -- the videos' frames shown in the library: a frame from about a tenth of the video
// (at most a minute in), decoded by a thread of its own with the media library's lower layers (av_demux,
// av_decoder, av_yuv_to_rgb), cropped to 16:9 and brought to BASE_W x BASE_H; kept on the card as a JPEG
// (SD:/etc/media/thumbs/<key>.jpg) so the next start shows them at once. Each size asked for is made once
// and kept (a small cache). A video this build cannot decode (H.264...) gets a frame drawn from its name.
//
//   g_thumbs.draw (canvas, videoIndex, x, y, w, h, radius, bg);   (a placeholder until it is there)
//
#ifndef _media_thumbs_h
#define _media_thumbs_h

#include "covers.h"
#include "img/pngsave.hpp"

namespace media {

#define THUMB_DIR "SD:/etc/media/thumbs"

// A frame of the video at about `atMs` -> w x h pixels 0x00RRGGBB (new unsigned[]), or 0. `stop`: give up
// (the window plays a video: the thumbnails wait).
static unsigned *video_frame_at (const char *path, int atMs, int *ow, int *oh, volatile bool *stop)
{
	void *f = kapi_open (path);
	if (!f) return 0;
	u64 size = kapi_fsize64 (f);
	struct av_demux *d = av_demux_new (AV_FMT_UNKNOWN);
	struct av_decoder *dec = 0;
	unsigned char *buf = (unsigned char *) malloc (65536);
	unsigned *out = 0; int vt = -1; bool sought = false; av_us target = (av_us) atMs * 1000;
	u64 read = 0; int frames = 0, packets = 0;
	struct av_frame fr; bool have = false; int idle = 0;
	if (d) av_demux_set_size (d, (int64_t) size);
	while (d && buf && read < (48u << 20) && !(stop && *stop) && idle < 3000)
	{
		struct av_packet pk; int r;
		while ((r = av_demux_read (d, &pk)) == AV_OK)
		{
			if (vt < 0)
				for (int i = 0; i < av_demux_ntracks (d); i++) if (av_demux_track (d, i)->kind == AV_VIDEO) { vt = i; break; }
			if (vt >= 0 && !dec) { dec = av_decoder_new (av_demux_track (d, vt)); if (!dec) { av_pkt_free (&pk); goto done; } }
			if (vt >= 0 && !sought)
			{	// the first packet: the tracks are known -- go near the target (an index: Cues, stbl), else decode on from here
				sought = true;
				av_us at = 0; int64_t off = target > 0 ? av_demux_seek (d, target, &at) : -1;
				if (off >= 0) { av_pkt_free (&pk); break; }
				if (target > 4 * AV_US) target = 4 * AV_US;		// (no index: not far)
			}
			if (pk.track == vt)
			{
				packets++;
				av_decoder_send (dec, &pk);
				while (av_decoder_receive (dec, &fr) == AV_OK)
				{
					frames++;
					if (fr.pix == AV_PIX_I420 && fr.width > 0 && fr.height > 0)
					{
						// keep converting the latest: the one at (or past) the target ends it
						delete[] out; out = new unsigned[(size_t) fr.width * fr.height];
						av_yuv_to_rgb (&fr, (uint8_t *) out, fr.width * 4, AV_PIX_BGRA);
						*ow = fr.width; *oh = fr.height; have = true;
						if (fr.pts >= target - 50000 || frames > 90) { av_pkt_free (&pk); goto done; }
					}
				}
			}
			av_pkt_free (&pk);
			if (packets > 400) goto done;
		}
		if (r == AV_EOF || r == AV_ERR || r == AV_EUNSUP) break;
		int64_t want = av_demux_want (d);
		if (want == -1) { kapi_msleep (2); idle++; continue; }	// (FFmpeg's demuxer on its thread: busy)
		if (want < 0 || (u64) want >= size) { av_demux_end (d); kapi_msleep (1); idle++; continue; }
		if (kapi_seek (f, (unsigned long long) want) != 0) break;
		int n = kapi_read (f, buf, 65536);
		if (n <= 0) { av_demux_end (d); continue; }
		av_demux_feed (d, want, buf, (size_t) n); read += (u64) n;
	}
	if (dec && !have)
	{	// the end of what was read: what the decoder holds
		av_decoder_send (dec, 0);
		while (av_decoder_receive (dec, &fr) == AV_OK)
			if (fr.pix == AV_PIX_I420 && fr.width > 0)
			{ delete[] out; out = new unsigned[(size_t) fr.width * fr.height]; av_yuv_to_rgb (&fr, (uint8_t *) out, fr.width * 4, AV_PIX_BGRA); *ow = fr.width; *oh = fr.height; have = true; break; }
	}
done:
	if (!have) { delete[] out; out = 0; }
	if (out) for (size_t i = 0, k = (size_t) *ow * *oh; i < k; i++) out[i] &= 0xFFFFFF;
	av_decoder_free (dec);
	av_demux_free (d);
	free (buf);
	kapi_close (f);
	return out;
}

// src (sw x sh) -> dst (dw x dh), the middle cut to dst's shape (cover), each pixel an average of a few
static void fit_cover (const unsigned *src, int sw, int sh, unsigned *dst, int dw, int dh)
{
	int cw = sw, ch = sw * dh / dw; if (ch > sh) { ch = sh; cw = sh * dw / dh; }
	int ox = (sw - cw) / 2, oy = (sh - ch) / 2;
	for (int y = 0; y < dh; y++)
	{
		int y0 = oy + y * ch / dh, y1 = oy + (y + 1) * ch / dh; if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < dw; x++)
		{
			int x0 = ox + x * cw / dw, x1 = ox + (x + 1) * cw / dw; if (x1 <= x0) x1 = x0 + 1;
			unsigned r = 0, g = 0, b = 0, c = 0;
			for (int yy = y0; yy < y1; yy += 1 + (y1 - y0) / 3) for (int xx = x0; xx < x1; xx += 1 + (x1 - x0) / 3)
			{ unsigned p = src[(size_t) yy * sw + xx]; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; c++; }
			dst[(size_t) y * dw + x] = (r / c) << 16 | (g / c) << 8 | (b / c);
		}
	}
}

class Thumbs
{
public:
	enum { BASE_W = 384, BASE_H = 216, NBASE = 64, NSCALED = 96, QN = 64 };
	VideoLib *lib;
	void (*onLoaded) ();
	volatile bool busy;				// a video plays: the thread waits
	Thumbs () : lib (0), onLoaded (0), busy (false), m_tick (0), m_lk (0), m_qn (0), m_tid (-1), m_quit (false), m_ev (-1)
	{ memset (m_base, 0, sizeof m_base); memset (m_sc, 0, sizeof m_sc); }

	void start () { m_ev = kapi_event_create (0, 0); m_tid = kapi_thread_create (thread_main, this, 1024 * 1024, "thumbs"); }
	void quit () { m_quit = true; busy = true; if (m_ev > 0) kapi_event_set (m_ev); if (m_tid > 0) kapi_thread_join (m_tid, 3000, 0); }
	void set_library (VideoLib *l) { kapi_lock (&m_lk); lib = l; m_qn = 0; kapi_unlock (&m_lk); for (int i = 0; i < NBASE; i++) if (m_base[i].pending) m_base[i].key = 0, m_base[i].pending = false; }

	unsigned key_of (int vi) const { const Video &x = lib->v[vi]; return (hash_str (x.path) ^ (unsigned) x.size * 2654435761u) | 1; }

	// the frame of video vi at (x, y), w x h (16:9 looks best), its corners rounded over bg
	void draw (Canvas &cv, int vi, int x, int y, int w, int h, int radius, unsigned bg)
	{
		if (!lib || vi < 0 || vi >= lib->n || w <= 0 || h <= 0) return;
		const unsigned *px = scaled (key_of (vi), vi, w, h);
		if (px)
			for (int j = 0; j < h; j++)
			{
				int yy = y + j; if (yy < 0 || yy >= cv.h) continue;
				unsigned *row = cv.px + (long) yy * cv.stride;
				for (int i = 0; i < w; i++) { int xx = x + i; if (xx >= 0 && xx < cv.w) row[xx] = px[j * w + i]; }
			}
		else
		{	// (loading) a dark band
			for (int j = 0; j < h; j++) cv.fillRect (x, y + j, w, 1, wk_mix (0x2A3040, 0x141820, j * 256 / h));
		}
		if (radius > 0) Covers::round_corners (cv, x, y, w, h, radius, bg);
	}
	// its main colour
	unsigned tone (int vi)
	{
		if (!lib || vi < 0 || vi >= lib->n) return 0x202838;
		const unsigned *px = scaled (key_of (vi), vi, 16, 9);
		if (!px) return 0x202838;
		unsigned r = 0, g = 0, b = 0;
		for (int i = 0; i < 144; i++) { r += px[i] >> 16 & 255; g += px[i] >> 8 & 255; b += px[i] & 255; }
		return (r / 144) << 16 | (g / 144) << 8 | (b / 144);
	}

private:
	struct Base { unsigned key; unsigned *px; unsigned use; bool pending; };
	struct Scaled { unsigned key; int w, h; unsigned *px; unsigned use; };
	Base m_base[NBASE]; Scaled m_sc[NSCALED]; unsigned m_tick;
	volatile int m_lk;
	struct Req { unsigned key; char path[300]; char title[120]; int atMs; bool playable; };
	Req m_q[QN]; int m_qn;
	int m_tid; volatile bool m_quit; int m_ev;

	Base *base_of (unsigned key) { for (int i = 0; i < NBASE; i++) if (m_base[i].key == key) return &m_base[i]; return 0; }
	Base *base_slot ()
	{
		int best = -1;
		for (int i = 0; i < NBASE; i++) { if (!m_base[i].key) return &m_base[i]; if (!m_base[i].pending && (best < 0 || m_base[i].use < m_base[best].use)) best = i; }
		if (best < 0) return 0;
		Base &b = m_base[best]; delete[] b.px;
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == b.key) { delete[] m_sc[i].px; memset (&m_sc[i], 0, sizeof m_sc[i]); }
		memset (&b, 0, sizeof b); return &b;
	}
	const unsigned *scaled (unsigned key, int vi, int w, int h)
	{
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == key && m_sc[i].w == w && m_sc[i].h == h && m_sc[i].px) { m_sc[i].use = ++m_tick; return m_sc[i].px; }
		Base *b = base_of (key);
		if (!b)
		{
			b = base_slot (); if (!b) return 0;
			b->key = key; b->use = ++m_tick; b->pending = true;
			ask (key, lib->v[vi]);
		}
		b->use = ++m_tick;
		if (!b->px) return 0;
		int k = -1; unsigned old = ~0u;
		for (int i = 0; i < NSCALED; i++) { if (!m_sc[i].px) { k = i; break; } if (m_sc[i].use < old) { old = m_sc[i].use; k = i; } }
		Scaled &s = m_sc[k]; delete[] s.px;
		s.key = key; s.w = w; s.h = h; s.use = ++m_tick; s.px = new unsigned[(unsigned) w * h];
		fit_cover (b->px, BASE_W, BASE_H, s.px, w, h);
		return s.px;
	}
	void ask (unsigned key, const Video &x)
	{
		kapi_lock (&m_lk);
		if (m_qn < QN)
		{
			Req &r = m_q[m_qn++]; r.key = key; scopy (r.path, x.path, sizeof r.path); scopy (r.title, x.title, sizeof r.title); r.playable = x.playable;
			int at = x.durMs / 10; if (at > 60000) at = 60000; if (x.durMs > 0 && at < 1000 && x.durMs > 2000) at = 1000;
			r.atMs = at;
		}
		kapi_unlock (&m_lk);
		if (m_ev > 0) kapi_event_set (m_ev);
	}
	static int thread_main (void *p) { ((Thumbs *) p)->run (); return 0; }
	struct Done { Thumbs *t; unsigned key; unsigned *px; };
	static void loaded (void *ctx, long)
	{
		Done *d = (Done *) ctx;
		Base *b = d->t->base_of (d->key);
		if (b && b->pending) { b->pending = false; b->px = d->px; d->px = 0; }
		delete[] d->px;
		if (d->t->onLoaded) d->t->onLoaded ();
		delete d;
	}
	void run ()
	{
		while (!m_quit)
		{
			if (busy) { kapi_msleep (100); continue; }
			Req r; bool have = false;
			kapi_lock (&m_lk);
			if (m_qn > 0) { r = m_q[m_qn - 1]; m_qn--; have = true; }		// (the latest asked first: what is in sight)
			kapi_unlock (&m_lk);
			if (!have) { kapi_event_wait (m_ev, 500); continue; }
			Done *d = new Done { this, r.key, make (r) };
			kapi_post (loaded, d, 0);
		}
	}
	unsigned *make (const Req &r)
	{
		char cp[64]; snprintf (cp, sizeof cp, THUMB_DIR "/%08x.jpg", r.key);
		ImgFrames im; memset (&im, 0, sizeof im);
		unsigned *px = new unsigned[BASE_W * BASE_H];
		if (img_load (cp, &im) && im.n >= 1 && im.w == BASE_W && im.h == BASE_H)
		{
			for (int i = 0; i < BASE_W * BASE_H; i++) px[i] = im.px[0][i] & 0xFFFFFF;
			img_free (&im);
			return px;
		}
		img_free (&im);
		int w = 0, h = 0;
		unsigned *f = r.playable ? video_frame_at (r.path, r.atMs, &w, &h, &busy) : 0;
		if (f)
		{
			fit_cover (f, w, h, px, BASE_W, BASE_H);
			delete[] f;
			unsigned n = 0; unsigned char *jpg = pngsave::jpeg_encode (px, BASE_W, BASE_H, 85, &n);
			if (jpg) { kapi_mkdir (LIB_DIR); kapi_mkdir (THUMB_DIR); kapi_save_file (cp, jpg, n); delete[] jpg; }
			return px;
		}
		if (busy && r.playable) { delete[] px; requeue (r); return 0; }	// (stopped for a video: asked again later)
		generated (r.title, px);
		return px;
	}
	void requeue (const Req &r) { kapi_lock (&m_lk); if (m_qn < QN) { memmove (m_q + 1, m_q, sizeof (Req) * m_qn); m_q[0] = r; m_qn++; } kapi_unlock (&m_lk); }
	// a frame drawn from the name (a video not decoded here): a gradient, a film's perforations, a play mark
	static void generated (const char *title, unsigned *px)
	{
		static const unsigned PAL[6][2] = { { 0x22304A, 0x5A7AA8 }, { 0x3A2440, 0xA05A8C }, { 0x203C38, 0x4AA08C }, { 0x40301C, 0xC08A48 }, { 0x2C2C34, 0x7A7A8C }, { 0x182838, 0x3C82B4 } };
		unsigned hsh = hash_str (title);
		const unsigned *c = PAL[hsh % 6];
		Canvas cv; cv.adopt (px, BASE_W, BASE_H);
		for (int y = 0; y < BASE_H; y++) cv.fillRect (0, y, BASE_W, 1, wk_mix (c[1], c[0], y * 256 / BASE_H));
		for (int k = 0; k < 12; k++) { wk_rbox (cv, 12 + k * 32, 10, 18, 12, 3, 0x000000, 0x000000, 90); wk_rbox (cv, 12 + k * 32, BASE_H - 22, 18, 12, 3, 0x000000, 0x000000, 90); }
		VPath o; o.circle (V (BASE_W / 2), V (BASE_H / 2), V (34)); o.fill (cv, 0xFFFFFF, 60);
		int t[] = { V (BASE_W / 2 - 11), V (BASE_H / 2 - 18), V (BASE_W / 2 + 19), V (BASE_H / 2), V (BASE_W / 2 - 11), V (BASE_H / 2 + 18) };
		VPath p; p.poly (t, 3); p.fill (cv, 0xFFFFFF, 200);
	}
};

} // namespace media

#endif
