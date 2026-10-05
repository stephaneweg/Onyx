//
// facetest -- (the desktop simulator) checks uikit's text hook (uikit/text.h) with FreeType's face
// (ft/uikitface.h): the bitmap path's measures, the face's prefix widths (monotone, their sum), a
// click's place (the nearest character boundary), UTF-8 (next / prev / get), Textbox and Textarea
// editing by whole characters (typed Latin-1 and the euro stored as UTF-8), a click's caret, the
// scroll keeping the caret in view, a password's stars, up / down by x, uk_text_fit, and the way
// back to the bitmap fonts. Built and run by studio.sh; prints "facetest: all passed".
//
#include "kapi.h"
#include "uikit/uikit.h"
#include "ft/uikitface.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
using namespace uikit;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf ("FAIL %s:%d: ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static void check_positions (const char *s)
{
	int n = strlen (s), prev = -1;
	int W[256], B[256], nb = 0;
	for (int i = 0; ; i = uk_u8_next (s, i, n)) { B[nb] = i; W[nb] = uk_tw_n (s, i); nb++; if (i >= n) break; }
	for (int k = 1; k < nb; k++) CHECK (W[k] >= W[k - 1] - 1, "not monotone at %d in '%s'", B[k], s);
	CHECK (W[nb - 1] == uk_tw (s), "total %d vs %d", W[nb - 1], uk_tw (s));
	for (int x = -3; x < W[nb - 1] + 12; x++)
	{
		int p = uk_tpos (s, n, x), best = 1 << 30, ok = 0;
		for (int k = 0; k < nb; k++) { int d = abs (W[k] - (x < 0 ? 0 : x)); if (d < best) best = d; }
		for (int k = 0; k < nb; k++) if (B[k] == p && abs (W[k] - (x < 0 ? 0 : x)) == best) ok = 1;
		CHECK (ok, "tpos '%s' x=%d -> %d (not the nearest boundary)", s, x, p);
		(void) prev;
	}
}

int main ()
{
	CHECK (uk_textface () == 0, "a face before install");
	int bfh = uk_fh (), bfw = uk_fw ();
	CHECK (bfh == 16 && bfw == 8, "bitmap cell %d x %d", bfw, bfh);
	CHECK (uk_tw ("hello") == 40, "bitmap tw");
	CHECK (uk_tpos ("hello", 5, 13) == 2, "bitmap tpos %d", uk_tpos ("hello", 5, 13));
	CHECK (ft_uikit_install ("DejaVu Sans", 13), "install");
	TextFace *f = uk_textface ();
	printf ("face: height %d ascent %d fw %d; 'Afro-jazz, Ghibli colours' = %d px\n", f->height (), f->ascent (), uk_fw (), uk_tw ("Afro-jazz, Ghibli colours"));
	CHECK (uk_fh () == f->height (), "uk_fh follows the face");
	check_positions ("Afro-jazz, Ghibli colours");
	check_positions ("WWW MMM iii lll");
	check_positions ("Caf\xC3\xA9 na\xC3\xAFve \xE2\x82\xAC \xF0\x9F\x8E\xB5!");
	check_positions ("Latin-1 stray: \xE9t\xE9");
	// bold is wider
	CHECK (uk_tw ("Generate", 2) > uk_tw ("Generate", 0), "bold not wider");
	// width cache consistency: same string twice, prefix vs whole
	CHECK (uk_tw_n ("abcdef", 3) == uk_tw ("abc"), "prefix %d vs %d", uk_tw_n ("abcdef", 3), uk_tw ("abc"));
	// UTF-8 helpers
	const char *u = "a\xC3\xA9" "b\xE2\x82\xAC";		// a é b €
	CHECK (uk_u8_next (u, 1, 7) == 3 && uk_u8_prev (u, 3) == 1 && uk_u8_prev (u, 7) == 4 && uk_u8_next (u, 4, 7) == 7, "u8 next/prev");
	int k; CHECK (uk_u8_get (u + 4, 3, &k) == 0x20AC && k == 3, "u8 get");
	CHECK (uk_u8_get ("\xE9", 1, &k) == 0xE9 && k == 1, "stray byte");
	// Textbox editing
	Textbox tb (0, 0, 200, 26, "Caf\xC3\xA9");
	CHECK (tb.caret == 5, "caret %d", tb.caret);
	tb.onKey (KEY_LEFT); CHECK (tb.caret == 3, "left over e-acute: %d", tb.caret);
	tb.onKey (KEY_RIGHT); CHECK (tb.caret == 5, "right: %d", tb.caret);
	tb.onKey (KEY_BACKSPACE); CHECK (!strcmp (tb.text, "Caf") && tb.caret == 3, "backspace: '%s' %d", tb.text, tb.caret);
	tb.onKey (0xE9); CHECK (!strcmp (tb.text, "Caf\xC3\xA9") && tb.caret == 5, "typed e-acute");
	tb.onKey (0x80); CHECK (!strcmp (tb.text, "Caf\xC3\xA9\xE2\x82\xAC") && tb.caret == 8, "typed euro");
	tb.onKey (KEY_HOME); tb.onKey (KEY_DEL); CHECK (!strcmp (tb.text, "af\xC3\xA9\xE2\x82\xAC"), "del '%s'", tb.text);
	tb.caret = 2; tb.onKey (KEY_DEL); CHECK (!strcmp (tb.text, "af\xE2\x82\xAC"), "del e-acute '%s'", tb.text);
	// a click: the caret at the nearest boundary
	tb.setText ("Afro-jazz, Ghibli colours"); tb.onDraw ();
	int x = 6 + uk_tw_n (tb.text, 11);			// right before "Ghibli"
	tb.onMouse (-1, -1, 0, 0, 0, 0); tb.onMouse (x + 1, 10, 1, 0, 0, 0); tb.onMouse (x + 1, 10, 0, 0, 0, 0);
	CHECK (tb.caret == 11, "click caret %d", tb.caret);
	// a long text scrolls: the caret stays visible
	tb.setText ("The quick brown fox jumps over the lazy dog again and again"); tb.onDraw ();
	CHECK (tb.vstart > 0, "no scroll: vstart %d", tb.vstart);
	CHECK (uk_tw_n (tb.text + tb.vstart, tb.caret - tb.vstart) <= 200 - 12, "caret out of view");
	tb.onKey (KEY_HOME); tb.onDraw (); CHECK (tb.vstart == 0, "home: vstart %d", tb.vstart);
	// a password shows stars
	Textbox pw (0, 0, 200, 26, "p\xC3\xA9ss"); pw.password = true; pw.onDraw ();
	pw.onKey (KEY_LEFT); pw.onKey (KEY_LEFT); pw.onKey (KEY_LEFT); CHECK (pw.caret == 1, "pw caret %d", pw.caret);
	// Textarea: up / down by x, left / right by character, UTF-8 backspace
	Textarea ta (0, 0, 300, 120, 256);
	ta.setContent ("abc\nWWWWW\niii\n\xC3\xA9t\xC3\xA9");
	ta.caret = 6;						// after "WW"
	ta.onKey (KEY_UP); CHECK (ta.caret == 3, "up: %d", ta.caret);	// "abc" is narrower than "WW"
	ta.caret = 5; ta.onKey (KEY_DOWN);			// after the first W -> "iii": nearest x
	{ int xw = uk_tw ("W"), best = 0; for (int i = 1; i <= 3; i++) if (abs (uk_tw_n ("iii", i) - xw) < abs (uk_tw_n ("iii", best) - xw)) best = i;
	  CHECK (ta.caret == 10 + best, "down: %d (want %d)", ta.caret, 10 + best); }
	ta.caret = 19; ta.onKey (KEY_BACKSPACE); CHECK (ta.len == 17 && ta.caret == 17, "ta backspace: len %d caret %d", ta.len, ta.caret);
	ta.onKey (KEY_LEFT); CHECK (ta.caret == 16, "ta left %d", ta.caret);
	ta.onKey (KEY_LEFT); CHECK (ta.caret == 14, "ta left over e-acute %d", ta.caret);
	ta.onKey (0xE8); CHECK (ta.len == 19 && (unsigned char) ta.buf[14] == 0xC3 && (unsigned char) ta.buf[15] == 0xA8, "ta typed");
	ta.onDraw ();
	// a click in the textarea: row 1, before the third W
	ta.onMouse (-1, -1, 0, 0, 0, 0);
	ta.onMouse (4 + uk_tw ("WW") + 1, 2 + uk_fh () + 3, 1, 0, 0, 0); ta.onMouse (4 + uk_tw ("WW") + 1, 2 + uk_fh () + 3, 0, 0, 0, 0);
	CHECK (ta.caret == 6, "ta click %d", ta.caret);
	// text fit
	char b[64]; int w = uk_text_fit ("E(3,8) conga and more", 60, b, sizeof b);
	CHECK (w <= 60 && strstr (b, "...") != 0, "fit '%s' %d", b, w);
	w = uk_text_fit ("Hi", 60, b, sizeof b); CHECK (!strcmp (b, "Hi"), "fit short");
	// back to the bitmap fonts
	uk_set_textface (0); CHECK (uk_fh () == 16 && uk_fw () == 8 && uk_tw ("hello") == 40, "back to bitmap");
	printf (fails ? "facetest: %d FAILED\n" : "facetest: all passed\n", fails);
	return fails != 0;
}
