//
// htmltest.cpp -- Mail's HTML renderer (user/Libs/mail/html.h) on the PC: a few messages' HTML (a newsletter in tables, a
// receipt, text styles and lists, a plain text) laid out and drawn with the card's fonts into PPM files (the test
// script turns them into PNGs to look at), with checks on the layout (the tables' columns, the centring, the lines,
// the links, the hidden parts). Run by tools/tests/run_mail_test.sh.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "fontkit/fonts.h"
#include "mail/html_ft.h"
#include "mail/mime.h"

using namespace mail;
using namespace mail::html;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); printf (__VA_ARGS__); printf ("\n"); } } while (0)

// a picture for cid:logo (a blue square with a white bar)
struct Pics : Pictures
{
	unsigned px[64 * 32];
	Pics () { for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) px[y * 64 + x] = (y > 12 && y < 20 && x > 8 && x < 56) ? 0xFFFFFFFF : 0xFF1A73E8; }
	const unsigned *get (const char *src, int *w, int *h) override
	{
		if (!strcmp (src, "cid:logo@shop")) { *w = 64; *h = 32; return px; }
		return 0;							// (remote: blocked)
	}
};

static void save_ppm (const uikit::Canvas &cv, const char *path)
{
	FILE *f = fopen (path, "wb"); if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", cv.w, cv.h);
	for (int i = 0; i < cv.w * cv.h; i++) { unsigned c = cv.px[i]; unsigned char b[3] = { (unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c }; fwrite (b, 1, 3, f); }
	fclose (f);
}
static Html *render (const char *name, const char *src, int w, bool text = false)
{
	static FtHost host; static Pics pics; host.pics = &pics;
	Html *h = new Html;
	if (text) h->parse_text (src, (int) strlen (src)); else h->parse (src, (int) strlen (src));
	h->layout (host, w - 32);
	int H = h->height () + 32; if (H > 3000) H = 3000; if (H < 100) H = 100;
	uikit::Canvas cv; cv.alloc (w, H); cv.clear (0xFFFFFFFF);
	unsigned bg = h->background (); if (bg >> 24) cv.clear (bg);
	host.cv = &cv;
	h->paint (host, 16, 16, 0, 0, w, H);
	char p[256]; snprintf (p, sizeof p, "%s/%s.ppm", getenv ("OUT") ? getenv ("OUT") : ".", name);
	save_ppm (cv, p);
	printf ("html: %s %dx%d, %d items\n", name, w, H, h->lay->items.n);
	return h;
}
static const Item *find_text (Html *h, const char *s)
{
	for (int i = 0; i < h->lay->items.n; i++) { const Item &it = h->lay->items[i]; if (it.kind == I_TEXT && it.n >= (int) strlen (s) && !memcmp (it.s, s, strlen (s))) return &it; }
	return 0;
}

static const char NEWSLETTER[] =
	"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"><html><head><meta charset=\"utf-8\"><title>Onyx News</title>"
	"<style type=\"text/css\">body{margin:0;background:#eef1f5}.wrap{width:100%;background:#eef1f5}"
	".card{background:#ffffff;border-radius:8px}.h1{font-family:Georgia,serif;font-size:26px;color:#1b2a41;margin:0 0 8px}"
	"p{font-family:Arial,Helvetica,sans-serif;font-size:15px;line-height:22px;color:#3c4043;margin:0 0 14px}"
	".btn{background:#1a73e8;color:#ffffff !important;text-decoration:none;padding:10px 22px;border-radius:4px;font-weight:bold;font-family:Arial}"
	".muted{color:#80868b;font-size:12px}.preheader{display:none!important}"
	"@media only screen and (max-width:480px){.col{display:block!important;width:100%!important}}"
	"</style></head><body bgcolor=\"#eef1f5\">"
	"<div class=\"preheader\">This preheader must not show.</div>"
	"<table class=\"wrap\" width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\"><tr><td align=\"center\" style=\"padding:24px 0\">"
	"<table width=\"520\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" class=\"card\">"
	"<tr><td style=\"padding:20px 28px;border-bottom:1px solid #e0e3e7\"><img src=\"cid:logo@shop\" width=\"64\" height=\"32\" alt=\"Shop\" style=\"display:block\"></td></tr>"
	"<tr><td style=\"padding:28px\"><h1 class=\"h1\">Your order is on its way</h1>"
	"<p>Hello Anna, good news: the <b>Raspberry Pi&nbsp;4</b> you ordered on <i>Monday</i> has left our warehouse &amp; should reach you by Thursday.</p>"
	"<p style=\"text-align:center;margin:24px 0\"><a class=\"btn\" href=\"https://shop.example/track/123\">Track my parcel</a></p>"
	"<table width=\"100%\" cellpadding=\"6\" cellspacing=\"0\" style=\"border-collapse:collapse;font-family:Arial;font-size:14px\">"
	"<tr style=\"background:#f1f3f4\"><th align=\"left\">Item</th><th align=\"right\" width=\"60\">Qty</th><th align=\"right\" width=\"90\">Price</th></tr>"
	"<tr><td style=\"border-bottom:1px solid #e0e3e7\">Raspberry Pi 4 Model B, 8 GB</td><td align=\"right\" style=\"border-bottom:1px solid #e0e3e7\">1</td><td align=\"right\" style=\"border-bottom:1px solid #e0e3e7\">&euro;&nbsp;89.90</td></tr>"
	"<tr><td>Official case (red/white)</td><td align=\"right\">2</td><td align=\"right\">&euro;&nbsp;11.80</td></tr>"
	"<tr><td colspan=\"2\" align=\"right\"><b>Total</b></td><td align=\"right\"><b>&euro;&nbsp;101.70</b></td></tr></table>"
	"</td></tr>"
	"<tr><td style=\"padding:0 28px 24px\"><table width=\"100%\"><tr>"
	"<td class=\"col\" width=\"50%\" valign=\"top\" style=\"padding-right:10px\"><p><b>Shipping to</b><br>Anna Lefèvre<br>12 rue des Fleurs<br>1000 Bruxelles</p></td>"
	"<td class=\"col\" width=\"50%\" valign=\"top\"><p><b>Questions?</b><br>Reply to this message or call <a href=\"tel:+3221234567\">+32 2 123 45 67</a>.</p>"
	"<img src=\"https://tracker.example/open.gif\" width=\"1\" height=\"1\"><img src=\"https://cdn.example/banner.jpg\" width=\"200\" height=\"60\" alt=\"Summer sale\"></td>"
	"</tr></table></td></tr>"
	"</table>"
	"<p class=\"muted\" style=\"margin-top:16px\">You receive this because you shopped at Pi Shop. <a href=\"https://shop.example/unsub\" style=\"color:#80868b\">Unsubscribe</a></p>"
	"</td></tr></table></body></html>";

static const char STYLES[] =
	"<div style=\"font-family:'Segoe UI',sans-serif;font-size:14px\">"
	"<h2 style=\"color:#b3261e;border-bottom:2px solid #b3261e;padding-bottom:4px\">Meeting notes &mdash; Q3</h2>"
	"<p>Text in <b>bold</b>, <i>italic</i>, <u>underlined</u>, <s>struck</s>, <code>code()</code>, <span style=\"background:#fff59d\">highlighted</span>, "
	"H<sub>2</sub>O and E=mc<sup>2</sup>, <font color=\"#188038\" size=\"4\">a green font</font> and <small>small print</small>.</p>"
	"<ul><li>First point, long enough to wrap onto a second line inside the list item so that the marker stays on the first line only.</li>"
	"<li>Second point<ul><li>nested one</li><li>nested two</li></ul></li></ul>"
	"<ol start=\"3\"><li>three</li><li>four</li><li value=\"10\">ten</li></ol>"
	"<blockquote style=\"border-left:3px solid #ccc;margin:8px 0;padding-left:10px;color:#5f6368\">A quoted reply, as Gmail does it, with its grey bar.</blockquote>"
	"<pre style=\"background:#f6f8fa;padding:8px\">int main ()\n{\n    return 0;\t// tab\n}</pre>"
	"<img src=\"cid:logo@shop\" align=\"left\" hspace=\"6\"><p>A floated picture on the left: this text flows beside it and then below it once it is past the picture's bottom edge, as in HTML 4 mail.</p>"
	"<hr><p style=\"text-align:justify\">Justified text: Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.</p>"
	"<table border=\"1\" cellpadding=\"4\"><caption>A bordered table</caption><tr><td rowspan=\"2\">rowspan</td><td>b</td></tr><tr><td>c</td></tr></table>"
	"<p>A very long link: https://example.com/a/very/long/path/that/does/not/fit/on/one/line/at/all/so/it/is/cut/somewhere/sensible</p>"
	"</div>";

static const char PLAIN[] =
	"Hi Anna,\n\nSee you on Saturday! The cabin is at https://maps.example/cabin?id=42.\n\n> Are we still on for Saturday?\n> I booked the cabin.\n\nBjörn";

int main ()
{
	fnt::init ();
	// the newsletter
	Html *h = render ("html-newsletter", NEWSLETTER, 640);
	CHECK (!strcmp (h->title (), "Onyx News"), "[%s]", h->title ());
	CHECK (!find_text (h, "This preheader"), "preheader hidden");
	const Item *track = find_text (h, "Track");
	CHECK (track && track->link >= 0 && !strcmp (h->link_at (track->x + 2, track->y - 3), "https://shop.example/track/123"), "the button's link");
	CHECK (track && (track->color & 0xFFFFFF) == 0xFFFFFF, "white on blue %08x", track ? track->color : 0);
	// the card (520 wide) centred in 608
	int cardX = -1; for (int i = 0; i < h->lay->items.n; i++) { const Item &it = h->lay->items[i]; if (it.kind == I_RECT && it.w == 520 && (it.color & 0xFFFFFF) == 0xFFFFFF) { cardX = it.x; break; } }
	CHECK (cardX == (608 - 520) / 2, "card at %d", cardX);
	const Item *qty = find_text (h, "Qty"), *price = find_text (h, "Price"), *total = find_text (h, "Total");
	CHECK (qty && price && qty->x < price->x, "columns");
	const Item *amount = find_text (h, "\xE2\x82\xAC\xC2\xA0" "101.70");
	CHECK (amount && price && amount->x + amount->w > price->x + price->w - 4 && amount->x + amount->w <= cardX + 520 - 28 + 1, "right aligned %d %d", amount ? amount->x + amount->w : 0, price ? price->x + price->w : 0);
	CHECK (total && amount && total->y == amount->y, "total row");
	const Item *ship = find_text (h, "Shipping"), *quest = find_text (h, "Questions?");
	CHECK (ship && quest && ship->y == quest->y && quest->x > ship->x + 200, "two columns (%d,%d) (%d,%d)", ship ? ship->x : 0, ship ? ship->y : 0, quest ? quest->x : 0, quest ? quest->y : 0);
	int blocked = 0; for (int i = 0; i < h->lay->items.n; i++) if (h->lay->items[i].kind == I_BLOCKED) blocked++;
	CHECK (blocked == 1 && h->remote_pictures () == 2, "blocked %d remote %d", blocked, h->remote_pictures ());
	int pics = 0; for (int i = 0; i < h->lay->items.n; i++) if (h->lay->items[i].kind == I_IMAGE) pics++;
	CHECK (pics == 1, "the cid picture");
	// narrow: the media query stacks the columns
	Html *n = render ("html-newsletter-narrow", NEWSLETTER, 420);
	const Item *ship2 = find_text (n, "Shipping"), *quest2 = find_text (n, "Questions?");
	CHECK (ship2 && quest2 && quest2->y > ship2->y + 40, "stacked");
	delete n;
	Buf txt; h->plain_text (txt);
	CHECK (strstr (txt.c (), "Your order is on its way") && strstr (txt.c (), "Track my parcel"), "plain text");
	delete h;

	// the styles
	h = render ("html-styles", STYLES, 640);
	const Item *first = find_text (h, "First point"); const Item *three = find_text (h, "3."), *ten = find_text (h, "10.");
	CHECK (first && three && ten, "list markers");
	const Item *bullet = find_text (h, "\xE2\x80\xA2");
	CHECK (bullet && first && bullet->y == first->y && bullet->x < first->x, "the bullet on the first line");
	const Item *code = find_text (h, "code()"); CHECK (code && code->font != (first ? first->font : -1), "monospace");
	const Item *tab = find_text (h, "    return 0;"); CHECK (tab != 0, "pre kept");
	const Item *flt = find_text (h, "A floated"); CHECK (flt && flt->x >= 64 + 12, "beside the float %d", flt ? flt->x : 0);
	const Item *cut = find_text (h, "https://example.com/a/very"); CHECK (cut && cut->w <= 608, "the long word cut %d", cut ? cut->w : 0);
	delete h;

	// plain text
	h = render ("html-plain", PLAIN, 640, true);
	const Item *link = find_text (h, "https://maps.example/cabin?id=42");
	CHECK (link && link->link >= 0 && !strcmp (h->link_at (link->x + 3, link->y - 3), "https://maps.example/cabin?id=42"), "plain text link");
	const Item *q = find_text (h, "> Are we"); CHECK (q && (q->color & 0xFFFFFF) == 0x5f6368, "quoted");
	delete h;

	// a robustness pass: broken HTML
	const char *bad = "<table><tr><td>a<td>b<tr><td>c</table><p>one<p>two<li>x<b><i>nested</b></i></p></div></span><img><br/>&notanentity; &#x1F600; &#128512 <<>> </";
	h = render ("html-broken", bad, 400);
	CHECK (find_text (h, "two") && find_text (h, "c"), "broken html");
	delete h;

	printf ("html: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
