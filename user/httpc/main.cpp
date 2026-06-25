//
// httpc -- a small, mouse-driven text web browser for Onyx (Lynx-like).
//
// Fetches http:// and https:// pages (the reusable HttpClient + the mbedTLS transport,
// user/http.hpp / user/tls/), strips the HTML to flowing text, shows hyperlinks in blue
// (click to follow), and renders simple forms: text fields, checkboxes, dropdowns and
// buttons. A navbar gives Back / Forward / an address bar / Go, and a right-hand scrollbar.
//
// It is a newlib C++ app (for TLS + malloc) that draws its whole UI itself into the window
// content canvas (kapi_create_window + kapi_draw_text_buf + kapi_present) and is driven by
// the kapi pointer/key event streams -- no widget toolkit, so it stays newlib-clean.
//
#define ONYX_HTTP_TLS
#include "http.hpp"
#include "kapi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ---- palette (0x00RRGGBB; the canvas ignores the top byte) -------------------
enum {
	C_BG     = 0x00FFFFFF, C_TEXT  = 0x00202024, C_LINK = 0x001A4Fd0,
	C_NAV    = 0x00E6E6EC, C_BTN   = 0x00D2D2DA, C_BTNH = 0x00BFC8E8,
	C_BORDER = 0x00888890, C_FIELD = 0x00FFFFFF, C_FOCUS = 0x003366CC,
	C_SEL    = 0x00CFE0FF, C_SB    = 0x00C4C4CC, C_THUMB = 0x009098A8,
	C_HEAD   = 0x00101015, C_DIM   = 0x00707078,
};

// ---- window / font ----------------------------------------------------------
static unsigned *CAN = 0;
static int        W = 0, H = 0;
static int        FW = 8, FH = 16;	// font cell

static int  NAV_H;			// navbar height
static const int SBW = 12;		// scrollbar width
static const int MARGIN = 6;

// ---------------------------------------------------------------------------
// Drawing helpers (write straight into the canvas, then kapi_present once).
// ---------------------------------------------------------------------------
static void fillrect (int x, int y, int w, int h, unsigned c)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > W) w = W - x;
	if (y + h > H) h = H - y;
	for (int j = 0; j < h; j++) {
		unsigned *row = CAN + (long) (y + j) * W + x;
		for (int i = 0; i < w; i++) row[i] = c;
	}
}
static void frame (int x, int y, int w, int h, unsigned c)
{
	fillrect (x, y, w, 1, c); fillrect (x, y + h - 1, w, 1, c);
	fillrect (x, y, 1, h, c); fillrect (x + w - 1, y, 1, h, c);
}
static void drawtext (int x, int y, const char *s, unsigned c)
{
	kapi_draw_text_buf (CAN, W, H, x, y, s, c);
}
// draw n bytes of s (not necessarily NUL-terminated)
static void drawtextn (int x, int y, const char *s, int n, unsigned c)
{
	char t[512];
	if (n > (int) sizeof t - 1) n = sizeof t - 1;
	memcpy (t, s, n); t[n] = '\0';
	drawtext (x, y, t, c);
}

// ---------------------------------------------------------------------------
// Page model: parsed items (words / breaks / form controls), links, controls.
// ---------------------------------------------------------------------------
enum { IT_WORD = 0, IT_BR = 1, IT_PARA = 2, IT_CTRL = 3 };
struct Item { unsigned char kind; int off, len, link, ctrl; };

enum { CT_TEXT = 0, CT_CHECK, CT_SELECT, CT_SUBMIT, CT_BUTTON };
struct Opt  { char label[64]; char value[64]; };
struct Ctrl {
	int  type;
	char name[64];
	char value[256];		// text value, or button label
	int  checked;
	Opt *opts; int nopts; int optcap; int sel;
	int  rx, ry, rw, rh;		// last on-screen rect (screen coords)
};

static char *g_norm = 0;  static int g_norm_len = 0, g_norm_cap = 0;
static Item *g_item = 0;  static int g_nitem = 0,    g_item_cap = 0;
static char (*g_link)[768] = 0; static int g_nlink = 0, g_link_cap = 0;
static Ctrl *g_ctrl = 0;  static int g_nctrl = 0,    g_ctrl_cap = 0;

static char g_form_action[1024];
static int  g_form_post = 0;

static char g_url[1024]   = "about:start";	// current address-bar text
static char g_base[1024]  = "";			// current page URL (for relative links)
static int  g_doc_h = 0;
static int  g_scroll = 0;
static char g_status[160] = "";

// dynamic-array growth (malloc/realloc; this app has newlib but avoids operator new)
static void *grow (void *p, int need, int *cap, int elem)
{
	if (need <= *cap) return p;
	int nc = *cap ? *cap * 2 : 64;
	while (nc < need) nc *= 2;
	void *np = realloc (p, (size_t) nc * elem);
	if (np) *cap = nc;
	return np ? np : p;
}

static void model_reset (void)
{
	g_norm_len = 0; g_nitem = 0; g_nlink = 0;
	for (int i = 0; i < g_nctrl; i++) free (g_ctrl[i].opts);
	g_nctrl = 0; g_form_action[0] = '\0'; g_form_post = 0;
}

static int norm_put (const char *s, int n)	// append n bytes to g_norm, return offset
{
	g_norm = (char *) grow (g_norm, g_norm_len + n + 1, &g_norm_cap, 1);
	int off = g_norm_len;
	memcpy (g_norm + off, s, n); g_norm_len += n; g_norm[g_norm_len] = '\0';
	return off;
}
static void emit (unsigned char kind, int off, int len, int link, int ctrl)
{
	g_item = (Item *) grow (g_item, g_nitem + 1, &g_item_cap, sizeof (Item));
	Item &it = g_item[g_nitem++];
	it.kind = kind; it.off = off; it.len = len; it.link = link; it.ctrl = ctrl;
}

// ---------------------------------------------------------------------------
// URL resolution: resolve `href` against the current page `base`.
// ---------------------------------------------------------------------------
static int starts_ci (const char *s, const char *p)
{
	for (int i = 0; p[i]; i++) { char a = s[i], b = p[i];
		if (a >= 'A' && a <= 'Z') a += 32; if (b >= 'A' && b <= 'Z') b += 32;
		if (a != b) return 0; }
	return 1;
}
static void resolve_url (const char *base, const char *href, char *out, int cap)
{
	out[0] = '\0';
	if (!href || !href[0]) { strncpy (out, base, cap - 1); out[cap-1]='\0'; return; }
	if (href[0] == '#') {				// same-page fragment -> base
		strncpy (out, base, cap - 1); out[cap-1]='\0'; return; }
	if (starts_ci (href, "http://") || starts_ci (href, "https://")) {
		strncpy (out, href, cap - 1); out[cap-1]='\0'; return; }
	// split base into scheme://host  and  path
	char scheme_host[768]; scheme_host[0]='\0';
	const char *p = base;
	if (starts_ci (p, "http://"))  p += 7; else if (starts_ci (p, "https://")) p += 8;
	const char *hs = p; while (*p && *p != '/') p++;	// p at first '/' of path or end
	int shlen = (int) (p - base);
	if (shlen > (int) sizeof scheme_host - 1) shlen = sizeof scheme_host - 1;
	memcpy (scheme_host, base, shlen); scheme_host[shlen] = '\0';
	(void) hs;
	if (href[0] == '/') {				// root-relative
		int n = snprintf (out, cap, "%s%s", scheme_host, href); (void) n; return;
	}
	// relative to the base directory
	char dir[1024]; int dl = 0;
	const char *path = base + shlen;		// starts with '/' or empty
	if (!*path) { dir[dl++]='/'; }
	else {
		int last = 0; for (int i = 0; path[i]; i++) if (path[i]=='/') last = i;
		for (int i = 0; i <= last && dl < (int) sizeof dir - 1; i++) dir[dl++] = path[i];
	}
	dir[dl] = '\0';
	snprintf (out, cap, "%s%s%s", scheme_host, dir, href);
}

// ---------------------------------------------------------------------------
// HTML helpers: tag attribute extraction + entity decode.
// ---------------------------------------------------------------------------
// Find attribute `name` inside tag-body [t, t+tn); copy value to out[cap]. Returns 1/0.
static int attr (const char *t, int tn, const char *name, char *out, int cap)
{
	int nl = (int) strlen (name);
	for (int i = 0; i + nl < tn; i++) {
		if ((i == 0 || t[i-1] == ' ' || t[i-1] == '\t' || t[i-1]=='\n')) {
			int j = 0; while (j < nl && (t[i+j]|32) == (name[j]|32)) j++;
			if (j == nl) {
				int k = i + nl; while (k < tn && (t[k]==' '||t[k]=='\t')) k++;
				if (k < tn && t[k] == '=') {
					k++; while (k < tn && (t[k]==' '||t[k]=='\t')) k++;
					char q = 0; if (k < tn && (t[k]=='"'||t[k]=='\'')) q = t[k++];
					int o = 0;
					while (k < tn && o < cap - 1) {
						if (q && t[k]==q) break;
						if (!q && (t[k]==' '||t[k]=='\t'||t[k]=='>')) break;
						out[o++] = t[k++];
					}
					out[o] = '\0';
					return 1;
				}
			}
		}
	}
	out[0] = '\0';
	return 0;
}
// Decode one entity starting at s[*i] ('&'...); append decoded bytes to norm; advance *i.
static void decode_entity (const char *s, int n, int *i, char *dst, int *dn, int dcap)
{
	int j = *i + 1, code = 0;
	char name[12]; int nl = 0;
	if (j < n && s[j] == '#') {
		j++; int hex = 0;
		if (j < n && (s[j]=='x'||s[j]=='X')) { hex = 1; j++; }
		while (j < n && s[j] != ';' && nl < 10) {
			char c = s[j];
			if (hex) { if (c>='0'&&c<='9') code=code*16+(c-'0');
				else if ((c|32)>='a'&&(c|32)<='f') code=code*16+((c|32)-'a'+10); else break; }
			else { if (c>='0'&&c<='9') code=code*10+(c-'0'); else break; }
			j++;
		}
		if (j < n && s[j]==';') j++;
		// emit code as UTF-8 (only need ASCII + a couple of common ones here)
		if (code < 128 && code > 0) { if (*dn < dcap-1) dst[(*dn)++] = (char) code; }
		else if (*dn < dcap-1) dst[(*dn)++] = '?';
		*i = j; return;
	}
	while (j < n && s[j] != ';' && nl < 10 &&
	       ((s[j]>='a'&&s[j]<='z')||(s[j]>='A'&&s[j]<='Z'))) name[nl++] = s[j++];
	name[nl] = '\0';
	if (j < n && s[j]==';') j++;
	char out = 0;
	if (!strcmp (name,"amp")) out='&'; else if (!strcmp(name,"lt")) out='<';
	else if (!strcmp(name,"gt")) out='>'; else if (!strcmp(name,"quot")) out='"';
	else if (!strcmp(name,"apos")) out='\''; else if (!strcmp(name,"nbsp")) out=' ';
	else out = 0;
	if (out && *dn < dcap-1) dst[(*dn)++] = out;
	else if (!out) {		// unknown entity: keep literally
		if (*dn < dcap-1) dst[(*dn)++] = '&';
		for (int k=0;k<nl && *dn<dcap-1;k++) dst[(*dn)++] = name[k];
	}
	*i = j;
}

// ---------------------------------------------------------------------------
// HTML parser -> items / links / controls.
// ---------------------------------------------------------------------------
static int new_ctrl (int type)
{
	g_ctrl = (Ctrl *) grow (g_ctrl, g_nctrl + 1, &g_ctrl_cap, sizeof (Ctrl));
	Ctrl &c = g_ctrl[g_nctrl];
	memset (&c, 0, sizeof c); c.type = type; c.sel = 0;
	return g_nctrl++;
}
static int tag_is (const char *t, int tn, const char *name)
{
	int nl = (int) strlen (name);
	if (tn < nl) return 0;
	for (int i = 0; i < nl; i++) if ((t[i]|32) != (name[i]|32)) return 0;
	return tn == nl || t[nl]==' ' || t[nl]=='\t' || t[nl]=='\n' || t[nl]=='/' || t[nl]=='>';
}

static void parse_html (const char *h, int n)
{
	model_reset ();
	int inLink = -1, curSelect = -1, curOpt = -1, skip = 0;	// skip = script/style/textarea
	char skname[12] = "";
	int  pendingBreak = 1;				// suppress leading blank

	int i = 0;
	while (i < n) {
		if (h[i] == '<') {
			int s = i + 1;
			int close = 0; if (s < n && h[s] == '/') { close = 1; s++; }
			int e = s; while (e < n && h[e] != '>') e++;	// tag body [s,e)
			const char *t = h + s; int tn = e - s;
			i = (e < n) ? e + 1 : n;

			if (skip) {				// only the matching close tag ends a skip
				if (close && tag_is (t, tn, skname)) { skip = 0; skname[0]='\0'; }
				continue;
			}
			if (!close && (tag_is (t,tn,"script") || tag_is (t,tn,"style"))) {
				skip = 1; strcpy (skname, tag_is (t,tn,"script") ? "script" : "style");
				continue;
			}
			if (curSelect >= 0) {			// inside <select>: collect <option>s
				if (close && tag_is (t,tn,"select")) { curSelect = -1; curOpt = -1; continue; }
				if (!close && tag_is (t,tn,"option")) {
					Ctrl &c = g_ctrl[curSelect];
					c.opts = (Opt *) grow (c.opts, c.nopts+1, &c.optcap, sizeof (Opt));
					Opt &op = c.opts[c.nopts]; op.label[0]='\0'; op.value[0]='\0';
					attr (t,tn,"value",op.value,sizeof op.value);
					curOpt = c.nopts++;
					continue;
				}
				if (close && tag_is (t,tn,"option")) { curOpt = -1; continue; }
				continue;			// ignore other tags inside <select>
			}

			if (tag_is (t,tn,"br")) { emit (IT_BR,0,0,-1,-1); pendingBreak=0; }
			else if (tag_is(t,tn,"p")||tag_is(t,tn,"div")||tag_is(t,tn,"section")||
			         tag_is(t,tn,"article")||tag_is(t,tn,"header")||tag_is(t,tn,"footer")||
			         tag_is(t,tn,"ul")||tag_is(t,tn,"ol")||tag_is(t,tn,"table")||
			         tag_is(t,tn,"form")||tag_is(t,tn,"blockquote")||
			         tag_is(t,tn,"pre")||tag_is(t,tn,"hr")) {
				if (!pendingBreak) { emit (IT_PARA,0,0,-1,-1); pendingBreak=1; }
			}
			else if (tag_is(t,tn,"li")||tag_is(t,tn,"tr")) { emit (IT_BR,0,0,-1,-1); }
			else if (tag_is(t,tn,"h1")||tag_is(t,tn,"h2")||tag_is(t,tn,"h3")||
			         tag_is(t,tn,"h4")||tag_is(t,tn,"h5")||tag_is(t,tn,"h6")) {
				if (!pendingBreak) { emit (IT_PARA,0,0,-1,-1); pendingBreak=1; }
			}
			else if (!close && tag_is (t,tn,"a")) {
				char href[768]; attr (t,tn,"href",href,sizeof href);
				if (href[0]) {
					g_link = (char (*)[768]) grow (g_link, g_nlink+1, &g_link_cap, 768);
					resolve_url (g_base, href, g_link[g_nlink], 768);
					inLink = g_nlink++;
				}
			}
			else if (close && tag_is (t,tn,"a")) inLink = -1;
			else if (!close && tag_is (t,tn,"form")) {
				char act[768]; attr (t,tn,"action",act,sizeof act);
				resolve_url (g_base, act[0]?act:g_base, g_form_action, sizeof g_form_action);
				char m[8]; attr (t,tn,"method",m,sizeof m);
				g_form_post = (m[0] && (m[0]|32)=='p');
			}
			else if (!close && tag_is (t,tn,"input")) {
				char ty[24]; attr (t,tn,"type",ty,sizeof ty);
				for (char *q=ty;*q;q++) *q |= 32;
				int kind = CT_TEXT;
				if (!strcmp(ty,"checkbox")||!strcmp(ty,"radio")) kind = CT_CHECK;
				else if (!strcmp(ty,"submit")||!strcmp(ty,"image")) kind = CT_SUBMIT;
				else if (!strcmp(ty,"button")) kind = CT_BUTTON;
				else if (!strcmp(ty,"hidden")) kind = -1;
				if (kind == -1) continue;	// hidden: skip (kept simple)
				int ci = new_ctrl (kind);
				attr (t,tn,"name",g_ctrl[ci].name,sizeof g_ctrl[ci].name);
				attr (t,tn,"value",g_ctrl[ci].value,sizeof g_ctrl[ci].value);
				if (kind==CT_SUBMIT && !g_ctrl[ci].value[0]) strcpy(g_ctrl[ci].value,"Submit");
				char ch[8]; g_ctrl[ci].checked = attr (t,tn,"checked",ch,sizeof ch);
				emit (IT_CTRL,0,0,-1,ci); pendingBreak=0;
			}
			else if (!close && tag_is (t,tn,"select")) {
				int ci = new_ctrl (CT_SELECT);
				attr (t,tn,"name",g_ctrl[ci].name,sizeof g_ctrl[ci].name);
				emit (IT_CTRL,0,0,-1,ci); pendingBreak=0;
				curSelect = ci;
			}
			else if (!close && tag_is (t,tn,"textarea")) {
				int ci = new_ctrl (CT_TEXT);
				attr (t,tn,"name",g_ctrl[ci].name,sizeof g_ctrl[ci].name);
				emit (IT_CTRL,0,0,-1,ci); pendingBreak=0;
				skip = 1; strcpy (skname, "textarea");
			}
			else if (!close && tag_is (t,tn,"button")) {
				int ci = new_ctrl (CT_SUBMIT);
				attr (t,tn,"name",g_ctrl[ci].name,sizeof g_ctrl[ci].name);
				strcpy (g_ctrl[ci].value, "Submit");
				emit (IT_CTRL,0,0,-1,ci); pendingBreak=0;
			}
			// all other tags: ignored
			continue;
		}
		// ---- text run ----
		if (curSelect >= 0) {			// route text to the current <option> label
			char buf[80]; int bn = 0;
			while (i < n && h[i] != '<') {
				char c = h[i];
				if (c=='&') { decode_entity (h,n,&i,buf,&bn,(int)sizeof buf); continue; }
				if (bn < (int)sizeof buf-1) buf[bn++] = c;
				i++;
			}
			if (curOpt >= 0 && curSelect < g_nctrl) {
				int s2=0; while (s2<bn && (buf[s2]==' '||buf[s2]=='\t'||buf[s2]=='\n'||buf[s2]=='\r')) s2++;
				int e2=bn; while (e2>s2 && (buf[e2-1]==' '||buf[e2-1]=='\t'||buf[e2-1]=='\n'||buf[e2-1]=='\r')) e2--;
				Opt &op = g_ctrl[curSelect].opts[curOpt];
				int ln = e2-s2; if (ln > (int)sizeof op.label-1) ln = sizeof op.label-1;
				if (ln > 0) { memcpy (op.label, buf+s2, ln); op.label[ln]='\0';
					if (!op.value[0]) { strncpy (op.value, op.label, sizeof op.value-1);
						op.value[sizeof op.value-1]='\0'; } }
			}
			continue;
		}
		char word[256]; int wn = 0;
		while (i < n && h[i] != '<') {
			char c = h[i];
			if (c==' '||c=='\t'||c=='\r'||c=='\n') {
				if (wn) { int off = norm_put (word, wn);
					emit (IT_WORD, off, wn, inLink, -1); wn = 0; pendingBreak = 0; }
				i++;
				continue;
			}
			if (c=='&') { decode_entity (h, n, &i, word, &wn, (int) sizeof word); continue; }
			if (wn < (int) sizeof word - 1) word[wn++] = c;
			i++;
		}
		if (wn) { int off = norm_put (word, wn);
			emit (IT_WORD, off, wn, inLink, -1); wn = 0; pendingBreak = 0; }
	}
}

// ---------------------------------------------------------------------------
// Click targets recorded during layout/draw (screen coordinates).
// ---------------------------------------------------------------------------
struct LR { int x, y, w, h, link; };
static LR  g_lr[6000]; static int g_nlr = 0;

// navbar hit rects
static int BK_X, FW_X, GO_X, AD_X, AD_W, BTN_W;

// focus: -2 = address bar, -1 = none, >=0 = control index
static int g_focus = -2;
static int g_addrcur = 0;	// cursor in address bar
static int g_openSel = -1;	// select control whose popup is open (or -1)

// ---------------------------------------------------------------------------
// Layout + draw. Returns total document height (for the scrollbar).
// ---------------------------------------------------------------------------
static void draw_ctrl (Ctrl &c, int x, int y, int contentTop, int contentBot)
{
	int vis = (y + c.rh > contentTop && y < contentBot);
	c.rx = x; c.ry = y; (void) vis;
	if (c.type == CT_TEXT) {
		c.rw = 18 * FW; c.rh = FH + 4;
		if (y + c.rh > contentTop && y < contentBot) {
			fillrect (x, y, c.rw, c.rh, C_FIELD);
			frame (x, y, c.rw, c.rh, (g_focus==(&c-g_ctrl))?C_FOCUS:C_BORDER);
			int maxch = (c.rw - 6) / FW; const char *v = c.value;
			int vl = (int) strlen (v); int st = vl > maxch ? vl - maxch : 0;
			drawtext (x+3, y+2, v+st, C_TEXT);
		}
	} else if (c.type == CT_CHECK) {
		c.rw = FH; c.rh = FH;
		if (y + c.rh > contentTop && y < contentBot) {
			fillrect (x, y, c.rw, c.rh, C_FIELD); frame (x, y, c.rw, c.rh, C_BORDER);
			if (c.checked) { drawtext (x+2, y+1, "x", C_TEXT); }
		}
	} else if (c.type == CT_SELECT) {
		const char *lbl = c.nopts ? c.opts[c.sel].label : "(choose)";
		int lw = (int) strlen (lbl) * FW; c.rw = lw + 2*FW + 10; if (c.rw < 8*FW) c.rw = 8*FW;
		c.rh = FH + 4;
		if (y + c.rh > contentTop && y < contentBot) {
			fillrect (x, y, c.rw, c.rh, C_FIELD); frame (x, y, c.rw, c.rh, C_BORDER);
			drawtext (x+3, y+2, lbl, C_TEXT);
			drawtext (x + c.rw - FW - 3, y+2, "v", C_DIM);
		}
	} else { // SUBMIT / BUTTON
		int lw = (int) strlen (c.value) * FW; c.rw = lw + 12; c.rh = FH + 4;
		if (y + c.rh > contentTop && y < contentBot) {
			fillrect (x, y, c.rw, c.rh, C_BTN); frame (x, y, c.rw, c.rh, C_BORDER);
			drawtext (x+6, y+2, c.value, C_HEAD);
		}
	}
}

static void render (void)
{
	// background
	fillrect (0, 0, W, H, C_BG);

	// ---- navbar ----
	fillrect (0, 0, W, NAV_H, C_NAV);
	fillrect (0, NAV_H-1, W, 1, C_BORDER);
	BTN_W = 3 * FW + 8;
	int bx = MARGIN, by = (NAV_H - (FH+6)) / 2, bh = FH + 6;
	BK_X = bx;
	fillrect (bx, by, BTN_W, bh, C_BTN); frame (bx, by, BTN_W, bh, C_BORDER);
	drawtext (bx + (BTN_W-FW)/2, by+3, "<", C_HEAD); bx += BTN_W + 4;
	FW_X = bx;
	fillrect (bx, by, BTN_W, bh, C_BTN); frame (bx, by, BTN_W, bh, C_BORDER);
	drawtext (bx + (BTN_W-FW)/2, by+3, ">", C_HEAD); bx += BTN_W + 8;
	// Go button at the right
	int gx = W - MARGIN - (2*FW+12);
	GO_X = gx;
	// address bar between bx and gx
	AD_X = bx; AD_W = gx - 8 - bx;
	fillrect (AD_X, by, AD_W, bh, C_FIELD);
	frame (AD_X, by, AD_W, bh, (g_focus==-2)?C_FOCUS:C_BORDER);
	{
		int maxch = (AD_W - 8) / FW; int ul = (int) strlen (g_url);
		int st = ul > maxch ? ul - maxch : 0;
		drawtext (AD_X+4, by+3, g_url+st, C_TEXT);
		if (g_focus == -2) {	// caret
			int cx = AD_X+4 + (g_addrcur - st) * FW;
			if (cx >= AD_X+2 && cx < AD_X+AD_W-2) fillrect (cx, by+2, 1, FH, C_TEXT);
		}
	}
	fillrect (gx, by, 2*FW+12, bh, C_BTN); frame (gx, by, 2*FW+12, bh, C_BORDER);
	drawtext (gx+6, by+3, "Go", C_HEAD);

	// ---- content area ----
	int top = NAV_H, bot = H;
	int CW = W - SBW - 2*MARGIN;		// text wrap width
	int x = MARGIN, y = top + MARGIN - g_scroll;
	int lineH = FH + 3;
	g_nlr = 0;

	for (int k = 0; k < g_nitem; k++) {
		Item &it = g_item[k];
		if (it.kind == IT_BR)  { x = MARGIN; y += lineH; continue; }
		if (it.kind == IT_PARA){ x = MARGIN; y += lineH + (FH/2 + 2); continue; }
		if (it.kind == IT_CTRL) {
			Ctrl &c = g_ctrl[it.ctrl];
			// measure (rw) by a dry call height; compute width quickly
			int probew = (c.type==CT_CHECK)?FH : (c.type==CT_TEXT)?18*FW :
			             (c.type==CT_SELECT)? ( (c.nopts?(int)strlen(c.opts[c.sel].label):8)*FW + 2*FW+10 ) :
			             ((int)strlen(c.value)*FW + 12);
			if (x + probew > MARGIN + CW) { x = MARGIN; y += lineH; }
			draw_ctrl (c, x, y, top, bot);
			// record as a "link" target with id encoded as control: use link=-(ctrl+2)
			if (y + c.rh > top && y < bot && g_nlr < (int) (sizeof g_lr/sizeof g_lr[0]))
				g_lr[g_nlr++] = { x, y, c.rw, c.rh, -(it.ctrl + 2) };
			x += c.rw + FW; y += 0;
			// keep the line tall enough
			continue;
		}
		// WORD
		int wpx = it.len * FW;
		if (x + wpx > MARGIN + CW && x > MARGIN) { x = MARGIN; y += lineH; }
		if (y + FH > top && y < bot) {
			unsigned col = (it.link >= 0) ? C_LINK : C_TEXT;
			drawtextn (x, y, g_norm + it.off, it.len, col);
			if (it.link >= 0) {
				fillrect (x, y + FH - 1, wpx, 1, C_LINK);	// underline
				if (g_nlr < (int)(sizeof g_lr/sizeof g_lr[0]))
					g_lr[g_nlr++] = { x, y, wpx, FH, it.link };
			}
		}
		x += wpx + FW;
	}
	g_doc_h = (y + g_scroll) - (top + MARGIN) + lineH;

	// ---- scrollbar ----
	int trackH = H - top;
	fillrect (W - SBW, top, SBW, trackH, C_SB);
	if (g_doc_h > trackH) {
		int th = trackH * trackH / g_doc_h; if (th < 20) th = 20;
		int ty = top + (g_scroll * (trackH - th)) / (g_doc_h - trackH);
		if (ty < top) ty = top; if (ty + th > H) ty = H - th;
		fillrect (W - SBW + 2, ty, SBW - 4, th, C_THUMB);
	}

	// ---- open dropdown popup (drawn last, on top) ----
	if (g_openSel >= 0 && g_openSel < g_nctrl) {
		Ctrl &c = g_ctrl[g_openSel];
		int px = c.rx, py = c.ry + c.rh, pw = c.rw < 12*FW ? 12*FW : c.rw;
		int ph = c.nopts * (FH+2) + 2;
		fillrect (px, py, pw, ph, C_FIELD); frame (px, py, pw, ph, C_FOCUS);
		for (int o = 0; o < c.nopts; o++) {
			int oy = py + 1 + o * (FH+2);
			if (o == c.sel) fillrect (px+1, oy, pw-2, FH+2, C_SEL);
			drawtext (px+4, oy+1, c.opts[o].label, C_TEXT);
		}
	}

	// ---- status line over the navbar bottom (brief) ----
	if (g_status[0]) {
		int sy = NAV_H - FH - 1;
		// (status is shown in the title area only if it fits; keep it subtle)
		(void) sy;
	}

	kapi_present ();
}

// ---------------------------------------------------------------------------
// Fetch + navigate.
// ---------------------------------------------------------------------------
static char *g_resp = 0;		// response buffer (malloc)
static const int RESP_CAP = 700 * 1024;

static void show_message (const char *title, const char *msg)
{
	model_reset ();
	int o = norm_put (title, (int) strlen (title)); emit (IT_WORD,o,(int)strlen(title),-1,-1);
	emit (IT_PARA,0,0,-1,-1);
	// split msg into words
	const char *p = msg;
	while (*p) {
		while (*p==' ') p++;
		const char *s = p; while (*p && *p!=' ') p++;
		if (p>s) { int off = norm_put (s, (int)(p-s)); emit (IT_WORD,off,(int)(p-s),-1,-1); }
	}
	g_scroll = 0;
}

static void load_url (const char *url)
{
	if (starts_ci (url, "about:")) {
		strncpy (g_base, url, sizeof g_base - 1); g_base[sizeof g_base-1]='\0';
		strncpy (g_url, url, sizeof g_url - 1);
		show_message ("httpc", "Onyx text browser. Type a URL (http:// or https://) and press Go, or click a link.");
		g_addrcur = (int) strlen (g_url);
		return;
	}
	strncpy (g_status, "Loading...", sizeof g_status-1);
	render ();		// show the address bar update immediately

	HttpClient c;
	c.user_agent ("httpc/1.0 (Onyx)");
	HttpResponse r = c.get (url, g_resp, RESP_CAP);

	strncpy (g_base, url, sizeof g_base - 1); g_base[sizeof g_base-1]='\0';
	strncpy (g_url, url, sizeof g_url - 1);    g_url[sizeof g_url-1]='\0';
	g_addrcur = (int) strlen (g_url);
	g_scroll = 0; g_focus = -1; g_openSel = -1;

	if (r.is_error ()) {
		const char *m = "Could not load the page.";
		switch (r.status) {
		case HTTP_ERR_BAD_URL: m="Bad URL."; break;
		case HTTP_ERR_HTTPS:   m="HTTPS not available in this build."; break;
		case HTTP_ERR_NO_NET:  m="Network link is down."; break;
		case HTTP_ERR_CONNECT: m="Connection failed (DNS/TCP)."; break;
		case HTTP_ERR_TIMEOUT: m="The server did not respond."; break;
		case HTTP_ERR_TLS:     m="TLS handshake failed."; break;
		case HTTP_ERR_EMPTY:   m="Empty response."; break;
		}
		show_message ("Error", m);
		return;
	}
	if (r.status >= 300 && r.status < 400) {	// redirect
		char loc[1024];
		if (r.header ("Location", loc, sizeof loc) > 0) {
			char abs[1024]; resolve_url (url, loc, abs, sizeof abs);
			load_url (abs);
			return;
		}
	}
	// content-type: only parse text/* as HTML; otherwise show a note
	char ct[96]; r.header ("Content-Type", ct, sizeof ct);
	parse_html (r.body, r.body_len);
	g_status[0] = '\0';
}

// ---------------------------------------------------------------------------
// History.
// ---------------------------------------------------------------------------
static char g_hist[64][1024]; static int g_hn = 0, g_hi = -1;
static void navigate (const char *url)
{
	load_url (url);
	if (g_hi >= 0 && !strcmp (g_hist[g_hi], g_url)) return;	// same page
	g_hn = g_hi + 1;
	if (g_hn >= 64) { memmove (g_hist[0], g_hist[1], 63*1024); g_hn = 63; g_hi--; }
	strncpy (g_hist[g_hn], g_url, 1023); g_hist[g_hn][1023]='\0';
	g_hi = g_hn; g_hn++;
}
static void go_back (void)    { if (g_hi > 0)        { g_hi--; load_url (g_hist[g_hi]); } }
static void go_forward (void) { if (g_hi < g_hn - 1) { g_hi++; load_url (g_hist[g_hi]); } }

// ---------------------------------------------------------------------------
// Form submit: gather this page's controls -> query string -> GET/POST.
// ---------------------------------------------------------------------------
static void url_enc (const char *s, char *out, int *o, int cap)
{
	const char *hex = "0123456789ABCDEF";
	for (; *s && *o + 3 < cap; s++) {
		unsigned char c = (unsigned char) *s;
		if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')
			out[(*o)++] = c;
		else if (c==' ') out[(*o)++]='+';
		else { out[(*o)++]='%'; out[(*o)++]=hex[c>>4]; out[(*o)++]=hex[c&15]; }
	}
}
static void submit_form (void)
{
	if (!g_form_action[0]) return;
	char qs[2048]; int o = 0; int first = 1;
	for (int i = 0; i < g_nctrl; i++) {
		Ctrl &c = g_ctrl[i];
		if (!c.name[0]) continue;
		const char *val = 0; char vb[64];
		if (c.type == CT_TEXT) val = c.value;
		else if (c.type == CT_CHECK) { if (!c.checked) continue; val = c.value[0]?c.value:"on"; }
		else if (c.type == CT_SELECT) { val = c.nopts ? c.opts[c.sel].value : ""; (void)vb; }
		else continue;
		if (!first && o < (int)sizeof qs-1) qs[o++]='&'; first = 0;
		url_enc (c.name, qs, &o, sizeof qs);
		if (o < (int)sizeof qs-1) qs[o++]='=';
		url_enc (val, qs, &o, sizeof qs);
	}
	qs[o] = '\0';

	if (g_form_post) {
		strncpy (g_status, "Loading...", sizeof g_status-1); render ();
		HttpClient c; c.user_agent ("httpc/1.0 (Onyx)");
		HttpResponse r = c.post (g_form_action, "application/x-www-form-urlencoded",
		                         qs, o, g_resp, RESP_CAP);
		strncpy (g_base, g_form_action, sizeof g_base-1);
		strncpy (g_url, g_form_action, sizeof g_url-1); g_addrcur=(int)strlen(g_url);
		g_scroll=0; g_focus=-1; g_openSel=-1;
		if (r.is_error ()) show_message ("Error", "Form POST failed.");
		else parse_html (r.body, r.body_len);
		// record in history
		g_hn = g_hi+1; strncpy(g_hist[g_hn],g_url,1023); g_hi=g_hn; g_hn++;
		g_status[0]='\0';
	} else {
		char url[2048];
		snprintf (url, sizeof url, "%s%s%s", g_form_action,
		          strchr (g_form_action,'?')?"&":"?", qs);
		navigate (url);
	}
}

// ---------------------------------------------------------------------------
// Input: a tiny event queue filled by the kapi handlers, drained by the loop.
// ---------------------------------------------------------------------------
static volatile int g_clk_have = 0, g_clk_x, g_clk_y, g_clk_btn;
static void on_pointer (unsigned long sender, int event, long value)
{
	(void) sender;
	if (event == GUI_EVENT_PTR_DOWN) {
		g_clk_x = GUI_PTR_X (value); g_clk_y = GUI_PTR_Y (value);
		g_clk_btn = GUI_PTR_CHANGED (value); g_clk_have = 1;
	}
}
#define KQ 64
static volatile int g_kq[KQ]; static volatile int g_kh = 0, g_kt = 0;
static void on_key (unsigned long sender, int event, long value)
{
	(void) sender;
	if (event != GUI_EVENT_KEY) return;
	int nh = (g_kh + 1) % KQ; if (nh == g_kt) return;
	g_kq[g_kh] = (int) value; g_kh = nh;
}

static void insert_text (char *buf, int cap, int *cur, int ch)
{
	int n = (int) strlen (buf);
	if (n + 1 >= cap) return;
	for (int i = n; i > *cur; i--) buf[i] = buf[i-1];
	buf[*cur] = (char) ch; buf[n+1] = '\0'; (*cur)++;
}
static void backspace_text (char *buf, int *cur)
{
	if (*cur <= 0) return;
	int n = (int) strlen (buf);
	for (int i = *cur - 1; i < n; i++) buf[i] = buf[i+1];
	(*cur)--;
}

static void handle_click (int mx, int my)
{
	int top = NAV_H;
	// navbar?
	int by = (NAV_H - (FH+6)) / 2, bh = FH + 6;
	if (my < NAV_H) {
		if (my >= by && my < by+bh) {
			if (mx >= BK_X && mx < BK_X+BTN_W) { go_back (); return; }
			if (mx >= FW_X && mx < FW_X+BTN_W) { go_forward (); return; }
			if (mx >= GO_X && mx < GO_X+2*FW+12) { navigate (g_url); return; }
			if (mx >= AD_X && mx < AD_X+AD_W) {
				g_focus = -2; g_addrcur = (int) strlen (g_url); return;
			}
		}
		return;
	}
	// open dropdown popup has priority
	if (g_openSel >= 0 && g_openSel < g_nctrl) {
		Ctrl &c = g_ctrl[g_openSel];
		int px = c.rx, py = c.ry + c.rh, pw = c.rw < 12*FW ? 12*FW : c.rw;
		for (int o = 0; o < c.nopts; o++) {
			int oy = py + 1 + o*(FH+2);
			if (mx>=px && mx<px+pw && my>=oy && my<oy+FH+2) { c.sel = o; g_openSel=-1; return; }
		}
		g_openSel = -1; return;
	}
	// scrollbar?
	if (mx >= W - SBW) {
		int trackH = H - top;
		if (g_doc_h > trackH) {
			int rel = my - top;
			g_scroll = rel * (g_doc_h - trackH) / trackH;
			if (g_scroll < 0) g_scroll = 0;
			if (g_scroll > g_doc_h - trackH) g_scroll = g_doc_h - trackH;
		}
		return;
	}
	// content: hit-test recorded rects
	for (int i = 0; i < g_nlr; i++) {
		LR &r = g_lr[i];
		if (mx >= r.x && mx < r.x+r.w && my >= r.y && my < r.y+r.h) {
			if (r.link >= 0) { navigate (g_link[r.link]); return; }
			int ci = -(r.link) - 2;			// control
			if (ci < 0 || ci >= g_nctrl) return;
			Ctrl &c = g_ctrl[ci];
			if (c.type == CT_CHECK)  { c.checked = !c.checked; return; }
			if (c.type == CT_SELECT) { g_openSel = (g_openSel==ci)?-1:ci; return; }
			if (c.type == CT_TEXT)   { g_focus = ci; return; }
			if (c.type == CT_SUBMIT) { submit_form (); return; }
			return;
		}
	}
	g_focus = -1;	// click on empty space defocuses
}

static void handle_key (int k)
{
	int top = NAV_H, pageStep = (H - top) - 2*FH;
	if (g_focus == -2) {				// address bar editing
		if (k == KEY_ENTER) { g_focus=-1; navigate (g_url); return; }
		if (k == KEY_BACKSPACE) { backspace_text (g_url, &g_addrcur); return; }
		if (k == KEY_LEFT)  { if (g_addrcur>0) g_addrcur--; return; }
		if (k == KEY_RIGHT) { if (g_addrcur<(int)strlen(g_url)) g_addrcur++; return; }
		if (k >= 32 && k < 127) { insert_text (g_url, sizeof g_url, &g_addrcur, k); return; }
		return;
	}
	if (g_focus >= 0 && g_focus < g_nctrl && g_ctrl[g_focus].type == CT_TEXT) {
		Ctrl &c = g_ctrl[g_focus];
		int cur = (int) strlen (c.value);
		if (k == KEY_ENTER) { submit_form (); return; }
		if (k == KEY_BACKSPACE) { if (cur>0) c.value[cur-1]='\0'; return; }
		if (k >= 32 && k < 127 && cur+1 < (int)sizeof c.value) { c.value[cur]=(char)k; c.value[cur+1]='\0'; return; }
		return;
	}
	// no focus -> scrolling keys
	if (k == KEY_DOWN)  g_scroll += FH;
	else if (k == KEY_UP) g_scroll -= FH;
	else if (k == KEY_PGDN || k == ' ') g_scroll += pageStep;
	else if (k == KEY_PGUP) g_scroll -= pageStep;
	else if (k == KEY_HOME) g_scroll = 0;
	int top2 = H - NAV_H;
	if (g_scroll > g_doc_h - top2) g_scroll = g_doc_h - top2;
	if (g_scroll < 0) g_scroll = 0;
}

int main (void)
{
	kapi_screen_size (&W, &H);
	if (W <= 0 || W > 1920) W = 800;
	if (H <= 0 || H > 1080) H = 600;
	W -= 60; H -= 80;			// leave room for desktop chrome / panel
	if (W > 1000) W = 1000;
	if (H > 720)  H = 720;

	CAN = kapi_create_window (W, H, "httpc");
	if (!CAN) return 1;
	FW = kapi_font_width (); FH = kapi_font_height ();
	if (FW <= 0) FW = 8; if (FH <= 0) FH = 16;
	NAV_H = FH + 12;

	g_resp = (char *) malloc (RESP_CAP);
	if (!g_resp) return 1;

	kapi_set_pointer_handler (on_pointer);
	kapi_set_key_handler (on_key);

	load_url ("about:start");
	g_hi = -1; g_hn = 0;
	render ();

	while (!kapi_should_exit ()) {
		kapi_pump_events ();
		int dirty = 0;
		if (g_clk_have) { g_clk_have = 0; handle_click (g_clk_x, g_clk_y); dirty = 1; }
		while (g_kt != g_kh) { int k = g_kq[g_kt]; g_kt = (g_kt+1)%KQ; handle_key (k); dirty = 1; }
		if (dirty) render ();
		kapi_msleep (15);
	}
	return 0;
}
