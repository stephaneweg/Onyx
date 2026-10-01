//
// Apps/clipboard/main.cpp -- the shared clipboard's widget (docs/clipboard/README.md): the dock's clipboard
// button opens it at the bottom right of the screen, its bottom above the dock's top (the work area's
// bottom: the dock is outside it, however wide). It lists clipd's ring, newest first -- each item an
// icon of its kind, a line of it (an image: only its size), its time --; a click puts the cursor there
// (Ctrl+V pastes that one), the x of the row under the pointer deletes it, the bin clears them all.
// It goes when another window takes the keyboard, or Esc. A client of clipd (user/clipproto.h):
// CLIP_LIST, CLIP_SUBSCRIBE (re-listed at each change), CLIP_CURSOR, CLIP_DELETE, CLIP_CLEAR.
//
#include "kapi.h"
#include "ft/wtkface.h"
#include "wtk/wtk.h"
#include "clipboard.h"

using namespace wtk;

static const int W = 300, ROW = 30, HEAD = 42, PAD = 8;
static const int HMAX = HEAD + CLIP_RING * ROW + PAD;
static const unsigned KEY = WK_TRANSPARENT_KEY;

static ClipItemMsg g_it[CLIP_RING]; static int g_n;
static int g_pid;
static FtTextFace *g_small;

// ---- the conversation with clipd ------------------------------------------------------------------------
static void send (int type, const void *d, unsigned n) { if (g_pid) kapi_mailbox_send (g_pid, type, d, n); }
static void send_id (int type, unsigned id) { unsigned char b[4]; clipc_put32 (b, id); send (type, b, 4); }
static bool g_listing;
static void ask_list () { if (!g_listing) { g_listing = true; send (CLIP_LIST, "", 1); } }

// ---- the icons of the kinds ------------------------------------------------------------------------------
static unsigned kind_colour (const char *k)
{
	if (!strcmp (k, "image")) return 0x4EA05C;
	if (!strncmp (k, "files", 5)) return 0xE2B45C;
	if (!strcmp (k, "rtf")) return 0x3C60B0;
	if (!strcmp (k, "url")) return 0x2878B4;
	return 0x60687A;
}
static void kind_icon (Canvas &cv, const char *k, int x, int y, int s)
{
	VPath b; b.rrect (V (x), V (y), V (s), V (s), V (4)); b.fill (cv, kind_colour (k));
	const unsigned wh = 0xFFFFFF;
	if (!strcmp (k, "image"))
	{
		int t[] = { V (x + 3), V (y + s - 4), V (x + 8), V (y + 7), V (x + 12), V (y + s - 7), V (x + 14), V (y + s - 9), V (x + s - 3), V (y + s - 4) };
		VPath p; p.poly (t, 5); p.circle (V (x + s - 6), V (y + 6), V (2)); p.fill (cv, wh);
	}
	else if (!strncmp (k, "files", 5))
	{
		VPath p; p.rrect (V (x + 3), V (y + 6), V (s - 6), V (s - 9), V (1)); p.rrect (V (x + 3), V (y + 4), V (6), V (3), V (1)); p.fill (cv, wh);
	}
	else if (!strcmp (k, "url"))
	{
		VPath p; p.arc (V (x + s / 2), V (y + s / 2), V (s / 2 - 4), 0, 360, V (2)); p.line (V (x + 4), V (y + s / 2), V (x + s - 4), V (y + s / 2), V (2)); p.fill (cv, wh);
	}
	else
	{
		VPath p; for (int i = 0; i < 3; i++) p.rect (V (x + 4), V (y + 5 + i * 4), V (s - 8 - (i == 2) * 4), V (2)); p.fill (cv, wh);
	}
}

// ---- the card --------------------------------------------------------------------------------------
class Card : public Root
{
public:
	int hot, binHot; bool wasL; unsigned openT;
	Card (int x, int y) : Root (x, y, W, HMAX, "clipboard", WIN_FLAG_BORDERLESS | WIN_FLAG_TRANSPARENT | WIN_FLAG_SYSTEM),
		hot (-1), binHot (0), wasL (false), openT (kapi_get_ticks ()) { setBg (KEY); }
	int cardH () const { return HEAD + (g_n ? g_n : 1) * ROW + PAD; }
	int cardY () const { return HMAX - cardH (); }
	int rowAt (int my) const { int y0 = cardY () + HEAD; if (my < y0) return -1; int r = (my - y0) / ROW; return r < g_n ? r : -1; }
	void onDraw () override
	{
		canvas.clear (KEY);
		int y0 = cardY (), h = cardH ();
		unsigned face = wk_mix (C_FIELD, C_BG, 40), ink = C_FIELD_TEXT, dim = wk_mix (C_FIELD_TEXT, face, 110);
		canvas.fillRect (0, y0, W, h, face);
		wk_rline (canvas, 0, y0, W, h, 12, wk_tone (C_BG, 80), 255);
		wk_corner_key (canvas, 0, y0, W, h, 12);
		// the head: the clipboard's glyph, the title, the count, the bin
		int gx = 14, gy = y0 + 12;
		VPath o; int bd[] = { V (gx), V (gy + 2), V (gx + 14), V (gy + 2), V (gx + 14), V (gy + 18), V (gx), V (gy + 18) };
		o.polyline (bd, 4, V (2), true); o.rrect (V (gx + 3), V (gy), V (8), V (5), V (1)); o.fill (canvas, ink);
		wk_text (canvas, 40, y0 + (40 - wk_fh ()) / 2, "Clipboard", ink, 2);
		char c[16]; c[0] = (char) ('0' + g_n / 10); c[1] = (char) ('0' + g_n % 10); c[2] = 0;
		const char *cs = g_n >= 10 ? c : c + 1;
		char cnt[16]; int k = 0; for (int i = 0; cs[i]; i++) cnt[k++] = cs[i];
		const char *tail = " / 10"; for (int i = 0; tail[i]; i++) cnt[k++] = tail[i]; cnt[k] = 0;
		int cx = 40 + wk_tw ("Clipboard", 2) + 8;
		{ WkFaceScope sc (g_small); wk_text (canvas, cx, y0 + (40 - wk_fh ()) / 2 + 1, cnt, dim); }
		int bx = W - 36, by = y0 + 8;
		if (binHot) wk_rbox (canvas, bx, by, 26, 26, 6, wk_mix (face, 0x000000, 24), wk_mix (face, 0x000000, 24));
		VPath bin; int bo[] = { V (bx + 8), V (by + 9), V (bx + 18), V (by + 9), V (bx + 17), V (by + 21), V (bx + 9), V (by + 21) };
		bin.polyline (bo, 4, 24, true); bin.rect (V (bx + 6), V (by + 7), V (14), 24); bin.rect (V (bx + 11), V (by + 5), V (4), 24);
		bin.fill (canvas, g_n ? dim : wk_mix (dim, face, 120));
		canvas.fillRect (10, y0 + HEAD - 4, W - 20, 1, wk_mix (face, 0x000000, 30));
		// the rows
		if (!g_n)
		{
			WkFaceScope sc (g_small);
			const char *e = "Nothing copied yet";
			wk_text (canvas, (W - wk_tw (e)) / 2, y0 + HEAD + (ROW - wk_fh ()) / 2, e, dim);
		}
		for (int i = 0; i < g_n; i++)
		{
			const ClipItemMsg &m = g_it[i];
			int y = y0 + HEAD + i * ROW;
			if (m.cursor) { wk_rbox (canvas, 6, y, W - 12, ROW - 2, 6, wk_mix (face, C_ACCENT, 56), wk_mix (face, C_ACCENT, 56)); wk_rline (canvas, 6, y, W - 12, ROW - 2, 6, C_ACCENT, 255); }
			else if (i == hot) wk_rbox (canvas, 6, y, W - 12, ROW - 2, 6, wk_mix (face, C_ACCENT, 22), wk_mix (face, C_ACCENT, 22));
			kind_icon (canvas, m.kind, 14, y + 5, 18);
			char line[420];
			if (!strcmp (m.kind, "image"))
			{
				char a[12], b[12]; int ka = 0, kb = 0; unsigned v = m.w; char t[12]; int kt = 0;
				do { t[kt++] = (char) ('0' + v % 10); v /= 10; } while (v); while (kt) a[ka++] = t[--kt]; a[ka] = 0;
				v = m.h; do { t[kt++] = (char) ('0' + v % 10); v /= 10; } while (v); while (kt) b[kb++] = t[--kt]; b[kb] = 0;
				int p = 0; const char *s = "Image  "; for (int j = 0; s[j]; j++) line[p++] = s[j];
				for (int j = 0; a[j]; j++) line[p++] = a[j]; s = " x "; for (int j = 0; s[j]; j++) line[p++] = s[j];
				for (int j = 0; b[j]; j++) line[p++] = b[j]; line[p] = 0;
			}
			else
			{
				int p = 0; for (int j = 0; m.preview[j] && p < 400; j++) line[p++] = m.preview[j];
				if (!strcmp (m.kind, "files-cut")) { const char *s = "  (cut)"; for (int j = 0; s[j]; j++) line[p++] = s[j]; }
				line[p] = 0;
				if (!line[0]) { const char *s = "(empty)"; for (p = 0; s[p]; p++) line[p] = s[p]; line[p] = 0; }
			}
			char when[8] = "00:00"; when[0] += (char) (m.hour / 10); when[1] += (char) (m.hour % 10); when[3] += (char) (m.minute / 10); when[4] += (char) (m.minute % 10);
			int right = 0;
			{ WkFaceScope sc (g_small); right = i == hot ? 26 : wk_tw (when) + 10; }
			char fit[200]; wk_text_fit (line, W - 44 - 14 - right, fit, sizeof fit);
			wk_text (canvas, 40, y + (ROW - 2 - wk_fh ()) / 2, fit, !strcmp (m.kind, "url") ? wk_mix (C_ACCENT, 0x000000, 70) : ink);
			if (i == hot) wk_glyph (canvas, WKG_CLOSE, W - 22, y + ROW / 2 - 1, 10, dim);
			else { WkFaceScope sc (g_small); wk_text (canvas, W - 14 - wk_tw (when), y + (ROW - 2 - wk_fh ()) / 2, when, dim); }
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int r = mx < 0 ? -1 : rowAt (my);
		int y0 = cardY ();
		bool onBin = mx >= W - 36 && mx < W - 10 && my >= y0 + 8 && my < y0 + 34;
		if (r != hot || onBin != (binHot != 0)) { hot = r; binHot = onBin; invalidate (true); }
		if (bl && !wasL)
		{
			if (my >= 0 && my < y0) { wasL = true; kapi_exit (0); }	// (above the card: elsewhere)
			if (onBin && g_n) send (CLIP_CLEAR, "", 1);
			else if (r >= 0)
			{
				if (mx >= W - 32) send_id (CLIP_DELETE, g_it[r].id);
				else send_id (CLIP_CURSOR, g_it[r].id);
			}
		}
		wasL = bl != 0;
		return true;
	}
	bool onKey (long k) override
	{
		if (k == 27) kapi_exit (0);
		int c = -1; for (int i = 0; i < g_n; i++) if (g_it[i].cursor) c = i;
		if ((k == KEY_UP || k == KEY_DOWN) && g_n)
		{
			c = c < 0 ? 0 : k == KEY_UP ? (c > 0 ? c - 1 : 0) : (c < g_n - 1 ? c + 1 : c);
			send_id (CLIP_CURSOR, g_it[c].id);
			return true;
		}
		if (k == KEY_DEL && c >= 0) { send_id (CLIP_DELETE, g_it[c].id); return true; }
		if (k == KEY_ENTER) kapi_exit (0);
		return false;
	}
	void onTick () override
	{
		// clipd's answers and news
		static ClipItemMsg tmp[CLIP_RING]; static int nt;
		unsigned char buf[520]; int from, type, n;
		while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf, 0)) >= 0)
		{
			if (from != g_pid) continue;
			if (type == CLIP_CHANGED) { ask_list (); continue; }
			if (type == CLIP_ITEM && n == (int) sizeof (ClipItemMsg) && nt < CLIP_RING) memcpy (&tmp[nt++], buf, sizeof (ClipItemMsg));
			if (type == CLIP_END)
			{
				memcpy (g_it, tmp, sizeof (ClipItemMsg) * nt); g_n = nt; nt = 0; g_listing = false;
				if (hot >= g_n) hot = -1;
				invalidate (true);
			}
		}
		// gone when another window has the keyboard (a click elsewhere)
		struct kapi_win_geom g;
		if (kapi_get_ticks () - openT > 30 && kapi_win_geometry (&g) == 0 && !(g.state & KAPI_WIN_KEYS)) kapi_exit (0);
	}
};

int main (void)
{
	ft_wtk_install ("DejaVu Sans", 13);
	g_small = new FtTextFace; if (!g_small->open ("DejaVu Sans", 11)) g_small = 0;
	g_pid = clip_service_ ();
	Card card (-W - 50, 30);				// (off the screen until it is placed)
	if (card.canvas.px == 0) return 1;
	struct kapi_win_geom g;
	int x = 600, y = 300;
	if (kapi_win_geometry (&g) == 0 && g.aw > 0) { x = g.ax + g.aw - W - 12; y = g.ay + g.ah - HMAX - 10; }
	kapi_move_window (x, y);
	send (CLIP_SUBSCRIBE, "", 1);
	ask_list ();
	card.run ();
	return 0;
}
