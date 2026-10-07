//
// look.h -- the Telegram app's look, in the way of Windows Live Messenger: the sky-blue gradients and their
// soft swooshes, the "glass" picture frames whose colour says the status (green online, grey away), the
// little buddy figure, the round avatars with initials (Telegram's seven colours) for those without a
// photo, the photos read (img_load) and cut to a square, kept at the sizes drawn. The text faces.
//
// MIT licence.
//
#ifndef TG_LOOK_H
#define TG_LOOK_H

#include "uikit/uikit.h"
#include "uikit/vpaint.h"
#include "imagekit/img/imgload.hpp"
#include "fontkit/uikitface.h"
#include "client.h"

using namespace uikit;

// ---- colours ------------------------------------------------------------------------------------------

#define TC_BG		0xFFFFFF		// the lists, the conversation
#define TC_SKY_TOP	0xCFE3F5		// the headers' gradient
#define TC_SKY_BOT	0xF4F9FD
#define TC_SKY_DEEP	0x9CC4EA
#define TC_LINE		0xC5D7E8		// the separating lines
#define TC_INK		0x1B1B1B		// the text
#define TC_GREY		0x6E7781		// secondary text
#define TC_LIGHT	0x9AA4AE		// the times
#define TC_TITLE	0x1E3D6B		// the names in the headers, the groups' titles
#define TC_LINK		0x1A5FB4
#define TC_SEL_TOP	0xDCEBFC		// a selected row (the Windows 7 way)
#define TC_SEL_BOT	0xC1DCF7
#define TC_SEL_RIM	0x84ACDD
#define TC_HOT_TOP	0xF2F8FE
#define TC_HOT_BOT	0xE3EFFC
#define TC_HOT_RIM	0xB8D6FB
#define TC_ONLINE	0x58B830
#define TC_AWAY		0xF2A20C
#define TC_BUSY		0xD9302C
#define TC_OFFLINE	0xA3A9B0
#define TC_UNREAD	0x2F8CE0		// the unread count's pill
#define TC_INPUT_BG	0xEAF2FA
#define TC_ME		0x2B5797		// "says:" of ours
#define TC_THEM		0x7A3E9D		// ... and theirs

// Telegram's peer colours (the avatar without a photo).
static const unsigned tc_peer[7][2] = {
	{ 0xFF885E, 0xFF516A }, { 0xFFCD6A, 0xFFA85C }, { 0xA0DE7E, 0x54CB68 }, { 0x53EDD6, 0x28C9B7 },
	{ 0x72D5FD, 0x2A9EF1 }, { 0xE0A2F3, 0xD669ED }, { 0x82B1FF, 0x665FFF } };
static int peer_colour (long long id) { long long k = id < 0 ? -id : id; return (int) (k % 7); }

// ---- the text faces -------------------------------------------------------------------------------------

struct Faces { FtTextFace *ui, *small, *big, *title, *huge; };
static Faces g_face;

static void faces_init ()
{
	g_face.ui = new FtTextFace; g_face.ui->open ("DejaVu Sans", 13);
	g_face.small = new FtTextFace; g_face.small->open ("DejaVu Sans", 11);
	g_face.big = new FtTextFace; g_face.big->open ("DejaVu Sans", 15);
	g_face.title = new FtTextFace; g_face.title->open ("DejaVu Sans", 18);
	g_face.huge = new FtTextFace; g_face.huge->open ("DejaVu Sans", 24);
}

// Text in a face, cut with "..." to w px (0: no limit).
static void ftext (Canvas &cv, FtTextFace *f, int x, int y, const char *s, unsigned c, int style = 0, int w = 0)
{
	if (!s || !*s) return;
	if (w > 0 && f->width (s, style) > w)
	{
		static char b[512];
		int n = 0, ell = f->width ("...", style);
		const char *p = s;
		while (*p && n < (int) sizeof b - 4)
		{
			int k = uk_u8_len (p, 4);
			memcpy (b + n, p, (size_t) k); b[n + k] = 0;
			if (f->width (b, style) + ell > w) break;
			n += k; p += k;
		}
		while (n > 0 && b[n - 1] == ' ') n--;
		memcpy (b + n, "...", 4);
		f->draw (cv, x, y, b, c, style);
		return;
	}
	f->draw (cv, x, y, s, c, style);
}
static int ftw (FtTextFace *f, const char *s, int style = 0) { return s && *s ? f->width (s, style) : 0; }

// ---- backgrounds ----------------------------------------------------------------------------------------

static void fill_grad (Canvas &cv, int x, int y, int w, int h, unsigned top, unsigned bot)
{
	for (int r = 0; r < h; r++) cv.fillRect (x, y + r, w, 1, uk_mix (top, bot, h > 1 ? r * 256 / (h - 1) : 0));
}

// The sky: the gradient, two soft swooshes of light (Messenger's), a line under it.
static void sky (Canvas &cv, int x, int y, int w, int h, bool line = true)
{
	fill_grad (cv, x, y, w, h, TC_SKY_TOP, TC_SKY_BOT);
	VPath p;
	int pts[16] = { V (x + w / 3), V (y), V (x + w), V (y), V (x + w), V (y + h * 2 / 3), V (x + w * 2 / 3), V (y + h / 3) };
	p.poly (pts, 4); p.fill (cv, 0xFFFFFF, 60);
	p.clear ();
	int pts2[8] = { V (x + w / 2), V (y), V (x + w * 3 / 4), V (y), V (x + w), V (y + h / 3), V (x + w), V (y + h / 2) };
	p.poly (pts2, 4); p.fill (cv, 0xFFFFFF, 50);
	p.clear (); p.ellipse (V (x + w - w / 6), V (y - h / 3), V (w / 3), V (h * 2 / 3)); p.fill (cv, 0xFFFFFF, 45);
	if (line) cv.fillRect (x, y + h - 1, w, 1, TC_LINE);
}

// ---- the status --------------------------------------------------------------------------------------------

enum { TS_ONLINE, TS_AWAY, TS_OFFLINE, TS_GROUP, TS_BOT };

static int user_ts (tg::User *u, int now)
{
	if (!u) return TS_OFFLINE;
	if (u->bot) return TS_BOT;
	if (u->status == tg::ST_ONLINE && (u->expires == 0 || u->expires > now)) return TS_ONLINE;
	if (u->status == tg::ST_RECENTLY) return TS_AWAY;
	if (u->status == tg::ST_OFFLINE && now - u->wasOnline < 15 * 60) return TS_AWAY;
	return TS_OFFLINE;
}
static unsigned ts_colour (int ts) { return ts == TS_ONLINE ? TC_ONLINE : ts == TS_AWAY ? TC_AWAY : ts == TS_GROUP ? 0x3A8EE6 : ts == TS_BOT ? 0x8A6FDB : TC_OFFLINE; }

// The buddy: a little person (two for a group) in the status colour, glossy.
static void buddy (Canvas &cv, int x, int y, int s, unsigned c, bool group = false)
{
	VPath p;
	float k = s / 16.0f;
	unsigned dark = uk_mix (c, 0x000000, 90), light = uk_mix (c, 0xFFFFFF, 110);
	for (int who = group ? 1 : 0; who >= 0; who--)
	{
		float ox = group ? (who ? 4.0f * k : -2.0f * k) : 0, oy = group && who ? -1.5f * k : 0;
		unsigned body = who ? uk_mix (c, 0xFFFFFF, 70) : c;
		p.clear (); p.circle (V (x) + (int) ((8 + ox) * k * 16), V (y) + (int) ((4.6f + oy) * k * 16), (int) (3.6f * k * 16)); p.ellipse (V (x) + (int) ((8 + ox) * k * 16), V (y) + (int) ((13.2f + oy) * k * 16), (int) (6.2f * k * 16), (int) (4.6f * k * 16)); p.fill (cv, dark);
		p.clear (); p.circle (V (x) + (int) ((8 + ox) * k * 16), V (y) + (int) ((4.6f + oy) * k * 16), (int) (2.9f * k * 16)); p.ellipse (V (x) + (int) ((8 + ox) * k * 16), V (y) + (int) ((13.2f + oy) * k * 16), (int) (5.4f * k * 16), (int) (3.9f * k * 16)); p.fill (cv, body);
		p.clear (); p.ellipse (V (x) + (int) ((7 + ox) * k * 16), V (y) + (int) ((3.4f + oy) * k * 16), (int) (1.5f * k * 16), (int) (1.0f * k * 16)); p.fill (cv, light, 200);
	}
}

// ---- the avatars ---------------------------------------------------------------------------------------------

struct AvEntry { long long key; int size; unsigned *px; };
static AvEntry g_av[256];
static int g_nav = 0;

// A photo file -> its square middle at size x size (0: unreadable).
static unsigned *av_load (const char *path, int size)
{
	ImgFrames im;
	if (!img_load (path, &im)) return 0;
	int w = im.w, h = im.h, sq = w < h ? w : h, ox = (w - sq) / 2, oy = (h - sq) / 2;
	unsigned *src = im.px[0], *out = new unsigned[(size_t) size * size];
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++)
		{
			int x0 = ox + x * sq / size, x1 = ox + (x + 1) * sq / size, y0 = oy + y * sq / size, y1 = oy + (y + 1) * sq / size;
			if (x1 <= x0) x1 = x0 + 1;
			if (y1 <= y0) y1 = y0 + 1;
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++)
			{ unsigned c = src[yy * w + xx]; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; }
			out[y * size + x] = n ? ((r / n) << 16) | ((g / n) << 8) | (b / n) : 0;
		}
	img_free (&im);
	return out;
}

// src (sw x sh) brought to dw x dh: the average of what each pixel covers (smaller), the nearest (larger).
static unsigned *scale_box (const unsigned *src, int sw, int sh, int dw, int dh)
{
	unsigned *out = new unsigned[(size_t) dw * dh];
	for (int y = 0; y < dh; y++)
		for (int x = 0; x < dw; x++)
		{
			int x0 = x * sw / dw, x1 = (x + 1) * sw / dw, y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
			if (x1 <= x0) x1 = x0 + 1;
			if (y1 <= y0) y1 = y0 + 1;
			if (x1 > sw) x1 = sw;
			if (y1 > sh) y1 = sh;
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++)
			{ unsigned c = src[yy * sw + xx]; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; }
			out[y * dw + x] = n ? ((r / n) << 16) | ((g / n) << 8) | (b / n) : 0xFFFFFF;
		}
	return out;
}

// A rectangle of pixels (w x h) laid at (x, y), its corners rounded (r).
static void blit_rect_round (Canvas &cv, const unsigned *px, int x, int y, int w, int h, int r)
{
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			int dx = i < r ? r - i : i >= w - r ? i - (w - r - 1) : 0, dy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0;
			int a = 255;
			if (dx && dy)
			{
				int d2 = (dx - 1) * (dx - 1) + (dy - 1) * (dy - 1), r2 = r * r;
				if (d2 >= r2) continue;
				if (d2 > (r - 1) * (r - 1)) a = 255 * (r2 - d2) / (r2 - (r - 1) * (r - 1) + 1);
			}
			if (a >= 255) cv.pixel (x + i, y + j, px[j * w + i]);
			else uk_blend_px (cv, x + i, y + j, px[j * w + i], a);
		}
}

// The peer's photo at size (cached), 0 if it has none (yet).
static unsigned *av_photo (tg::Client &c, long long peer, int size)
{
	long long pid = 0;
	if (tg::ptype (peer) == tg::P_USER) { tg::User *u = c.user (tg::pid (peer)); if (u) pid = u->photoId; }
	else { tg::Chat *ch = c.chat (tg::pid (peer)); if (ch) pid = ch->photoId; }
	if (!pid) return 0;
	for (int i = 0; i < g_nav; i++) if (g_av[i].key == pid && g_av[i].size == size) return g_av[i].px;
	char path[160];
	if (!c.photo (peer, path, sizeof path)) return 0;
	unsigned *px = av_load (path, size);
	if (!px) return 0;
	if (g_nav == 256) { delete [] g_av[0].px; memmove (g_av, g_av + 1, sizeof g_av[0] * 255); g_nav--; }
	g_av[g_nav].key = pid; g_av[g_nav].size = size; g_av[g_nav].px = px; g_nav++;
	return px;
}

// The initials of a name (two letters at most, UTF-8).
static void initials (const char *name, char *out)
{
	int o = 0, words = 0;
	const char *p = name;
	while (*p && words < 2)
	{
		while (*p == ' ') p++;
		if (!*p) break;
		int k = uk_u8_len (p, 4);
		unsigned cp = uk_u8_get (p, 4, 0);
		if (cp >= 'a' && cp <= 'z') cp -= 32;
		else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 32;
		if (cp < 0x2000) o += uk_u8_put (out + o, cp);
		(void) k;
		words++;
		while (*p && *p != ' ') p++;
	}
	out[o] = 0;
}

// Pixels laid in a rounded square (r) at (x, y), the corners anti-aliased over what is there.
static void blit_round (Canvas &cv, const unsigned *px, int x, int y, int s, int r)
{
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			int dx = i < r ? r - i : i >= s - r ? i - (s - r - 1) : 0, dy = j < r ? r - j : j >= s - r ? j - (s - r - 1) : 0;
			int a = 255;
			if (dx && dy)
			{
				int d2 = (dx - 1) * (dx - 1) + (dy - 1) * (dy - 1), r2 = r * r;
				if (d2 >= r2) continue;
				if (d2 > (r - 1) * (r - 1)) a = 255 * (r2 - d2) / (r2 - (r - 1) * (r - 1) + 1);
			}
			if (a >= 255) cv.pixel (x + i, y + j, px[j * s + i]);
			else uk_blend_px (cv, x + i, y + j, px[j * s + i], a);
		}
}

// A round avatar (Telegram's way, in the lists): the photo, else the initials on the peer's colour.
static void avatar_round (Canvas &cv, tg::Client &c, long long peer, int x, int y, int s)
{
	unsigned *px = av_photo (c, peer, s);
	if (px) { blit_round (cv, px, x, y, s, s / 2); return; }
	int col = peer_colour (tg::pid (peer));
	VPath p;
	p.circle (V (x) + V (s) / 2, V (y) + V (s) / 2, V (s) / 2);
	p.fill (cv, tc_peer[col][1]);
	p.clear (); p.circle (V (x) + V (s) / 2, V (y) + V (s) / 2 - V (s) / 10, V (s) * 2 / 5); p.fill (cv, tc_peer[col][0], 120);
	char name[128], ini[16];
	c.peerName (peer, name, sizeof name);
	initials (name, ini);
	FtTextFace *f = s >= 56 ? g_face.huge : s >= 36 ? g_face.big : g_face.ui;
	int tw = ftw (f, ini, 2);
	f->draw (cv, x + (s - tw) / 2, y + (s - f->height ()) / 2, ini, 0xFFFFFF, 2);
}

// The glass frame of Messenger around a picture: s the picture, the frame b px around it, its colour the status.
static void frame (Canvas &cv, int x, int y, int s, int b, unsigned c)
{
	int o = s + 2 * b;
	uk_rbox (cv, x - b - 1, y - b - 1, o + 2, o + 2, b + 4, uk_mix (c, 0x000000, 80), uk_mix (c, 0x000000, 110));
	uk_rbox (cv, x - b, y - b, o, o, b + 3, uk_mix (c, 0xFFFFFF, 150), c);
	uk_rbox (cv, x - b, y - b, o, o / 2, b + 3, 0xFFFFFF, 0xFFFFFF, 90, UK_TL | UK_TR);
	uk_rbox (cv, x - 1, y - 1, s + 2, s + 2, 4, 0xFFFFFF, 0xFFFFFF);
}

// A framed picture (the headers', the conversation's display pictures): the photo, else the initials.
static void avatar_framed (Canvas &cv, tg::Client &c, long long peer, int x, int y, int s, unsigned status)
{
	int b = s >= 80 ? 6 : s >= 48 ? 5 : 3;
	frame (cv, x, y, s, b, status);
	unsigned *px = av_photo (c, peer, s);
	if (px) { blit_round (cv, px, x, y, s, 3); return; }
	int col = peer_colour (tg::pid (peer));
	uk_rbox (cv, x, y, s, s, 3, tc_peer[col][0], tc_peer[col][1]);
	char name[128], ini[16];
	c.peerName (peer, name, sizeof name);
	initials (name, ini);
	FtTextFace *f = s >= 56 ? g_face.huge : s >= 36 ? g_face.big : g_face.ui;
	int tw = ftw (f, ini, 2);
	f->draw (cv, x + (s - tw) / 2 + 1, y + (s - f->height ()) / 2 + 1, ini, uk_mix (tc_peer[col][1], 0, 100), 2);
	f->draw (cv, x + (s - tw) / 2, y + (s - f->height ()) / 2, ini, 0xFFFFFF, 2);
}

// The default picture (the sign-in's): Messenger's two buddies on the sky.
static void avatar_default (Canvas &cv, int x, int y, int s, unsigned status)
{
	int b = s >= 80 ? 7 : 5;
	frame (cv, x, y, s, b, status);
	uk_rbox (cv, x, y, s, s, 3, 0xE9F4FF, 0xB9D8F5);
	buddy (cv, x + s * 36 / 100, y + s * 10 / 100, s * 54 / 100, 0x3A8EE6);
	buddy (cv, x + s * 8 / 100, y + s * 30 / 100, s * 62 / 100, TC_ONLINE);
}

// "last seen ..." in the user's words.
static void status_text (tg::Client &c, tg::User *u, char *out, int cap)
{
	out[0] = 0;
	if (!u) return;
	int now = c.serverTime ();
	if (u->bot) { snprintf (out, (size_t) cap, "%s", TR ("bot")); return; }
	if (u->id == c.selfId) { snprintf (out, (size_t) cap, "%s", c.wantOnline () ? TR ("Online") : TR ("Appear offline")); return; }
	switch (u->status)
	{
	case tg::ST_ONLINE: if (!u->expires || u->expires > now) { snprintf (out, (size_t) cap, "%s", TR ("Online")); return; } // fall through
	case tg::ST_OFFLINE:
	{
		int w = u->status == tg::ST_ONLINE ? u->expires : u->wasOnline;
		int ago = now - w;
		if (ago < 60) snprintf (out, (size_t) cap, "%s", TR ("last seen just now"));
		else if (ago < 3600) snprintf (out, (size_t) cap, TR ("last seen %d min ago"), ago / 60);
		else if (ago < 86400) snprintf (out, (size_t) cap, TR ("last seen %d h ago"), ago / 3600);
		else snprintf (out, (size_t) cap, TR ("last seen %d days ago"), ago / 86400);
		return;
	}
	case tg::ST_RECENTLY: snprintf (out, (size_t) cap, "%s", TR ("last seen recently")); return;
	case tg::ST_WEEK: snprintf (out, (size_t) cap, "%s", TR ("last seen within a week")); return;
	case tg::ST_MONTH: snprintf (out, (size_t) cap, "%s", TR ("last seen within a month")); return;
	default: snprintf (out, (size_t) cap, "%s", TR ("last seen a long time ago")); return;
	}
}

#endif
