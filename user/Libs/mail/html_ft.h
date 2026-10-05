//
// mail/html_ft.h -- Mail's HTML renderer on Onyx: the Host (the card's fonts measured by FreeType -- fontkit/fonts.h --,
// the pictures' sizes) and the Painter (a uikit::Canvas: rectangles blended, FreeType's text, the pictures scaled). The
// families: Arial / Helvetica / Verdana -> Liberation Sans, Times -> Liberation Serif, Georgia -> Gelasio, Segoe /
// system-ui -> Selawik, Courier / monospace -> DejaVu Sans Mono. The pictures come from the app (Pictures: a cid:
// from the message, a remote one once allowed and fetched). Include once, in the app's translation unit, after
// fontkit/fonts.h. Part of Mail's own HTML renderer (mail/html.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_HTML_FT_H
#define ONYX_MAIL_HTML_FT_H

#include "mail/html.h"
#include "fontkit/fonts.h"

namespace mail {
namespace html {

// the pictures: a src -> its pixels (0xAARRGGBB, w x h; 0: not shown)
struct Pictures
{
	virtual ~Pictures () {}
	virtual const unsigned *get (const char *src, int *w, int *h) = 0;
};

struct FtHost : Host, Painter
{
	struct F { int fam, px; unsigned char bold, italic; };
	F fonts[96]; int nf;
	int fam[FF_COUNT];
	uikit::Canvas *cv; int cx0, cy0, cx1, cy1;
	Pictures *pics;
	int zoom;				// percent (100: as the HTML says)

	FtHost () : nf (0), cv (0), cx0 (0), cy0 (0), cx1 (0), cy1 (0), pics (0), zoom (100)
	{
		fnt::init ();
		static const char *const N[FF_COUNT][3] = { { "Liberation Sans", "Arimo", "DejaVu Sans" }, { "Liberation Serif", "Tinos", "DejaVu Serif" },
			{ "DejaVu Sans Mono", "Liberation Mono", "Cousine" }, { "Gelasio", "Liberation Serif", "DejaVu Serif" }, { "Selawik", "Liberation Sans", "DejaVu Sans" } };
		for (int i = 0; i < FF_COUNT; i++) { fam[i] = -1; for (int k = 0; k < 3 && fam[i] < 0; k++) fam[i] = fnt::find (N[i][k]); if (fam[i] < 0) fam[i] = 0; }
	}
	fnt::Font *get (int h)
	{
		if (h < 0 || h >= nf) return 0;
		const F &f = fonts[h];
		return fnt::get (f.fam, (f.bold ? fnt::BOLD : 0) | (f.italic ? fnt::ITALIC : 0), f.px * 64);
	}
	// ---- Host ----
	int font (int family, int px, bool bold, bool italic) override
	{
		if (family < 0 || family >= FF_COUNT) family = FF_SANS;
		px = px * zoom / 100; if (px < 4) px = 4;
		int fm = fam[family];
		for (int i = 0; i < nf; i++) if (fonts[i].fam == fm && fonts[i].px == px && fonts[i].bold == bold && fonts[i].italic == italic) return i;
		if (nf == 96) return 0;
		F &f = fonts[nf]; f.fam = fm; f.px = px; f.bold = bold; f.italic = italic;
		return nf++;
	}
	int width (int h, const char *s, int n) override
	{
		fnt::Font *f = get (h); if (!f) return n * 7;
		long x = 0; unsigned prev = 0;
		const char *e = s + n;
		while (s < e)
		{
			unsigned cp = u8get (s); if (s > e) break;
			if (prev) x += fnt::kern (f, prev, cp);
			x += fnt::advance (f, cp == 0xA0 ? ' ' : cp);
			prev = cp;
		}
		return (int) ((x + 32) >> 6);
	}
	void metrics (int h, int *asc, int *desc) override
	{
		fnt::Font *f = get (h);
		if (!f) { *asc = 11; *desc = 3; return; }
		*asc = (f->ascent + 63) >> 6; *desc = (f->descent + 63) >> 6;
	}
	bool image (const char *src, int *w, int *h) override
	{
		*w = *h = 0;
		if (!pics) return false;
		const unsigned *px = pics->get (src, w, h);
		if (px) { *w = *w * zoom / 100; *h = *h * zoom / 100; }
		return px != 0;
	}
	// ---- Painter ----
	void clip (int x0, int y0, int x1, int y1) override
	{
		cx0 = x0 < 0 ? 0 : x0; cy0 = y0 < 0 ? 0 : y0;
		cx1 = cv && x1 > cv->w ? cv->w : x1; cy1 = cv && y1 > cv->h ? cv->h : y1;
	}
	void fill (int x, int y, int w, int h, unsigned c) override
	{
		if (!cv) return;
		int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y, x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
		if (x0 >= x1 || y0 >= y1) return;
		unsigned a = c >> 24;
		for (int j = y0; j < y1; j++)
		{
			unsigned *d = cv->px + j * cv->stride;
			if (a >= 255) for (int i = x0; i < x1; i++) d[i] = 0xFF000000u | c;
			else for (int i = x0; i < x1; i++) fnt::blend (d[i], c, (int) a);
		}
	}
	void text (int h, int x, int base, const char *s, int n, unsigned c) override
	{
		fnt::Font *f = get (h); if (!f || !cv) return;
		long x64 = (long) x * 64; unsigned prev = 0;
		const char *e = s + n;
		while (s < e)
		{
			unsigned cp = u8get (s); if (s > e) break;
			if (prev) x64 += fnt::kern (f, prev, cp);
			fnt::draw (*cv, f, (int) x64, base, cp, c, cx0, cy0, cx1, cy1);
			x64 += fnt::advance (f, cp == 0xA0 ? ' ' : cp);
			prev = cp;
		}
	}
	void picture (const char *src, int x, int y, int w, int h) override
	{
		if (!pics || !cv || w <= 0 || h <= 0) return;
		int iw, ih; const unsigned *px = pics->get (src, &iw, &ih);
		if (!px || iw <= 0 || ih <= 0) return;
		int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y, x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
		for (int j = y0; j < y1; j++)
		{
			const unsigned *srow = px + (long) ((j - y) * ih / h) * iw;
			unsigned *d = cv->px + j * cv->stride;
			for (int i = x0; i < x1; i++)
			{
				unsigned s = srow[(i - x) * iw / w], a = s >> 24;
				if (a >= 255) d[i] = s; else if (a) fnt::blend (d[i], s, (int) a);
			}
		}
	}
};

} // namespace html
} // namespace mail

#endif
