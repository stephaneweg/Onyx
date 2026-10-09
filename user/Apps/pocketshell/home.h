//
// home.h -- pocketshell's LAUNCHER, v2 (docs/COMPACT-SHELL-STUDY.md section 6.2, the approved mock-ups
// docs/compact-shell/mockups/pocket-home-v2.png, -1080, -search; drawn the same way: tools/screenshot/mockup_compact.py's
// pocket_home_v2). The home window (backmost, at the work area) holds, in logical units times the scale:
//
//   * a round SEARCH FIELD with a soft shadow (an Aqua ring and a clear button while typing) and, beside it, TODAY:
//     the Calendar's next appointment on one line (today.h; a click opens the Calendar; none: not drawn) -- at a
//     logical width of 1100 and more a TODAY COLUMN at the right: the date, the agenda, the notifications;
//   * the categories as CHIPS on the wallpaper (Recent, the categories of app.txt in the dock's order, Settings);
//   * ONE RAISED CARD with a header ("Productivity  14 apps") and the apps on PLATES (one shape for every icon); the
//     keyboard's focus an Aqua ring and glow, the label in a pill; a dot under a running app. Recent: the apps opened
//     last and, when there is room, DOCUMENTS -- the files opened last (SystemKit's recent.h), with their app's icon;
//   * the RUNNING strip at the bottom: the open apps as thumbnails of their windows (PocketUI's copies, uk_shell_thumb),
//     the icon and the name on a dark foot, a close button; the key hints at its right end;
//   * typing: SEARCH RESULTS -- the chips become the kinds (All, Apps, Settings, Files, with their counts), the best
//     match a card at the left (Enter opens it), the others grouped (apps, the settings by their help lines, the
//     files), the matched letters in Aqua, the bottom line runs the text in a Terminal.
//
// Keys: Tab / Shift+Tab the category (searching: the next group), the arrows choose (Down from the card's last row:
// the Running strip -- Enter brings that app, Del closes it, Up back), Enter opens, Esc clears the search (else back to
// the app), typing searches. Part of main.cpp (one unit).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//

static int g_tab = 0;					// the category shown (g_cats)
static int g_tabFirst = 0;				// the first chip drawn (they scroll)
static int g_focus = 0;					// the card's focused item (a tile, then the documents)
static int g_stripFocus = -1;				// the Running strip's focused app (-1: the card has the focus)
static int g_gridTop = 0;				// the grid's first row shown
static char g_query[64];				// the search's text ("": the categories)
static int g_resSel = 0, g_resTop = 0;
static int g_kind = 0;					// the results shown: 0 all, 1 apps, 2 settings, 3 files
enum { H_TAB = 1, H_CHEV, H_TILE, H_DOC, H_RUNCARD, H_RUNX, H_RESULT, H_FIELD, H_CLEARQ, H_TODAY, H_AGENDA, H_HNOTE, H_KIND };

static const unsigned INK = 0x18181A, INK2 = 0x66666C, CAPS = 0x82868E;

static int D10 (int lp10)	{ return (lp10 * S + 500) / 1000; }	// tenths of a logical unit -> pixels

// ---- the faces, by their size in logical units (opened at the scale; again when it changes) ---------------------
static FtTextFace *g_faces[32];
static FtTextFace *F (int lp)
{
	if (lp < 6 || lp > 31) return 0;
	if (g_faces[lp] == 0)
	{
		g_faces[lp] = new FtTextFace;
		if (!g_faces[lp]->open ("DejaVu Sans", D (lp))) { delete g_faces[lp]; g_faces[lp] = 0; }
	}
	return g_faces[lp];
}
static void faces_rescale (void)
{
	for (int i = 0; i < 32; i++) if (g_faces[i] != 0) g_faces[i]->open ("DejaVu Sans", D (i));
}
// text centred in a box of height h at y
static void text_v (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int style = 0) { uk_text (cv, x, y + (h - uk_fh ()) / 2, s, c, style); }

// ---- the layout (pixels), from the logical width and height ---------------------------------------------------------
struct Lay
{
	bool side, narrow, strip, today;
	int M;
	int sx, sy, sw, sh;			// the search field
	int tx, ty, tw, th;			// the Today line
	int cx, cy, cw, ch;			// the chips
	int px, py, pw, ph;			// the card
	int colx, colw;				// the Today column
	int ry, rh;				// the Running strip (its line of words, then the thumbnails)
};
static Lay lay (void)
{
	Lay L;
	memset (&L, 0, sizeof L);
	bool q = g_query[0] != 0;
	L.M = D (16);
	L.side = !q && LW () >= 1100 && !portrait ();
	L.narrow = LW () < 700;
	L.strip = !q && LH () >= 400;
	L.today = !q && !L.side && agenda_next () >= 0;
	L.sx = L.M; L.sy = D (12); L.sh = D (34);
	int y = L.sy + L.sh;
	if (q || L.narrow) L.sw = g_aw - 2 * L.M;
	else if (L.side) L.sw = D (480);
	else L.sw = L.today ? D (380) : g_aw - 2 * L.M;
	if (L.today && L.narrow)
	{
		if (LH () >= 560) { L.tx = L.M; L.ty = y + D (8); L.tw = g_aw - 2 * L.M; L.th = D (30); y = L.ty + L.th; }
		else L.today = false;
	}
	else if (L.today) { L.tx = L.sx + L.sw + D (12); L.ty = L.sy; L.tw = g_aw - L.M - L.tx; L.th = L.sh; }
	L.colw = L.side ? D (300) : 0;
	L.colx = g_aw - L.M - L.colw;
	L.cx = L.M; L.cy = y + D (12); L.ch = D (30); L.cw = g_aw - 2 * L.M - (L.side ? L.colw + L.M : 0);
	L.rh = L.strip ? D (104) : D (12);
	L.px = L.M; L.py = L.cy + L.ch + D (12); L.pw = L.cw; L.ph = g_ah - L.py - L.rh;
	if (L.ph < D (120)) L.ph = D (120);
	L.ry = g_ah - L.rh + D (10);
	return L;
}

// ---- the recent documents (SystemKit's recent.h), those still on the card ------------------------------------------------
#define MAXDOCS 12
static struct recent_doc g_docs[MAXDOCS];
static int g_ndocs;
static void docs_load (void)
{
	static struct recent_doc d[RECENT_DOCS_MAX];
	int n = recent_docs (d, RECENT_DOCS_MAX);
	g_ndocs = 0;
	for (int i = 0; i < n && g_ndocs < MAXDOCS; i++) if (fs_exists (d[i].path) && !fs_is_dir (d[i].path)) g_docs[g_ndocs++] = d[i];
}
// "today, 11:20", "yesterday", "Monday", "2 Oct"
static void doc_when (const struct recent_doc &d, char *o, int cap)
{
	int k = 0; o[0] = 0;
	if (d.date <= 0) return;
	int days = day_number (today_ymd ()) - day_number (d.date);
	if (days == 0)
	{
		char t[6]; hm_text (t, d.time / 100 * 60 + d.time % 100);
		lx_cat (o, cap, &k, TR ("today")); lx_cat (o, cap, &k, ", "); lx_cat (o, cap, &k, t);
	}
	else if (days == 1) lx_cat (o, cap, &k, TR ("yesterday"));
	else if (days > 1 && days < 7) lx_cat (o, cap, &k, TR (WD[weekday (d.date / 10000, d.date / 100 % 100, d.date % 100)]));
	else { num_cat (o, cap, &k, d.date % 100); lx_cat (o, cap, &k, " "); lx_cat (o, cap, &k, TR (MOS[(d.date / 100 % 100 + 11) % 12])); }
}
static void dir_of (const char *path, char *o, int cap)		// "SD:/docs" of "SD:/docs/x.rtf"
{
	const char *b = path;
	for (const char *p = path; *p; p++) if (*p == '/') b = p;
	int n = (int) (b - path);
	if (n > cap - 1) n = cap - 1;
	memcpy (o, path, (size_t) n); o[n] = 0;
}

// ---- the card's contents: the tiles (and, in Recent, the documents) ------------------------------------------------------
static int g_items[MAXAPPS], g_nitems;			// the apps of the category shown
static void tab_items (void)
{
	g_nitems = 0;
	if (g_tab < 0 || g_tab >= g_ncats) return;
	const char *c = g_cats[g_tab].name;
	if (ieq (c, "Recent"))
	{
		for (int r = 0; r < g_nrecent; r++)
			for (int i = 0; i < g_napps; i++)
				if (!g_apps[i].applet && ieq (g_apps[i].name, g_recent[r])) { g_items[g_nitems++] = i; break; }
		return;
	}
	bool set = ieq (c, "Settings");
	for (int i = 0; i < g_napps; i++)
		if (set ? g_apps[i].applet : (!g_apps[i].applet && !g_apps[i].hidden && ieq (g_apps[i].cat, c))) g_items[g_nitems++] = i;
}
static bool tab_is_recent (void) { return g_tab >= 0 && g_tab < g_ncats && ieq (g_cats[g_tab].name, "Recent"); }

struct Card
{
	int gx, gy, cellw, rowh, cols, rows;	// the tiles' grid
	int ntiles;				// the tiles reachable (Recent with documents: the first row)
	bool docs; int dx, dy, dw, dh, dgap, dcols, ndocs;	// the documents
	int total;				// tiles + documents
};
static Card card (const Lay &L)
{
	Card c;
	memset (&c, 0, sizeof c);
	c.cellw = D (94); c.rowh = D (100);
	c.cols = (L.pw - D (16)) / c.cellw;
	if (c.cols < 1) c.cols = 1;
	c.gx = L.px + (L.pw - c.cols * c.cellw) / 2;
	c.gy = L.py + D (46);
	c.rows = (L.ph - D (46) + D (6)) / c.rowh;
	if (c.rows < 1) c.rows = 1;
	c.ntiles = g_nitems;
	if (tab_is_recent () && g_ndocs > 0)
	{
		c.dx = L.px + D (18); c.dgap = D (12); c.dh = D (56);
		c.dcols = L.pw >= D (900) ? 3 : L.pw >= D (540) ? 2 : 1;
		c.dw = (L.pw - D (36) - c.dgap * (c.dcols - 1)) / c.dcols;
		c.dy = c.gy + c.rowh + D (44);
		int fit = (L.py + L.ph - D (10) - c.dy + c.dgap) / (c.dh + c.dgap);	// the documents' rows that fit
		if (fit > 0)
		{
			c.docs = true;
			c.ndocs = g_ndocs < fit * c.dcols ? g_ndocs : fit * c.dcols;
			c.rows = 1;
			if (c.ntiles > c.cols) c.ntiles = c.cols;
		}
	}
	c.total = c.ntiles + c.ndocs;
	return c;
}

// ---- the pieces --------------------------------------------------------------------------------------------------------
// A key's cap: 18 lp, a light gradient, its word bold -> its width.
static int cap2 (Canvas &cv, int x, int y, const char *k, bool measure = false)
{
	UkFaceScope f (F (10));
	int w = tw (k, 2) + D (10), h = D (18);
	if (w < D (18)) w = D (18);
	if (measure) return w;
	lk_fill (cv, x, y, w, h, D (4), 0xF8F8F9, 0xCCCCCE);
	lk_ring (cv, x, y, w, h, D (4), 16, 0x000000, 70);
	text_v (cv, x + (w - tw (k, 2)) / 2, y - D10 (5), h, k, 0x1E1E20, 2);
	return w;
}
// Hints: "keys|word" pairs, the keys split at '+' or ' ' (each a cap) -> the width drawn.
static int hints2 (Canvas &cv, int x, int y, const char *const *h, int n, unsigned ink, bool measure = false)
{
	int x0 = x;
	for (int i = 0; i + 1 < n; i += 2)
	{
		const char *p = h[i];
		while (*p)
		{
			char k[24]; int j = 0;
			while (*p && *p != '+' && *p != ' ' && j < 23) k[j++] = *p++;
			k[j] = 0;
			if (*p) p++;
			const char *kt = TR (k);
			x += cap2 (cv, x, y, kt, measure) + D (3);
		}
		UkFaceScope f (F (11));
		const char *w = TR (h[i + 1]);
		if (!measure) text_v (cv, x + D (2), y, D (18), w, ink);
		x += D (2) + tw (w) + D (14);
	}
	return x - x0;
}

// The magnifier: a ring and its handle.
static void magnifier (Canvas &cv, int x, int y, int s, unsigned c)
{
	int r = s * 6 / 15;
	lk_ring (cv, x, y, 2 * r, 2 * r, r, D10 (18) * 16 / 10 > 16 ? D10 (18) * 16 / 10 : 20, c);
	int t = D10 (20) > 1 ? D10 (20) : 2;
	for (int k = 0; k < s - 2 * r + D (1); k++)
		for (int j = 0; j < t; j++)
			lk_px (cv, x + 2 * r - D (2) + k + j / 2, y + 2 * r - D (2) + k - (j + 1) / 2, c, 255);
}

// An app's icon on a plate (60 lp, rounded), its label under it; the focus: an Aqua ring and glow, the label in a pill.
static void plate (Canvas &cv, int x, int y, int cw, App &a, const char *label, bool focus, bool running)
{
	int ps = D (60), px = x + (cw - ps) / 2, py = y;
	if (focus) lk_shadow (cv, px - D (4), py - D (4), ps + D (8), ps + D (8), D (19), D (7) / 2 + 1, 150, 0, uk_tone (C_ACCENT, 140));
	lk_shadow (cv, px, py, ps, ps, D (15), D10 (25) > 1 ? D10 (25) / 2 + 1 : 1, 55, D10 (15));
	lk_fill (cv, px, py, ps, ps, D (15), 0xFFFFFF, 0xEAECF1);
	lk_ring (cv, px, py, ps, ps, D (15), 16, 0x000000, 26);
	if (focus) lk_ring (cv, px - D (4), py - D (4), ps + D (8), ps + D (8), D (19), D10 (25) * 16 / 10 < 24 ? 24 : D10 (25) * 16 / 10, C_ACCENT);
	int is = D (44);
	draw_icon (cv, px + (ps - is) / 2, py + (ps - is) / 2, a, is);
	if (running) lk_fill (cv, x + cw / 2 - D10 (25), py + ps + D (5), D (5), D (5), D10 (25), C_ACCENT, C_ACCENT);
	UkFaceScope f (F (11));
	char lab[64];
	int lw = uk_text_fit (label, cw - D (focus ? 18 : 8), lab, sizeof lab, focus ? 2 : 0);
	int ly = py + ps + D (12);
	if (focus)
	{
		lk_fill (cv, x + (cw - lw) / 2 - D (8), ly, lw + D (16), D (18), D (9), uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 118));
		text_v (cv, x + (cw - lw) / 2, ly, D (18), lab, 0xFFFFFF, 2);
	}
	else text_v (cv, x + (cw - lw) / 2, ly, D (18), lab, INK);
}

// The card's raised panel.
static void panel_card (Canvas &cv, int x, int y, int w, int h)
{
	lk_shadow (cv, x, y, w, h, D (16), D (10) / 2 + 1, 100, D (4));
	lk_fill (cv, x, y, w, h, D (16), 0xFBFBFC, 0xEEEFF3);
	lk_ring (cv, x, y, w, h, D (16), 16, 0x000000, 34);
}
// A header: the category's dot, its name, a word ("14 apps").
static void header (Canvas &cv, int x, int y, const char *title, const char *sub, unsigned dot, bool hasDot)
{
	int tx = x;
	if (hasDot) { lk_fill (cv, x, y + D (5), D (9), D (9), D10 (45), dot, dot); tx = x + D (16); }
	{ UkFaceScope f (F (14)); uk_text (cv, tx, y, title, INK, 2); tx += tw (title, 2) + D (10); }
	UkFaceScope f (F (12)); uk_text (cv, tx, y + D (2), sub, INK2);
}
static void small_caps (Canvas &cv, int x, int y, const char *s) { UkFaceScope f (F (10)); uk_text (cv, x, y, s, CAPS, 2); }

// ---- the search field and the Today line --------------------------------------------------------------------------------------------
static void draw_search (Canvas &cv, const Lay &L)
{
	int x = L.sx, y = L.sy, w = L.sw, h = L.sh;
	lk_shadow (cv, x, y, w, h, h / 2, D (6) / 2 + 1, 90, D (2));
	lk_fill (cv, x, y, w, h, h / 2, 0xFFFFFF, 0xF7F7F9);
	if (g_query[0]) lk_ring (cv, x - D (2), y - D (2), w + D (4), h + D (4), h / 2 + D (2), D10 (25) * 16 / 10 < 24 ? 24 : D10 (25) * 16 / 10, uk_tone (C_ACCENT, 160));
	else lk_ring (cv, x, y, w, h, h / 2, 16, 0x000000, 30);
	int ms = D (15);
	magnifier (cv, x + D (14), y + (h - ms) / 2, ms, g_query[0] ? C_ACCENT : 0x787C84);
	UkFaceScope f (F (13));
	int tx = x + D (38);
	if (g_query[0])
	{
		char b[80];
		int n = uk_text_fit (g_query, w - D (80), b, sizeof b);
		text_v (cv, tx, y, h, b, INK);
		cv.fillRect (tx + n + D (2), y + D (9), D10 (15) > 1 ? D10 (15) : 2, h - D (18), C_ACCENT);	// (the caret)
		int cs = D (18), cx = x + w - D (30), cy = y + (h - cs) / 2;
		lk_fill (cv, cx, cy, cs, cs, cs / 2, 0xC4C6CC, 0xC4C6CC);
		uk_glyph (cv, WKG_CLOSE, cx + cs / 2, cy + cs / 2, D (8), 0xFFFFFF);
		g_homeHits.add (cx - D (4), y, cs + D (8), h, H_CLEARQ, 0);
		g_homeHits.add (x, y, w - D (36), h, H_FIELD, 0);
	}
	else
	{
		char b[80];
		uk_text_fit (TR ("Search apps, files and settings"), w - D (50), b, sizeof b);
		text_v (cv, tx, y, h, b, 0x92949C);
		g_homeHits.add (x, y, w, h, H_FIELD, 0);
	}
}

static void draw_today_line (Canvas &cv, const Lay &L)
{
	int i = agenda_next ();
	if (!L.today || i < 0) return;
	const AgEv &v = g_ag[i];
	int x = L.tx, y = L.ty, w = L.tw, h = L.th;
	lk_fill (cv, x, y, w, h, h / 2, 0xFFFFFF, 0xFFFFFF, 26);
	lk_ring (cv, x, y, w, h, h / 2, 16, 0xFFFFFF, 60);
	int is = D (22);
	icon_of (cv, x + D (9), y + (h - is) / 2, "calendar", "C", is);
	char d[32]; short_date (d, sizeof d, v.date);
	char when[32]; agenda_when (v, when, sizeof when);
	int tx = x + D (40), right = x + w - D (14);
	UkFaceScope f (F (12));
	int ww = 0;
	if (when[0]) { UkFaceScope g (F (11)); ww = tw (when); text_v (cv, right - ww, y, h, when, 0xAABED8); ww += D (12); }
	text_v (cv, tx, y, h, d, 0xFFFFFF, 2);
	tx += tw (d, 2) + D (12);
	lk_fill (cv, tx - D (6), y + D (9), D (1) > 0 ? D (1) : 1, h - D (18), 0, 0xFFFFFF, 0xFFFFFF, 80);
	char what[140]; int k = 0; what[0] = 0;
	if (v.hm >= 0) { char t[6]; hm_text (t, v.hm); lx_cat (what, sizeof what, &k, t); lx_cat (what, sizeof what, &k, "  "); }
	lx_cat (what, sizeof what, &k, v.what);
	char b[140];
	uk_text_fit (what, right - ww - tx, b, sizeof b);
	text_v (cv, tx, y, h, b, 0xDCE6F4);
	g_homeHits.add (x, y, w, h, H_TODAY, 0);
}

// ---- the chips -------------------------------------------------------------------------------------------------------------------
// One chip at x (measure: only its width) -> its width.
static int chip (Canvas &cv, int x, int y, int h, const char *name, unsigned dot, bool hasDot, bool on, int count, bool measure)
{
	UkFaceScope f (F (12));
	char cnt[12] = ""; int k = 0;
	if (count >= 0) num_cat (cnt, sizeof cnt, &k, count);
	int w = tw (name, on ? 2 : 0) + D (hasDot ? 34 : 24) + (count >= 0 ? tw ("  ") + tw (cnt) : 0);
	if (measure) return w;
	if (on)
	{
		lk_shadow (cv, x, y, w, h, h / 2, D (4) / 2 + 1, 80, D10 (15));
		lk_fill (cv, x, y, w, h, h / 2, 0xFFFFFF, 0xEEF0F4);
	}
	else { lk_fill (cv, x, y, w, h, h / 2, 0xFFFFFF, 0xFFFFFF, 22); lk_ring (cv, x, y, w, h, h / 2, 16, 0xFFFFFF, 46); }
	int tx = x + D (12);
	if (hasDot) { lk_fill (cv, x + D (12), y + (h - D (8)) / 2, D (8), D (8), D (4), on ? dot : uk_tone (dot, 150), on ? dot : uk_tone (dot, 150)); tx = x + D (26); }
	text_v (cv, tx, y, h, name, on ? INK : 0xECF2FA, on ? 2 : 0);
	if (count >= 0) text_v (cv, tx + tw (name, on ? 2 : 0) + tw ("  "), y, h, cnt, on ? INK2 : 0xAABED8);
	return w;
}

static void draw_chips (Canvas &cv, const Lay &L)
{
	int x = L.cx, y = L.cy, h = L.ch, right = L.cx + L.cw - D (40);
	if (g_tab < g_tabFirst) g_tabFirst = g_tab;
	for (;;)							// (the chip chosen kept in view)
	{
		int xx = x;
		for (int i = g_tabFirst; i <= g_tab; i++) xx += chip (cv, 0, 0, h, TR (g_cats[i].name), 0, true, i == g_tab, -1, true) + D (6);
		if (xx <= right + D (6) || g_tabFirst >= g_tab) break;
		g_tabFirst++;
	}
	bool more = false;
	for (int i = g_tabFirst; i < g_ncats; i++)
	{
		const char *n = TR (g_cats[i].name);
		int w = chip (cv, 0, 0, h, n, 0, true, i == g_tab, -1, true);
		if (x + w > right) { more = true; break; }
		chip (cv, x, y, h, n, g_cats[i].dot, true, i == g_tab, -1, false);
		g_homeHits.add (x, y, w, h, H_TAB, i);
		x += w + D (6);
	}
	if (more || g_tabFirst > 0)					// the chevron: the next chips (or back to the first)
	{
		int bw = D (30), bx = L.cx + L.cw - bw;
		lk_fill (cv, bx, y, bw, h, h / 2, 0xFFFFFF, 0xFFFFFF, 30);
		lk_ring (cv, bx, y, bw, h, h / 2, 16, 0xFFFFFF, 50);
		uk_glyph (cv, more ? WKG_CHEV_RIGHT : WKG_CHEV_LEFT, bx + bw / 2, y + h / 2, D (10), 0xECF2FA);
		g_homeHits.add (bx, y, bw, h, H_CHEV, more ? 1 : -1);
	}
}

// ---- the card --------------------------------------------------------------------------------------------------------------------
static void draw_doc (Canvas &cv, int x, int y, int w, int h, const struct recent_doc &d, bool focus)
{
	if (focus) lk_shadow (cv, x - D (2), y - D (2), w + D (4), h + D (4), D (14), D (6) / 2 + 1, 120, 0, uk_tone (C_ACCENT, 140));
	lk_fill (cv, x, y, w, h, D (12), 0xFFFFFF, 0xFFFFFF);
	if (focus) lk_ring (cv, x - D (2), y - D (2), w + D (4), h + D (4), D (14), 24, C_ACCENT);
	else lk_ring (cv, x, y, w, h, D (12), 16, 0x000000, 26);
	char app[32] = "";
	const char *base = fs_basename (d.path);
	App *a = fa_app_for (d.path, app, sizeof app) ? find_app (app) : 0;
	int is = D (36);
	icon_of (cv, x + D (10), y + (h - is) / 2, app[0] ? app : "fileviewer", base, is);
	char b[160];
	{ UkFaceScope f (F (12)); uk_text_fit (base, w - D (64), b, sizeof b, 2); uk_text (cv, x + D (56), y + D (10), b, INK, 2); }
	char dir[96], when[32], sub[200]; int k = 0;
	dir_of (d.path, dir, sizeof dir); doc_when (d, when, sizeof when);
	lx_cat (sub, sizeof sub, &k, a ? a->label : app[0] ? app : TR ("File")); lx_cat (sub, sizeof sub, &k, "  -  ");
	lx_cat (sub, sizeof sub, &k, dir);
	if (when[0]) { lx_cat (sub, sizeof sub, &k, "  -  "); lx_cat (sub, sizeof sub, &k, when); }
	UkFaceScope f (F (11)); uk_text_fit (sub, w - D (64), b, sizeof b); uk_text (cv, x + D (56), y + D (29), b, INK2);
}

static void draw_card (Canvas &cv, const Lay &L)
{
	panel_card (cv, L.px, L.py, L.pw, L.ph);
	tab_items ();
	Card c = card (L);
	if (g_focus >= c.total) g_focus = c.total > 0 ? c.total - 1 : 0;
	bool recent = tab_is_recent ();
	const Cat &ct = g_cats[g_tab];
	char sub[48]; int k = 0;
	if (recent) lx_cat (sub, sizeof sub, &k, TR ("opened last"));
	else { num_cat (sub, sizeof sub, &k, g_nitems); lx_cat (sub, sizeof sub, &k, " "); lx_cat (sub, sizeof sub, &k, g_nitems == 1 ? TR ("app") : TR ("apps")); }
	header (cv, L.px + D (18), L.py + D (14), TR (ct.name), sub, ct.dot, true);
	if (g_nitems == 0)
	{
		UkFaceScope f (F (12));
		text_cfit (cv, L.px, c.gy + D (30), L.pw, recent ? TR ("The apps you open come here.") : TR ("No apps here."), INK2);
	}
	// the tiles: the focused one's row kept in view
	bool tileFocus = g_stripFocus < 0 && g_focus < c.ntiles;
	if (!c.docs)
	{
		int frow = g_focus / c.cols;
		if (frow < g_gridTop) g_gridTop = frow;
		if (frow >= g_gridTop + c.rows) g_gridTop = frow - c.rows + 1;
	}
	else g_gridTop = 0;
	for (int i = g_gridTop * c.cols; i < c.ntiles && i < (g_gridTop + c.rows) * c.cols; i++)
	{
		int r = i / c.cols - g_gridTop, col = i % c.cols;
		int x = c.gx + col * c.cellw, y = c.gy + r * c.rowh;
		App &a = g_apps[g_items[i]];
		plate (cv, x, y, c.cellw, a, a.applet ? TR (a.label) : a.label, tileFocus && i == g_focus, !a.applet && task_of (a.name) >= 0);
		g_homeHits.add (x, y - D (4), c.cellw, c.rowh, H_TILE, i);
	}
	int totalRows = (c.ntiles + c.cols - 1) / c.cols;
	if (!c.docs && totalRows > c.rows)				// (more rows: a thin bar at the right)
	{
		int gh = c.rows * c.rowh, th = gh * c.rows / totalRows, ty = c.gy + (gh - th) * g_gridTop / (totalRows - c.rows);
		lk_fill (cv, L.px + L.pw - D (8), ty, D (4), th, D (2), 0xA8AEB8, 0xA8AEB8);
	}
	if (!c.docs) return;
	int sy = c.gy + c.rowh;
	cv.fillRect (L.px + D (18), sy, L.pw - D (36), 1, 0xDCDDE2);
	header (cv, L.px + D (18), sy + D (14), TR ("Documents"), TR ("the files opened last, with the app that opens them"), 0, false);
	for (int i = 0; i < c.ndocs; i++)
	{
		int r = i / c.dcols, col = i % c.dcols;
		int x = c.dx + col * (c.dw + c.dgap), y = c.dy + r * (c.dh + c.dgap);
		draw_doc (cv, x, y, c.dw, c.dh, g_docs[i], g_stripFocus < 0 && g_focus == c.ntiles + i);
		g_homeHits.add (x, y, c.dw, c.dh, H_DOC, i);
	}
}

// ---- the Today column (a logical width of 1100 and more) ---------------------------------------------------------------------------
static void draw_today_column (Canvas &cv, const Lay &L)
{
	int x = L.colx, y = L.py, w = L.colw, h = L.ph;
	panel_card (cv, x, y, w, h);
	int px = x + D (18), iw = w - D (36), yy = y + D (16);
	int yr = 2026, mo = 1, dd = 1;
	kapi_get_datetime (&yr, &mo, &dd, 0, 0, 0);
	{ UkFaceScope f (F (13)); uk_text (cv, px, yy, TR (WD[weekday (yr, mo, dd)]), INK2); }
	{
		char d[40]; int k = 0;
		num_cat (d, sizeof d, &k, dd); lx_cat (d, sizeof d, &k, " "); lx_cat (d, sizeof d, &k, TR (MO[(mo + 11) % 12]));
		UkFaceScope f (F (22)); char b[60]; uk_text_fit (d, iw - D (44), b, sizeof b, 2); uk_text (cv, px, yy + D (18), b, INK, 2);
	}
	icon_of (cv, x + w - D (50), yy + D (4), "calendar", "C", D (32));
	g_homeHits.add (x, y, w, D (74), H_AGENDA, 0);
	yy += D (58); cv.fillRect (px, yy, iw, 1, 0xE0E1E5); yy += D (12);
	small_caps (cv, px, yy, TR ("AGENDA")); yy += D (20);
	int now, today = today_ymd (&now), next = agenda_next ();
	int nrows = 0, maxRows = (h - D (190)) / 2 / D (44);		// (half of what is left: the agenda, the rest the notifications)
	if (maxRows < 1) maxRows = 1;
	if (maxRows > 5) maxRows = 5;
	int first = 0;
	while (first < g_nag && g_ag[first].date == today && g_ag[first].hm >= 0 && g_ag[first].hm < now && first < next && g_nag - first > maxRows) first++;
	if (g_nag == 0) { UkFaceScope f (F (12)); uk_text (cv, px, yy, TR ("Nothing planned."), INK2); yy += D (30); }
	for (int i = first; i < g_nag && nrows < maxRows; i++, nrows++)
	{
		const AgEv &v = g_ag[i];
		bool past = v.date == today && v.hm >= 0 && v.hm < now, isNext = i == next;
		unsigned bar = isNext ? C_ACCENT : past ? 0xBEC0C6 : cat_dot ("Productivity");
		lk_fill (cv, px, yy + D (2), D (4), D (32), D (2), bar, bar);
		unsigned c1 = past ? 0x96989E : INK;
		char t[24];
		if (v.date != today) short_date (t, sizeof t, v.date);
		else if (v.hm >= 0) hm_text (t, v.hm);
		else fs_copy (t, TR ("today"), sizeof t);
		int tcol;
		{ UkFaceScope f (F (12)); tcol = v.date != today ? tw (t, 2) + D (12) : D (48); if (tcol < D (48)) tcol = D (48); uk_text (cv, px + D (14), yy, t, c1, 2); }
		char b[120];
		{ UkFaceScope f (F (12)); uk_text_fit (v.what, iw - D (14) - tcol, b, sizeof b, isNext ? 2 : 0); uk_text (cv, px + D (14) + tcol, yy, b, c1, isNext ? 2 : 0); }
		if (isNext) { char wh[32]; agenda_when (v, wh, sizeof wh); UkFaceScope f (F (11)); uk_text (cv, px + D (14) + tcol, yy + D (17), wh, C_ACCENT); }
		g_homeHits.add (px, yy, iw, D (40), H_AGENDA, i);
		yy += D (44);
	}
	cv.fillRect (px, yy, iw, 1, 0xE0E1E5); yy += D (12);
	small_caps (cv, px, yy, TR ("NOTIFICATIONS")); yy += D (20);
	if (g_nnotes == 0) { UkFaceScope f (F (12)); uk_text (cv, px, yy, TR ("No notifications."), INK2); }
	for (int i = 0; i < g_nnotes && yy + D (50) <= y + h - D (10); i++)
	{
		Note &n = g_notes[i];
		lk_fill (cv, px, yy, iw, D (50), D (10), 0xFFFFFF, 0xFFFFFF);
		lk_ring (cv, px, yy, iw, D (50), D (10), 16, 0x000000, 24);
		icon_of (cv, px + D (8), yy + D (9), note_app (n), n.title, D (32));
		char b[200];
		{ UkFaceScope f (F (10)); uk_text (cv, px + iw - D (10) - tw (n.hm), yy + D (7), n.hm, INK2); }
		{ UkFaceScope f (F (12)); uk_text_fit (n.title, iw - D (100), b, sizeof b, 2); uk_text (cv, px + D (48), yy + D (8), b, INK, 2); }
		{ UkFaceScope f (F (11)); uk_text_fit (n.text, iw - D (58), b, sizeof b); uk_text (cv, px + D (48), yy + D (26), b, INK2); }
		g_homeHits.add (px, yy, iw, D (50), H_HNOTE, i);
		yy += D (58);
	}
}

// ---- the Running strip: the open apps as thumbnails ----------------------------------------------------------------------------------
struct RunThumb { unsigned id; int w, h; unsigned *px; unsigned at; };
static RunThumb g_run[UK_TASKS_MAX];
static unsigned g_runSig, g_runAt;
static void run_thumbs (int cw, int ch)			// the pictures taken again: the tasks changed, a second passed, or a new size
{
	unsigned sig = (unsigned) g_ntasks * 977u + (unsigned) cw * 31u + (unsigned) ch;
	for (int i = 0; i < g_ntasks; i++) sig = sig * 131u + g_tasks[i].id;
	unsigned now = kapi_get_ticks ();
	if (sig == g_runSig && now - g_runAt < 100) return;
	g_runSig = sig; g_runAt = now;
	for (int i = 0; i < g_ntasks && i < UK_TASKS_MAX; i++)
	{
		RunThumb &t = g_run[i];
		int tw_ = g_tasks[i].w, th_ = g_tasks[i].h;
		int h = tw_ > 0 && th_ > 0 ? cw * th_ / tw_ : ch;		// (the window's shape: its top shown)
		if (h < ch) h = ch;
		if (h > 512) h = 512;
		int w = cw > 512 ? 512 : cw;
		if (t.px == 0 || t.w * t.h < w * h) { delete [] t.px; t.px = new unsigned[w * h]; }
		t.id = g_tasks[i].id; t.w = w; t.h = h;
		if (uk_shell_thumb (g_tasks[i].id, t.px, w, h) != 1)
			for (int k = 0; k < w * h; k++) t.px[k] = 0x3A4250;
	}
}

static void draw_strip (Canvas &cv, const Lay &L)
{
	if (!L.strip) return;
	int x = L.M, y = L.ry, w = g_aw - 2 * L.M;
	int lw;
	{
		UkFaceScope f (F (12));
		text_v (cv, x + D (2), y, D (18), TR ("Running"), 0xF0F4FA, 2);
		lw = tw (TR ("Running"), 2);
	}
	int nx = x + D (2) + lw + D (10);
	{
		char n[8]; int k = 0; num_cat (n, sizeof n, &k, g_ntasks);
		lk_fill (cv, nx, y + D (2), D (20), D (14), D (7), 0xFFFFFF, 0xFFFFFF, 40);
		UkFaceScope f (F (10)); text_v (cv, nx + (D (20) - tw (n, 2)) / 2, y + D (2) - D10 (5), D (14), n, 0xF0F4FA, 2);
	}
	// the key hints at the line's right end -- the first ones left out when they do not fit
	static const char *const H[] = { TRN ("Tab"), TRN ("category"), "\xE2\x86\x90 \xE2\x86\x91 \xE2\x86\x93 \xE2\x86\x92", TRN ("choose"),
					 TRN ("Enter"), TRN ("open"), TRN ("Alt+Tab"), TRN ("switch") };
	static const char *const HS[] = { "\xE2\x86\x90 \xE2\x86\x92", TRN ("choose"), TRN ("Enter"), TRN ("bring it"), TRN ("Del"), TRN ("close"), TRN ("Up"), TRN ("back") };
	const char *const *hs = g_stripFocus >= 0 ? HS : H;
	for (int skip = 0; skip < 8; skip += 2)
	{
		int hw = hints2 (cv, 0, 0, hs + skip, 8 - skip, 0, true);
		if (nx + D (40) + hw <= x + w + D (14)) { hints2 (cv, x + w - hw + D (14), y, hs + skip, 8 - skip, 0xD2DEEE); break; }
	}
	int cw = D (134), ch = D (62), cy = y + D (24), cx = x;
	if (g_ntasks == 0) { UkFaceScope f (F (11)); text_v (cv, x + D (2), cy, D (30), TR ("No open apps."), 0xB8C0CC); return; }
	run_thumbs (cw, ch);
	if (g_stripFocus >= g_ntasks) g_stripFocus = g_ntasks - 1;
	for (int i = 0; i < g_ntasks; i++)
	{
		if (cx + cw > x + w) break;
		const uk_shell_task &t = g_tasks[i];
		bool on = i == g_stripFocus;
		if (on) lk_shadow (cv, cx - D (3), cy - D (3), cw + D (6), ch + D (6), D (12), D (6) / 2 + 1, 170, 0, uk_tone (C_ACCENT, 150));
		else lk_shadow (cv, cx, cy, cw, ch, D (9), D (4) / 2 + 1, 110, D (2));
		lk_picture (cv, cx, cy, cw, ch, D (9), g_run[i].px, g_run[i].w, ch);
		int fh = D (24);
		// the dark foot (its bottom corners the card's)
		{
			LkBox b = lk_box (cx, cy, cw, ch, D (9));
			for (int j = ch - fh; j < ch; j++)
				for (int k = 0; k < cw; k++)
				{
					int a = j >= ch - D (9) - 1 && (k <= D (9) || k >= cw - D (9) - 1) ? lk_cov (lk_sd (b, cx + k, cy + j)) : 255;
					lk_px (cv, cx + k, cy + j, 0x101828, a * 200 / 255);
				}
		}
		const char *l = task_label (t);
		icon_of (cv, cx + D (6), cy + ch - fh + D (3), t.name, l, D (18));
		{ UkFaceScope f (F (11)); char b[64]; uk_text_fit (l, cw - D (34) - D (20), b, sizeof b, 2); text_v (cv, cx + D (28), cy + ch - fh, fh, b, 0xFFFFFF, 2); }
		lk_ring (cv, cx, cy, cw, ch, D (9), 16, 0xFFFFFF, 110);
		if (on) lk_ring (cv, cx - D (3), cy - D (3), cw + D (6), ch + D (6), D (12), 32, C_ACCENT);
		// its close button: a dark bead with a cross, at the foot's right
		int d = D (16), bx = cx + cw - d - D (5), by = cy + ch - fh + (fh - d) / 2;
		lk_fill (cv, bx, by, d, d, d / 2, 0xFFFFFF, 0xFFFFFF, 40);
		uk_glyph (cv, WKG_CLOSE, bx + d / 2, by + d / 2, D (7), 0xFFFFFF);
		g_homeHits.add (cx, cy, cw - d - D (8), ch, H_RUNCARD, i);
		g_homeHits.add (bx - D (4), cy + ch - fh, d + D (9), fh, H_RUNX, i);
		cx += cw + D (10);
	}
}

// ---- the search's results ------------------------------------------------------------------------------------------------------------------
static bool g_resVis[MAXRESULTS];			// (drawn last time: the keys choose among those)

// A result as a small card: its icon, its title (the match in Aqua), a line under it.
static void result_icon (Canvas &cv, int x, int y, int s, const Result &r)
{
	if (r.kind == R_APP || r.kind == R_SETTING) draw_icon (cv, x, y, g_apps[r.app], s);
	else if (r.kind == R_RUN) icon_of (cv, x, y, "terminal", "T", s);
	else { char app[32]; icon_of (cv, x, y, fa_app_for (r.path, app, sizeof app) ? app : "fileviewer", r.title, s); }
}
static void result_card (Canvas &cv, int x, int y, int w, int h, int i, int is)
{
	const Result &r = g_res[i];
	bool on = i == g_resSel;
	if (on) lk_shadow (cv, x - D (2), y - D (2), w + D (4), h + D (4), D (12), D (5) / 2 + 1, 120, 0, uk_tone (C_ACCENT, 140));
	lk_fill (cv, x, y, w, h, D (10), 0xFFFFFF, 0xFFFFFF);
	if (on) lk_ring (cv, x - D (2), y - D (2), w + D (4), h + D (4), D (12), 24, C_ACCENT);
	else lk_ring (cv, x, y, w, h, D (10), 16, 0x000000, 24);
	result_icon (cv, x + D (8), y + (h - is) / 2, is, r);
	int tx = x + is + D (16), ww = w - is - D (24);
	char b[160];
	{ UkFaceScope f (F (12)); uk_text_fit (r.title, ww, b, sizeof b, 2); lk_text_hl (cv, tx, y + (h - D (36)) / 2, b, g_query, INK, C_ACCENT, 2); }
	{ UkFaceScope f (F (10)); lk_excerpt (r.sub, g_query, ww, b, sizeof b); lk_text_hl (cv, tx, y + (h - D (36)) / 2 + D (19), b, g_query, INK2, C_ACCENT); }
	g_resVis[i] = true;
	g_homeHits.add (x, y, w, h, H_RESULT, i);
}

// The best match: a tall card (wide screens) -- its plate, its name, what it is, a few words, Open (Enter).
static void best_card (Canvas &cv, int x, int y, int w, int h)
{
	const Result &r = g_res[0];
	bool on = g_resSel == 0;
	lk_shadow (cv, x, y, w, h, D (14), D (6) / 2 + 1, on ? 120 : 60, 0, on ? uk_tone (C_ACCENT, 140) : 0x060E1C);
	lk_fill (cv, x, y, w, h, D (14), 0xFFFFFF, 0xF6F8FC);
	if (on) lk_ring (cv, x, y, w, h, D (14), D10 (25) * 16 / 10 < 24 ? 24 : D10 (25) * 16 / 10, C_ACCENT);
	else lk_ring (cv, x, y, w, h, D (14), 16, 0x000000, 26);
	int ps = D (72), bx = x + (w - ps) / 2, by = y + D (18);
	lk_shadow (cv, bx, by, ps, ps, D (18), D (3) / 2 + 1, 60, D (2));
	lk_fill (cv, bx, by, ps, ps, D (18), 0xFFFFFF, 0xEAECF1);
	lk_ring (cv, bx, by, ps, ps, D (18), 16, 0x000000, 26);
	result_icon (cv, bx + (ps - D (54)) / 2, by + (ps - D (54)) / 2, D (54), r);
	char b[160];
	{
		UkFaceScope f (F (16));
		int n = uk_text_fit (r.title, w - D (20), b, sizeof b, 2);
		lk_text_hl (cv, x + (w - n) / 2, by + ps + D (12), b, g_query, INK, C_ACCENT, 2);
	}
	// what it is ("Settings  -  13 applets", "Productivity", "Control Panel", the folder)
	char what[96]; int k = 0; what[0] = 0;
	char words[260]; int kw = 0; words[0] = 0;
	if (r.kind == R_APP)
	{
		App &a = g_apps[r.app];
		lx_cat (what, sizeof what, &k, TR (a.cat));
		if (ieq (a.name, "control"))
		{
			int n = 0;
			for (int i = 0; i < g_napps; i++) if (g_apps[i].applet)
			{
				if (n) lx_cat (words, sizeof words, &kw, ", ");
				lx_cat (words, sizeof words, &kw, TR (g_apps[i].label)); n++;
			}
			lx_cat (what, sizeof what, &k, "  -  "); num_cat (what, sizeof what, &k, n); lx_cat (what, sizeof what, &k, " "); lx_cat (what, sizeof what, &k, TR ("applets"));
		}
		else if (task_of (a.name) >= 0) { lx_cat (what, sizeof what, &k, "  -  "); lx_cat (what, sizeof what, &k, TR ("running")); }
	}
	else if (r.kind == R_SETTING) { lx_cat (what, sizeof what, &k, TR ("Control Panel")); lx_cat (words, sizeof words, &kw, r.sub); }
	else lx_cat (what, sizeof what, &k, r.sub);
	{ UkFaceScope f (F (11)); text_cfit (cv, x, by + ps + D (36), w, what, INK2); }
	if (words[0])
	{
		UkFaceScope f (F (11));
		int st[5], ln[5];
		int lines = (y + h - D (60) - (by + ps + D (58))) / D (16);
		if (lines > 5) lines = 5;
		int nl = lines > 0 ? uk_text_wrap (words, (int) strlen (words), w - D (32), lines, st, ln) : 0;
		for (int i = 0; i < nl; i++)
		{
			char l[200]; int m = ln[i] < 199 ? ln[i] : 199;
			memcpy (l, words + st[i], (size_t) m); l[m] = 0;
			text_cfit (cv, x + D (16), by + ps + D (58) + i * D (16), w - D (32), l, 0x5A5C64);
		}
	}
	int bw = w - D (32), bh = D (30), bx2 = x + D (16), by2 = y + h - bh - D (14);
	lk_fill (cv, bx2, by2, bw, bh, bh / 2, uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 118));
	int cw = cap2 (cv, 0, 0, TR ("Enter"), true);
	{ UkFaceScope f (F (12)); int ow = tw (TR ("Open"), 2); text_v (cv, bx2 + (bw - ow - cw - D (10)) / 2, by2, bh, TR ("Open"), 0xFFFFFF, 2);
	  cap2 (cv, bx2 + (bw - ow - cw - D (10)) / 2 + ow + D (10), by2 + (bh - D (18)) / 2, TR ("Enter")); }
	g_resVis[0] = true;
	g_homeHits.add (x, y, w, h, H_RESULT, 0);
}

static int g_kindCount[4];
static void draw_results (Canvas &cv, const Lay &L)
{
	for (int i = 0; i < MAXRESULTS; i++) g_resVis[i] = false;
	// the kinds as chips, with their counts
	static const char *const K[] = { TRN ("All"), TRN ("Apps"), TRN ("Settings"), TRN ("Files") };
	int x = L.cx;
	for (int i = 0; i < 4; i++)
	{
		if (i > 0 && g_kindCount[i] == 0) continue;
		int w = chip (cv, 0, 0, L.ch, TR (K[i]), 0, false, g_kind == i, g_kindCount[i], true);
		if (x + w > L.cx + L.cw) break;
		chip (cv, x, L.cy, L.ch, TR (K[i]), 0, false, g_kind == i, g_kindCount[i], false);
		g_homeHits.add (x, L.cy, w, L.ch, H_KIND, i);
		x += w + D (6);
	}
	int px = L.px, py = L.py, pw = L.pw, ph = g_ah - py - D (12);
	panel_card (cv, px, py, pw, ph);
	if (g_resSel >= g_nres) g_resSel = g_nres - 1;
	if (g_resSel < 0) g_resSel = 0;
	// the bottom line: run the text in a Terminal, the keys
	int fy = py + ph - D (34);
	cv.fillRect (px + D (14), fy, pw - D (28), 1, 0xDCDDE2);
	int run = g_nres - 1;					// (the last result: R_RUN)
	if (run >= 0 && g_res[run].kind == R_RUN)
	{
		bool on = g_resSel == run;
		char t[140]; const char *tpl = TR ("Run \"%s\" in a Terminal"); int k = 0;
		for (const char *p = tpl; *p; p++)
			if (p[0] == '%' && p[1] == 's') { lx_cat (t, sizeof t, &k, g_query); p++; }
			else { char c[2] = { *p, 0 }; lx_cat (t, sizeof t, &k, c); }
		UkFaceScope f (F (11));
		int ww = tw (t) + D (36);
		if (on) lk_fill (cv, px + D (12), fy + D (6), ww + D (8), D (22), D (11), uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 118));
		icon_of (cv, px + D (20), fy + D (10), "terminal", "T", D (14));
		text_v (cv, px + D (42), fy + D (6), D (22), t, on ? 0xFFFFFF : INK2);
		g_resVis[run] = true;
		g_homeHits.add (px + D (12), fy + D (4), ww + D (8), D (26), H_RESULT, run);
	}
	if (!L.narrow)
	{
		static const char *const H[] = { "\xE2\x86\x91 \xE2\x86\x93", TRN ("choose"), TRN ("Tab"), TRN ("next group"), TRN ("Enter"), TRN ("open"), TRN ("Esc"), TRN ("clear") };
		int hw = hints2 (cv, 0, 0, H, 8, 0, true);
		hints2 (cv, px + pw - D (14) - hw + D (14), fy + D (8), H, 8, INK2);
	}
	int top = py + D (14), bottom = fy - D (8);
	int nres = run >= 0 && g_res[run].kind == R_RUN ? run : g_nres;		// (the results but the command)
	if (nres == 0)
	{
		UkFaceScope f (F (12));
		text_cfit (cv, px, top + D (30), pw, TR ("Nothing found: Enter runs it as a command."), INK2);
		return;
	}
	if (!L.narrow)							// wide: the best match at the left, the others grouped at the right
	{
		int hx = px + D (14), hw = D (216);
		small_caps (cv, hx + D (4), top, TR ("BEST MATCH"));
		best_card (cv, hx, top + D (20), hw, bottom - top - D (20));
		int rx = hx + hw + D (18), rw = px + pw - D (14) - rx;
		if (g_kind == 0)
		{
			int yy = top, i = 1;
			// the apps: a row
			int na = 0; while (i + na < nres && g_res[i + na].kind == R_APP) na++;
			if (na)
			{
				small_caps (cv, rx + D (4), yy, TR ("APPS")); yy += D (20);
				int per = rw / D (150); if (per < 1) per = 1; if (per > 4) per = 4;
				int aw = (rw - D (8) * (per - 1)) / per;
				int arows = na > per && bottom - top > D (330) ? 2 : 1;		// (a tall card: two rows of apps)
				for (int k = 0; k < na && k < per * arows; k++) result_card (cv, rx + (k % per) * (aw + D (8)), yy + (k / per) * D (54), aw, D (46), i + k, D (32));
				yy += (na > per ? arows : 1) * D (54) - D (8) + D (16);
				i += na;
			}
			// the settings and the files side by side (or one under the other when one kind is missing)
			int ns = 0; while (i + ns < nres && g_res[i + ns].kind == R_SETTING) ns++;
			int nf = 0; while (i + ns + nf < nres && g_res[i + ns + nf].kind == R_FILE) nf++;
			int cols = (ns > 0) + (nf > 0), colw = cols ? (rw - D (16) * (cols - 1)) / cols : rw;
			int gx = rx;
			for (int g = 0; g < 2; g++)
			{
				int n = g == 0 ? ns : nf, first = g == 0 ? i : i + ns;
				if (n == 0) continue;
				int y2 = yy;
				small_caps (cv, gx + D (4), y2, g == 0 ? TR ("SETTINGS") : TR ("FILES")); y2 += D (20);
				for (int k = 0; k < n && y2 + D (44) <= bottom; k++, y2 += D (50)) result_card (cv, gx, y2, colw, D (44), first + k, D (28));
				gx += colw + D (16);
			}
		}
		else								// one kind: its results as a grid of cards
		{
			int per = rw / D (220); if (per < 1) per = 1;
			int aw = (rw - D (8) * (per - 1)) / per, yy = top + D (20);
			small_caps (cv, rx + D (4), top, TR (K[g_kind]));
			for (int i = 1, k = 0; i < nres; i++, k++)
			{
				int y2 = yy + (k / per) * D (52);
				if (y2 + D (46) > bottom) break;
				result_card (cv, rx + (k % per) * (aw + D (8)), y2, aw, D (46), i, D (32));
			}
		}
		return;
	}
	// narrow: one column -- the best match, then the groups, the chosen one kept in view
	int Y[MAXRESULTS], H[MAXRESULTS], yy = 0, head = D (22);
	for (int i = 0; i < nres; i++)
	{
		if (i == 0 || i == 1 || g_res[i].kind != g_res[i - 1].kind) yy += head;
		Y[i] = yy; H[i] = i == 0 ? D (64) : D (44);
		yy += H[i] + D (6);
	}
	int avail = bottom - top;
	int sel = g_resSel < nres ? g_resSel : nres - 1;
	if (g_resTop > sel) g_resTop = sel;
	while (g_resTop < sel && Y[sel] + H[sel] - (Y[g_resTop] - head) > avail) g_resTop++;
	int base = Y[g_resTop] - head;
	static const char *const G[] = { TRN ("APPS"), TRN ("SETTINGS"), TRN ("FILES"), TRN ("RUN") };
	for (int i = g_resTop; i < nres; i++)
	{
		int ry = top + Y[i] - base;
		if (ry + H[i] > bottom) break;
		if (i == g_resTop || i == 1 || g_res[i].kind != g_res[i - 1].kind)
			small_caps (cv, px + D (18), ry - head + D (4), i == 0 ? TR ("BEST MATCH") : TR (G[g_res[i].kind]));
		result_card (cv, px + D (14), ry, pw - D (28), H[i], i, i == 0 ? D (44) : D (28));
	}
}

// ---- the home drawn ------------------------------------------------------------------------------------------------------------------------
static void draw_home (void)
{
	Canvas &cv = g_hc;
	g_homeHits.clear ();
	for (int y = 0; y < g_ah; y++)					// the wallpaper: Milk's navy, a gradient
		cv.fillRect (0, y, g_aw, 1, uk_mix (0x223A55, 0x16263C, y * 256 / (g_ah > 1 ? g_ah : 1)));
	Lay L = lay ();
	draw_search (cv, L);
	if (g_query[0]) draw_results (cv, L);
	else
	{
		draw_today_line (cv, L);
		draw_chips (cv, L);
		draw_card (cv, L);
		if (L.side) draw_today_column (cv, L);
		draw_strip (cv, L);
	}
	present (W_HOME);
	g_homeDirty = false;
}

// ---- the launcher's keys and clicks ----------------------------------------------------------------------------------------------------------
static void query_changed (void)
{
	g_resSel = 0; g_resTop = 0;
	search (g_query, g_kind, g_kindCount);
	g_homeDirty = true;
}

static void open_doc (int i)
{
	if (i < 0 || i >= g_ndocs) return;
	char p[160]; fs_copy (p, g_docs[i].path, sizeof p);
	if (!fa_open (p)) lx_launch ("fileviewer", p);
	docs_load ();
	g_homeDirty = true;
}

static void open_focused (void)
{
	if (g_query[0]) { if (g_resSel < g_nres) { Result r = g_res[g_resSel]; g_query[0] = 0; g_kind = 0; query_changed (); open_result (r); docs_load (); } return; }
	if (g_stripFocus >= 0) { if (g_stripFocus < g_ntasks) uk_shell_front (g_tasks[g_stripFocus].id, 1); g_stripFocus = -1; return; }
	tab_items ();
	Card c = card (lay ());
	if (g_focus < c.ntiles) open_app (g_apps[g_items[g_focus]].name);
	else open_doc (g_focus - c.ntiles);
}

static void set_tab (int t)
{
	if (g_ncats == 0) return;
	g_tab = (t + g_ncats) % g_ncats;
	g_focus = 0; g_gridTop = 0; g_stripFocus = -1;
	g_homeDirty = true;
}

// The card's arrows: the tiles' grid, then (Recent) the documents' -> false: nothing that way.
static bool card_move (int key)
{
	Lay L = lay ();
	tab_items ();
	Card c = card (L);
	int f = g_focus, n = c.total;
	if (n == 0) return false;
	if (f < c.ntiles)						// a tile
	{
		int col = f % c.cols;
		switch (key)
		{
		case KEY_LEFT:  if (f == 0) return false; f--; break;
		case KEY_RIGHT: if (f + 1 >= n) return false; f++; break;
		case KEY_UP:    if (f < c.cols) return false; f -= c.cols; break;
		case KEY_DOWN:
			if (f + c.cols < c.ntiles) f += c.cols;
			else if (c.ndocs > 0) { int dc = col * c.dcols / c.cols; f = c.ntiles + (dc < c.ndocs ? dc : c.ndocs - 1); }
			else if ((c.ntiles - 1) / c.cols > f / c.cols) f = c.ntiles - 1;	// (the last row, shorter)
			else return false;
			break;
		}
	}
	else								// a document
	{
		int d = f - c.ntiles, col = d % c.dcols;
		switch (key)
		{
		case KEY_LEFT:  f--; break;
		case KEY_RIGHT: if (f + 1 >= n) return false; f++; break;
		case KEY_UP:    if (d >= c.dcols) f -= c.dcols; else { int t = col * c.ntiles / c.dcols; f = t < c.ntiles ? t : c.ntiles - 1; if (f < 0) f = 0; } break;
		case KEY_DOWN:  if (d + c.dcols < c.ndocs) f += c.dcols; else return false; break;
		}
	}
	g_focus = f;
	g_homeDirty = true;
	return true;
}

static int next_group (int from, int dir)			// searching: Tab -> the first result of the next group
{
	if (g_nres <= 1) return from;
	int kind = from == 0 ? -1 : g_res[from].kind, i = from;
	for (int k = 0; k < g_nres; k++)
	{
		i = (i + dir + g_nres) % g_nres;
		int ki = i == 0 ? -1 : g_res[i].kind;
		if (ki != kind && g_resVis[i] && (dir > 0 || i == 0 || g_res[i - 1].kind != ki || i == 1)) return i;
	}
	return from;
}

static void home_key (unsigned long, int ev, long v)
{
	if (ev != GUI_EVENT_KEY) return;
	static bool said;					// (the Pi's kmsg: the keys reach the launcher)
	if (!said) { said = true; ax_puts ("pocketshell: the launcher gets the keys\n"); }
	int k = (int) v;
	unsigned mods = (unsigned) kapi_get_modifiers ();
	int ql = (int) strlen (g_query);
	if (ql)								// ---- searching
	{
		switch (k)
		{
		case KEY_ENTER: open_focused (); return;
		case 0x1b: g_query[0] = 0; g_kind = 0; query_changed (); return;
		case KEY_BACKSPACE:
			do ql--; while (ql > 0 && ((unsigned char) g_query[ql] & 0xC0) == 0x80);
			g_query[ql] = 0; if (!ql) g_kind = 0; query_changed (); return;
		case '\t': g_resSel = next_group (g_resSel, (mods & MOD_SHIFT) ? -1 : 1); g_homeDirty = true; return;
		case KEY_UP: case KEY_LEFT:
			for (int i = g_resSel - 1; i >= 0; i--) if (g_resVis[i]) { g_resSel = i; break; }
			g_homeDirty = true; return;
		case KEY_DOWN: case KEY_RIGHT:
			for (int i = g_resSel + 1; i < g_nres; i++) if (g_resVis[i] || i == g_nres - 1) { g_resSel = i; break; }
			g_homeDirty = true; return;
		}
	}
	else if (g_stripFocus >= 0)					// ---- the Running strip
	{
		switch (k)
		{
		case KEY_ENTER: case ' ': open_focused (); return;
		case KEY_LEFT: if (g_stripFocus > 0) g_stripFocus--; g_homeDirty = true; return;
		case KEY_RIGHT: if (g_stripFocus + 1 < g_ntasks) g_stripFocus++; g_homeDirty = true; return;
		case KEY_UP: case 0x1b: g_stripFocus = -1; g_homeDirty = true; return;
		case KEY_DEL: if (g_stripFocus < g_ntasks) uk_win_close (g_tasks[g_stripFocus].id); return;
		case '\t': g_stripFocus = -1; set_tab (g_tab + ((mods & MOD_SHIFT) ? -1 : 1)); return;
		case KEY_DOWN: return;
		}
	}
	else								// ---- the card
	{
		switch (k)
		{
		case KEY_ENTER: open_focused (); return;
		case 0x1b: if (g_ntasks > 0) uk_shell_front (g_tasks[0].id, 1); return;	// (Home again: back to the app)
		case KEY_BACKSPACE: return;
		case '\t': set_tab (g_tab + ((mods & MOD_SHIFT) ? -1 : 1)); return;
		case KEY_LEFT: if (!card_move (k)) set_tab (g_tab - 1); return;
		case KEY_RIGHT: if (!card_move (k)) set_tab (g_tab + 1); return;
		case KEY_UP: card_move (k); return;
		case KEY_DOWN:
			if (!card_move (k) && lay ().strip && g_ntasks > 0) { g_stripFocus = 0; g_homeDirty = true; }
			return;
		case KEY_PGDN: case KEY_PGUP: case KEY_HOME: case KEY_END:
			{
				Lay L = lay (); tab_items (); Card c = card (L);
				if (k == KEY_PGDN) g_focus += c.cols * c.rows;
				else if (k == KEY_PGUP) g_focus -= c.cols * c.rows;
				else g_focus = k == KEY_HOME ? 0 : c.total - 1;
				if (g_focus >= c.total) g_focus = c.total - 1;
				if (g_focus < 0) g_focus = 0;
				g_homeDirty = true;
				return;
			}
		}
	}
	if (k >= 32 && k < 256 && k != 127 && !(mods & MOD_CTRL) && ql < (int) sizeof g_query - 3)	// typing: the search
	{
		if (k < 128) g_query[ql++] = (char) k;
		else { g_query[ql++] = (char) (0xC0 | (k >> 6)); g_query[ql++] = (char) (0x80 | (k & 0x3F)); }	// (Latin-1 -> UTF-8)
		g_query[ql] = 0;
		g_stripFocus = -1;
		query_changed ();
	}
}

static void note_open (int i);
static void home_click (int x, int y, bool right)
{
	const Hit *h = g_homeHits.at (x, y);
	if (h == 0) return;
	switch (h->kind)
	{
	case H_TAB: set_tab (h->i); break;
	case H_CHEV:
		if (h->i > 0) { if (g_tabFirst + 1 < g_ncats) g_tabFirst++; if (g_tab < g_tabFirst) set_tab (g_tabFirst); }
		else g_tabFirst = 0;
		g_homeDirty = true;
		break;
	case H_TILE: g_focus = h->i; g_stripFocus = -1; g_homeDirty = true; if (!right) open_focused (); break;
	case H_DOC: { tab_items (); Card c = card (lay ()); g_focus = c.ntiles + h->i; g_stripFocus = -1; g_homeDirty = true; if (!right) open_doc (h->i); break; }
	case H_RUNCARD: g_stripFocus = -1; uk_shell_front (g_tasks[h->i].id, 1); break;
	case H_RUNX: uk_win_close (g_tasks[h->i].id); break;
	case H_RESULT: g_resSel = h->i; open_focused (); break;
	case H_CLEARQ: g_query[0] = 0; g_kind = 0; query_changed (); break;
	case H_KIND: g_kind = h->i; query_changed (); break;
	case H_TODAY: case H_AGENDA: open_app ("calendar"); break;
	case H_HNOTE: note_open (h->i); break;
	}
}
