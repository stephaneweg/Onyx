//
// Apps/media/watch.h -- a video playing: Onyx's media library's player (user/av: av_player_open_file -- its
// reader, decoding and sound threads; the frame due now handed over by av_player_poll in the window's
// layout, 0x00RRGGBB) kept for the window: the latest frame copied (the library's is valid until the
// next poll only), the status, and the frame drawn into a rectangle (fitted, black bars) by a fast
// nearest-neighbour scaler (a frame each 33 ms on the Pi: no filtering).
//
#ifndef _media_watch_h
#define _media_watch_h

#include "videos.h"
#include "wtk/wtk.h"

namespace media {

class VideoPlay
{
public:
	struct av_player *p;
	struct av_player_status st;
	unsigned *frame; int fw, fh; unsigned serial;	// the latest frame (0: none yet)
	unsigned statusGen;				// changes when what is shown about the status changes
	VideoPlay () : p (0), frame (0), fw (0), fh (0), serial (0), statusGen (0), m_cap (0), m_last (0) { memset (&st, 0, sizeof st); }
	~VideoPlay () { close (); }

	bool active () const { return p != 0; }
	// -> AV_OK, AV_ERR (the file cannot be read), AV_EUNSUP (not a container the library knows)
	int open (const char *path, int startMs, int volume, bool muted)
	{
		close ();
		p = av_player_new (AV_PIX_BGRA, 0);
		if (!p) return AV_ENOMEM;
		int r = av_player_open_file (p, path);
		if (r != AV_OK) { av_player_free (p); p = 0; return r; }
		set_volume (volume, muted);
		if (startMs > 0) av_player_seek (p, (av_us) startMs * 1000);
		av_player_play (p);
		memset (&st, 0, sizeof st); st.paused = 0;
		return AV_OK;
	}
	void close () { if (p) { av_player_free (p); p = 0; } delete[] frame; frame = 0; m_cap = 0; fw = fh = 0; memset (&st, 0, sizeof st); }
	void set_volume (int v, bool muted) { if (p) av_player_set_volume (p, (float) (v * v) / 10000.0f, muted ? 1 : 0); }
	void play () { if (p) { av_player_play (p); poll (); } }
	void pause () { if (p) { av_player_pause (p); poll (); } }
	void seek (long long ms) { if (p) { if (st.duration > 0 && ms * 1000 > st.duration) ms = st.duration / 1000; av_player_seek (p, (av_us) (ms < 0 ? 0 : ms) * 1000); poll (); } }
	long long pos_ms () const { return st.time > 0 ? st.time / 1000 : 0; }
	long long len_ms () const { return st.duration > 0 ? st.duration / 1000 : 0; }

	// the window's turn: true when a new frame came
	bool poll ()
	{
		if (!p) return false;
		struct av_video_out v;
		int old[6] = { st.paused, st.ended, st.waiting, st.seeking, st.ready, st.error };
		bool got = av_player_poll (p, &st, &v) != 0;
		if (got && v.pixels && v.width > 0 && v.height > 0)
		{
			size_t need = (size_t) v.width * v.height;
			if (need > m_cap) { delete[] frame; frame = new unsigned[need]; m_cap = need; }
			for (int y = 0; y < v.height; y++) memcpy (frame + (size_t) y * v.width, v.pixels + (size_t) y * v.stride, (size_t) v.width * 4);
			fw = v.width; fh = v.height; serial++;
		}
		int now[6] = { st.paused, st.ended, st.waiting, st.seeking, st.ready, st.error };
		if (memcmp (old, now, sizeof old)) statusGen++;
		long long sec = pos_ms () / 1000; if (sec != m_last) { m_last = sec; statusGen++; }
		return got;
	}

	// the frame fitted into (x, y, w, h) of cv, black bars around; its place in *rx.. (for the overlays)
	void draw (Canvas &cv, int x, int y, int w, int h)
	{
		if (!frame || fw <= 0 || fh <= 0) { for (int j = 0; j < h; j++) cv.fillRect (x, y + j, w, 1, 0); return; }
		// the display's shape (the container's display size: anamorphic videos)
		int dw = st.width > 0 ? st.width : fw, dh = st.height > 0 ? st.height : fh;
		int ow = w, oh = (int) ((long long) w * dh / dw);
		if (oh > h) { oh = h; ow = (int) ((long long) h * dw / dh); }
		if (ow < 1) ow = 1; if (oh < 1) oh = 1;
		int ox = x + (w - ow) / 2, oy = y + (h - oh) / 2;
		for (int j = 0; j < oy - y; j++) cv.fillRect (x, y + j, w, 1, 0);
		for (int j = oy + oh; j < y + h; j++) cv.fillRect (x, j, w, 1, 0);
		if (ox > x) cv.fillRect (x, oy, ox - x, oh, 0);
		if (ox + ow < x + w) cv.fillRect (ox + ow, oy, x + w - ox - ow, oh, 0);
		blit_scaled (cv.px, cv.stride, cv.w, cv.h, ox, oy, ow, oh, frame, fw, fh);
	}
	// src (sw x sh) scaled to (dx, dy, dw, dh) of a buffer (its stride, clipped to cw x ch)
	static void blit_scaled (unsigned *dst, int stride, int cw, int ch, int dx, int dy, int dw, int dh, const unsigned *src, int sw, int sh)
	{
		static int *xmap; static int xcap;
		if (dw > xcap) { delete[] xmap; xmap = new int[dw]; xcap = dw; }
		for (int i = 0; i < dw; i++) xmap[i] = (int) ((long long) i * sw / dw);
		int x0 = dx < 0 ? -dx : 0, x1 = dx + dw > cw ? cw - dx : dw;
		for (int j = 0; j < dh; j++)
		{
			int yy = dy + j; if (yy < 0 || yy >= ch) continue;
			const unsigned *s = src + (size_t) ((long long) j * sh / dh) * sw;
			unsigned *d = dst + (size_t) yy * stride + dx;
			if (dw == sw) { memcpy (d + x0, s + x0, (size_t) (x1 - x0) * 4); continue; }
			for (int i = x0; i < x1; i++) d[i] = s[xmap[i]];
		}
	}
private:
	size_t m_cap; long long m_last;
};

} // namespace media

#endif
