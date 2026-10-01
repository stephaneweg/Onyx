//
// Apps/pdf/engine.h -- the PDF Viewer's side of MuPDF (third_party/mupdf-1.28.5, built by mupdf.mk): the documents,
// their pages' display lists, the rendering into 0x00RRGGBB bitmaps, the text (selection, search), the links,
// the outline, the facts (Properties). And the worker thread that renders and searches while the window stays
// live: it hands its bitmaps and hits over with kapi_post.
//
// Threads: one fz_context a thread (the window's g_mu, the worker's clone), MuPDF's locks on kapi_lock. A
// document (fz_document) is not shared between threads: every use of one goes through its lock (Doc::lk) and
// stays short -- a page is turned into a display list there; the slow part, drawing the list, runs outside it
// (display lists may be drawn by several threads).
//
#ifndef _pdf_engine_h
#define _pdf_engine_h

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "kapi.h"
extern "C" {
#include "mupdf/fitz.h"
#include "mupdf/pdf.h"
}

namespace pdfv {

static inline void scopy (char *d, const char *s, int cap) { if (cap <= 0) return; int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

// ---- MuPDF's locks, its contexts --------------------------------------------------------------------------------
static volatile int g_mulk[FZ_LOCK_MAX];
static void mu_lock (void *, int n) { kapi_lock (&g_mulk[n]); }
static void mu_unlock (void *, int n) { kapi_unlock (&g_mulk[n]); }
static fz_locks_context g_locks = { 0, mu_lock, mu_unlock };
static fz_context *g_mu;			// the window's thread's
static bool engine_init ()
{
	g_mu = fz_new_context (0, &g_locks, 96 << 20);	// (its store: fonts, images, decoded streams)
	if (!g_mu) return false;
	fz_register_document_handlers (g_mu);
	fz_set_warning_callback (g_mu, 0, 0);			// (MuPDF's warnings: not on the console)
	fz_set_error_callback (g_mu, 0, 0);
	return true;
}

// ---- a rectangle in a page's points, the hits ---------------------------------------------------------------------
struct Quad { float x0, y0, x1, y1; };		// (axis-aligned: the quads' bounds)
static inline Quad quad_box (fz_quad q) { fz_rect r = fz_rect_from_quad (q); return Quad { r.x0, r.y0, r.x1, r.y1 }; }

struct OutlineItem { char *title; int page; float y; int depth, parent; bool kids, open; };
struct LinkInfo { Quad r; int page; float y; char *uri; };	// page >= 0: inside the document; uri: the web / a file
struct PageLinks { LinkInfo *l; int n; bool loaded; };
struct FontInfo { char name[80], type[24]; bool embedded, subset; };

// ---- a document -----------------------------------------------------------------------------------------------------
struct Doc
{
	volatile int lk;			// every use of `doc` (any thread)
	fz_document *doc; bool isPdf;
	char path[300], name[120], title[200];
	int npages; float *pw, *ph;		// the pages' sizes (points, their /Rotate applied)
	fz_display_list **dl; unsigned *dlUse; int ndl;	// the pages' lists (built on demand, the oldest dropped)
	PageLinks *links;
	OutlineItem *ol; int nol;
	int refs, id;

	Doc () { memset ((void *) this, 0, sizeof *this); }
};
static int g_docIds;

// fz_try under the lock: `body` must not return.
#define DOC_LOCK(d) kapi_lock (&(d)->lk)
#define DOC_UNLOCK(d) kapi_unlock (&(d)->lk)

// ---- the files through kapi (the card's volumes: SD:/, SD1:/... -- and the PC's simulator alike) ----------------------
struct KStream { void *h; long long size; unsigned char buf[32768]; };
static int ks_next (fz_context *ctx, fz_stream *stm, size_t)
{
	KStream *k = (KStream *) stm->state;
	int n = kapi_read (k->h, k->buf, sizeof k->buf);
	if (n < 0) fz_throw (ctx, FZ_ERROR_SYSTEM, "read error");
	stm->rp = k->buf; stm->wp = k->buf + n; stm->pos += n;
	if (n == 0) return EOF;
	return *stm->rp++;
}
static void ks_seek (fz_context *ctx, fz_stream *stm, int64_t off, int whence)
{
	KStream *k = (KStream *) stm->state;
	int64_t p = whence == SEEK_END ? k->size + off : whence == SEEK_CUR ? stm->pos + off : off;
	if (p < 0) p = 0; if (p > k->size) p = k->size;
	if (kapi_seek (k->h, (unsigned long long) p) != 0) fz_throw (ctx, FZ_ERROR_SYSTEM, "cannot seek");
	stm->pos = p; stm->rp = stm->wp = k->buf;
}
static void ks_drop (fz_context *ctx, void *st) { KStream *k = (KStream *) st; kapi_close (k->h); fz_free (ctx, k); }
static fz_stream *kstream_open (fz_context *ctx, const char *path)
{
	void *h = kapi_open (path);
	if (!h) fz_throw (ctx, FZ_ERROR_SYSTEM, "the file cannot be opened");
	KStream *k = 0;
	fz_try (ctx) k = (KStream *) fz_malloc (ctx, sizeof (KStream));
	fz_catch (ctx) { kapi_close (h); fz_rethrow (ctx); }
	k->h = h; k->size = (long long) kapi_fsize64 (h);
	fz_stream *s = fz_new_stream (ctx, k, ks_next, ks_drop);
	s->seek = ks_seek;
	return s;
}
static long long file_size (const char *path) { void *h = kapi_open (path); if (!h) return -1; long long n = (long long) kapi_fsize64 (h); kapi_close (h); return n; }
static bool file_exists (const char *path) { void *h = kapi_open (path); if (!h) return false; kapi_close (h); return true; }
// a whole (small) file -> malloc'd, NUL-terminated; 0: none
static char *file_read (const char *path, int *len)
{
	void *h = kapi_open (path); if (!h) return 0;
	unsigned n = kapi_fsize (h); char *b = (char *) malloc (n + 1);
	int r = b ? kapi_read (h, b, n) : -1; kapi_close (h);
	if (r < 0) { free (b); return 0; }
	b[r] = 0; if (len) *len = r; return b;
}

// open: 0 -> could not; *needPw: the document is encrypted (doc_auth then doc_load)
static Doc *doc_open (fz_context *ctx, const char *path, bool *needPw, char *err, int errCap)
{
	*needPw = false; if (err) err[0] = 0;
	fz_document *d = 0; fz_stream *stm = 0;
	fz_var (d); fz_var (stm);
	fz_try (ctx) { stm = kstream_open (ctx, path); d = fz_open_document_with_stream (ctx, "application/pdf", stm); }
	fz_always (ctx) fz_drop_stream (ctx, stm);
	fz_catch (ctx) { if (err) scopy (err, fz_caught_message (ctx), errCap); return 0; }
	Doc *D = new Doc;
	D->doc = d; D->id = ++g_docIds; D->refs = 1;
	D->isPdf = pdf_specifics (ctx, d) != 0;
	scopy (D->path, path, sizeof D->path);
	const char *b = path; for (const char *p = path; *p; p++) if (*p == '/' || *p == ':') b = p + 1;
	scopy (D->name, b, sizeof D->name);
	fz_try (ctx) *needPw = fz_needs_password (ctx, d) != 0;
	fz_catch (ctx) {}
	return D;
}
static bool doc_auth (fz_context *ctx, Doc *D, const char *pw)
{
	int ok = 0;
	fz_try (ctx) ok = fz_authenticate_password (ctx, D->doc, pw);
	fz_catch (ctx) ok = 0;
	return ok != 0;
}
static void outline_add (fz_context *ctx, Doc *D, fz_outline *o, int depth, int parent, int &cap)
{
	for (; o; o = o->next)
	{
		if (D->nol == cap) { cap = cap ? cap * 2 : 64; D->ol = (OutlineItem *) realloc (D->ol, sizeof (OutlineItem) * cap); }
		OutlineItem &it = D->ol[D->nol];
		int me = D->nol++;
		it.title = strdup (o->title ? o->title : "");
		for (char *p = it.title; *p; p++) if (*p == '\r' || *p == '\n' || *p == '\t') *p = ' ';
		it.page = -1; it.y = 0;
		fz_try (ctx) { it.page = fz_page_number_from_location (ctx, D->doc, o->page); it.y = o->y; }
		fz_catch (ctx) {}
		it.depth = depth; it.parent = parent; it.kids = o->down != 0; it.open = depth == 0 && o->is_open;
		if (o->down) outline_add (ctx, D, o->down, depth + 1, me, cap);
	}
}
// after the open (and the password): the pages, their sizes, the outline, the title
static bool doc_load (fz_context *ctx, Doc *D, char *err, int errCap)
{
	int n = 0;
	fz_try (ctx) n = fz_count_pages (ctx, D->doc);
	fz_catch (ctx) { scopy (err, fz_caught_message (ctx), errCap); return false; }
	if (n <= 0) { scopy (err, "The document has no pages.", errCap); return false; }
	D->npages = n;
	D->pw = (float *) malloc (sizeof (float) * n); D->ph = (float *) malloc (sizeof (float) * n);
	D->dl = (fz_display_list **) calloc (n, sizeof (fz_display_list *)); D->dlUse = (unsigned *) calloc (n, sizeof (unsigned));
	D->links = (PageLinks *) calloc (n, sizeof (PageLinks));
	for (int i = 0; i < n; i++)
	{
		D->pw[i] = 595; D->ph[i] = 842;
		fz_page *p = 0;
		fz_var (p);
		fz_try (ctx) { p = fz_load_page (ctx, D->doc, i); fz_rect b = fz_bound_page (ctx, p); if (b.x1 > b.x0 && b.y1 > b.y0) { D->pw[i] = b.x1 - b.x0; D->ph[i] = b.y1 - b.y0; } }
		fz_always (ctx) fz_drop_page (ctx, p);
		fz_catch (ctx) { if (i) { D->pw[i] = D->pw[i - 1]; D->ph[i] = D->ph[i - 1]; } }
	}
	fz_outline *o = 0;
	fz_var (o);
	fz_try (ctx) o = fz_load_outline (ctx, D->doc);
	fz_catch (ctx) o = 0;
	int cap = 0;
	if (o) { outline_add (ctx, D, o, 0, -1, cap); fz_drop_outline (ctx, o); }
	// a single entry holding all the others (the title, as the manuals have): open it
	if (D->nol > 1 && D->ol[0].kids && D->ol[0].depth == 0) { bool alone = true; for (int i = 1; i < D->nol; i++) if (D->ol[i].depth == 0) alone = false; if (alone) D->ol[0].open = true; }
	char t[200] = "";
	fz_try (ctx) { if (fz_lookup_metadata (ctx, D->doc, FZ_META_INFO_TITLE, t, sizeof t) <= 0) t[0] = 0; }
	fz_catch (ctx) t[0] = 0;
	scopy (D->title, t[0] ? t : D->name, sizeof D->title);
	return true;
}
static void doc_free (fz_context *ctx, Doc *D)
{
	for (int i = 0; i < D->npages; i++)
	{
		if (D->dl[i]) fz_drop_display_list (ctx, D->dl[i]);
		for (int k = 0; k < D->links[i].n; k++) free (D->links[i].l[k].uri);
		free (D->links[i].l);
	}
	for (int i = 0; i < D->nol; i++) free (D->ol[i].title);
	free (D->ol); free (D->pw); free (D->ph); free (D->dl); free (D->dlUse); free (D->links);
	fz_drop_document (ctx, D->doc);
	delete D;
}

// a page's display list, a reference of one's own (drop it: fz_drop_display_list); its links read on the way.
// At most DL_KEEP lists stay (the least used dropped).
enum { DL_KEEP = 24 };
static unsigned g_dlClock;
static fz_display_list *doc_list (fz_context *ctx, Doc *D, int pg)
{
	if (pg < 0 || pg >= D->npages) return 0;
	DOC_LOCK (D);
	fz_display_list *l = D->dl[pg];
	if (!l)
	{
		fz_page *p = 0;
		fz_var (p);
		fz_try (ctx)
		{
			p = fz_load_page (ctx, D->doc, pg);
			l = fz_new_display_list_from_page (ctx, p);
			if (!D->links[pg].loaded)
			{
				fz_link *ln = fz_load_links (ctx, p), *k;
				int n = 0; for (k = ln; k; k = k->next) n++;
				D->links[pg].l = (LinkInfo *) calloc (n ? n : 1, sizeof (LinkInfo));
				int j = 0;
				for (k = ln; k; k = k->next)
				{
					LinkInfo &li = D->links[pg].l[j];
					li.r = Quad { k->rect.x0, k->rect.y0, k->rect.x1, k->rect.y1 }; li.page = -1; li.uri = 0;
					if (!k->uri) continue;
					if (fz_is_external_link (ctx, k->uri)) li.uri = strdup (k->uri);
					else { float x = 0, y = 0; fz_location loc = fz_resolve_link (ctx, D->doc, k->uri, &x, &y); li.page = fz_page_number_from_location (ctx, D->doc, loc); li.y = y; if (li.page < 0) continue; }
					j++;
				}
				D->links[pg].n = j; D->links[pg].loaded = true;
				fz_drop_link (ctx, ln);
			}
		}
		fz_always (ctx) fz_drop_page (ctx, p);
		fz_catch (ctx) l = 0;
		if (l)
		{
			int n = 0, old = -1;
			for (int i = 0; i < D->npages; i++) if (D->dl[i]) { n++; if (old < 0 || D->dlUse[i] < D->dlUse[old]) old = i; }
			if (n >= DL_KEEP && old >= 0) { fz_drop_display_list (ctx, D->dl[old]); D->dl[old] = 0; }
			D->dl[pg] = l;
		}
	}
	if (l) { D->dlUse[pg] = ++g_dlClock; fz_keep_display_list (ctx, l); }
	DOC_UNLOCK (D);
	return l;
}

// the matrix of a page at scale s (px a point), turned rot degrees: the page's top left at (0, 0)
static fz_matrix page_ctm (Doc *D, int pg, float s, int rot)
{
	fz_matrix m = fz_pre_rotate (fz_scale (s, s), (float) rot);
	fz_rect b = fz_transform_rect (fz_make_rect (0, 0, D->pw[pg], D->ph[pg]), m);
	return fz_concat (m, fz_translate (-b.x0, -b.y0));
}
static inline void page_px (Doc *D, int pg, float s, int rot, int *w, int *h)
{
	float a = D->pw[pg] * s, b = D->ph[pg] * s;
	if (rot % 180) { float t = a; a = b; b = t; }
	*w = (int) (a + 0.5f); *h = (int) (b + 0.5f);
}

// page pg drawn at scale s, turned rot, the part (cx, cy, cw, ch) of it (px) -> 0x00RRGGBB, or 0
static unsigned *render_page (fz_context *ctx, Doc *D, int pg, float s, int rot, int cx, int cy, int cw, int ch)
{
	fz_display_list *l = doc_list (ctx, D, pg);
	if (!l || cw <= 0 || ch <= 0) { if (l) fz_drop_display_list (ctx, l); return 0; }
	unsigned *px = (unsigned *) malloc ((size_t) cw * ch * 4);
	if (!px) { fz_drop_display_list (ctx, l); return 0; }
	fz_pixmap *pm = 0; fz_device *dev = 0;
	fz_var (pm); fz_var (dev);
	bool ok = true;
	fz_try (ctx)
	{
		fz_irect bb = { cx, cy, cx + cw, cy + ch };
		pm = fz_new_pixmap_with_bbox_and_data (ctx, fz_device_bgr (ctx), bb, 0, 1, (unsigned char *) px);
		fz_clear_pixmap_with_value (ctx, pm, 255);
		dev = fz_new_draw_device (ctx, fz_identity, pm);
		fz_run_display_list (ctx, l, dev, page_ctm (D, pg, s, rot), fz_rect_from_irect (bb), 0);
		fz_close_device (ctx, dev);
	}
	fz_always (ctx) { fz_drop_device (ctx, dev); fz_drop_pixmap (ctx, pm); fz_drop_display_list (ctx, l); }
	fz_catch (ctx) ok = false;
	if (!ok) { free (px); return 0; }
	for (int i = 0, n = cw * ch; i < n; i++) px[i] &= 0xFFFFFF;
	return px;
}

// a page's text (selection, search): the caller's to drop (fz_drop_stext_page)
static fz_stext_page *page_text (fz_context *ctx, Doc *D, int pg)
{
	fz_display_list *l = doc_list (ctx, D, pg);
	if (!l) return 0;
	fz_stext_page *t = 0;
	fz_var (t);
	fz_try (ctx)
	{
		fz_stext_options o; memset (&o, 0, sizeof o);
		o.flags = FZ_STEXT_DEHYPHENATE;
		t = fz_new_stext_page_from_display_list (ctx, l, &o);
	}
	fz_always (ctx) fz_drop_display_list (ctx, l);
	fz_catch (ctx) t = 0;
	return t;
}

// the line of text around (x, y) of a page's text, cut to about `cap` characters around the hit (UTF-8)
static void text_around (fz_context *ctx, fz_stext_page *t, Quad q, char *out, int cap)
{
	out[0] = 0;
	float cy = (q.y0 + q.y1) / 2;
	for (fz_stext_block *b = t->first_block; b; b = b->next)
	{
		if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
		for (fz_stext_line *ln = b->u.t.first_line; ln; ln = ln->next)
		{
			if (cy < ln->bbox.y0 || cy > ln->bbox.y1 || q.x1 < ln->bbox.x0 || q.x0 > ln->bbox.x1) continue;
			// the line's characters; the hit's first one
			int n = 0, at = 0;
			for (fz_stext_char *c = ln->first_char; c; c = c->next) { fz_rect r = fz_rect_from_quad (c->quad); if (r.x0 <= q.x0 + 0.5f) at = n; n++; }
			int from = at - 14 < 0 ? 0 : at - 14, k = 0, o = 0;
			if (from > 0 && o + 4 < cap) { memcpy (out, "\xE2\x80\xA6", 3); o = 3; }
			for (fz_stext_char *c = ln->first_char; c && o < cap - 8; c = c->next, k++)
			{
				if (k < from) continue;
				o += fz_runetochar (out + o, c->c);
			}
			out[o] = 0;
			(void) ctx;
			return;
		}
	}
}

// the facts of the document (Properties)
static void doc_meta (fz_context *ctx, Doc *D, const char *key, char *out, int cap)
{
	out[0] = 0;
	DOC_LOCK (D);
	fz_try (ctx) { if (fz_lookup_metadata (ctx, D->doc, key, out, cap) <= 0) out[0] = 0; }
	fz_catch (ctx) out[0] = 0;
	DOC_UNLOCK (D);
}
static bool doc_allows (fz_context *ctx, Doc *D, fz_permission p)
{
	int r = 1;
	DOC_LOCK (D);
	fz_try (ctx) r = fz_has_permission (ctx, D->doc, p);
	fz_catch (ctx) r = 1;
	DOC_UNLOCK (D);
	return r != 0;
}
// the fonts the pages use (the first 300 pages), each once
static int doc_fonts (fz_context *ctx, Doc *D, FontInfo *out, int max)
{
	int n = 0;
	pdf_document *pd = pdf_specifics (ctx, D->doc);
	if (!pd) return 0;
	DOC_LOCK (D);
	fz_try (ctx)
	{
		for (int pg = 0; pg < D->npages && pg < 300 && n < max; pg++)
		{
			pdf_obj *po = pdf_lookup_page_obj (ctx, pd, pg);
			pdf_obj *res = pdf_dict_get_inheritable (ctx, po, PDF_NAME (Resources));
			pdf_obj *fonts = pdf_dict_get (ctx, res, PDF_NAME (Font));
			int k = pdf_dict_len (ctx, fonts);
			for (int i = 0; i < k && n < max; i++)
			{
				pdf_obj *f = pdf_dict_get_val (ctx, fonts, i);
				const char *bn = pdf_to_name (ctx, pdf_dict_get (ctx, f, PDF_NAME (BaseFont)));
				const char *st = pdf_to_name (ctx, pdf_dict_get (ctx, f, PDF_NAME (Subtype)));
				pdf_obj *desc = pdf_dict_get (ctx, f, PDF_NAME (FontDescriptor));
				if (!desc) desc = pdf_dict_get (ctx, pdf_array_get (ctx, pdf_dict_get (ctx, f, PDF_NAME (DescendantFonts)), 0), PDF_NAME (FontDescriptor));
				bool emb = !strcmp (st, "Type3") || (desc && (pdf_dict_get (ctx, desc, PDF_NAME (FontFile)) || pdf_dict_get (ctx, desc, PDF_NAME (FontFile2)) || pdf_dict_get (ctx, desc, PDF_NAME (FontFile3))));
				if (!bn || !bn[0]) bn = "(unnamed)";
				bool sub = strlen (bn) > 7 && bn[6] == '+';
				const char *nm = sub ? bn + 7 : bn;
				bool dup = false; for (int j = 0; j < n; j++) if (!strcmp (out[j].name, nm)) dup = true;
				if (dup) continue;
				scopy (out[n].name, nm, sizeof out[n].name);
				scopy (out[n].type, !strcmp (st, "Type0") ? "Type 0 (CID)" : !strcmp (st, "TrueType") ? "TrueType" : !strcmp (st, "Type1") ? "Type 1" : !strcmp (st, "Type3") ? "Type 3" : st, sizeof out[n].type);
				out[n].embedded = emb; out[n].subset = sub; n++;
			}
		}
	}
	fz_catch (ctx) {}
	DOC_UNLOCK (D);
	return n;
}

// ---- the worker: renders the pages asked, the thumbnails, searches -------------------------------------------------
enum { J_PAGE = 1, J_THUMB, J_RECENT };
struct Job { int kind; Doc *doc; int docId; int page; float scale; int rot; int cx, cy, cw, ch; int gen; char path[300]; };
struct Done { Job job; unsigned *px; int w, h; int npages; };	// (posted; the window frees it)

enum { WANT_MAX = 48 };
struct SearchHit { int page; Quad q[4]; int nq; char *ctx; };
struct HitPack { int gen, page, n; SearchHit *h; Doc *doc; bool last; };	// (posted; the window frees it)
struct SearchState
{
	Doc *doc; char needle[200]; int opts; int gen;
	int page;				// the next page to search (npages: done)
	bool on;
};

typedef void (*DoneFn) (void *, long);
struct Worker
{
	volatile int lk; int ev, tid; volatile bool quit;
	Job want[WANT_MAX]; int nwant;		// what the window wants now, the most wanted first (it rewrites the list)
	SearchState search;
	DoneFn onDone, onHits;
	fz_context *ctx;

	void start (DoneFn done, DoneFn hits)
	{
		onDone = done; onHits = hits; lk = 0; busy = 0; hasRunning = false; nwant = 0; quit = false; search.on = false; search.doc = 0;
		ctx = fz_clone_context (g_mu);
		ev = kapi_event_create (0, 0);
		tid = kapi_thread_create (thread_main, this, 1024 * 1024, "pdf-render");
	}
	Job running; bool hasRunning;		// (what it draws now: not asked again)
	static bool same (const Job &a, const Job &b)
	{
		return a.kind == b.kind && a.docId == b.docId && a.page == b.page && a.scale == b.scale && a.rot == b.rot && a.cx == b.cx && a.cy == b.cy
			&& a.cw == b.cw && a.ch == b.ch && !strcmp (a.path, b.path);
	}
	void set_wants (const Job *j, int n)
	{
		kapi_lock (&lk);
		nwant = 0;
		for (int i = 0; i < n && nwant < WANT_MAX; i++) if (!hasRunning || !same (j[i], running)) want[nwant++] = j[i];
		kapi_unlock (&lk);
		kapi_event_set (ev);
	}
	void start_search (Doc *d, const char *needle, int opts, int gen)
	{
		kapi_lock (&lk);
		search.doc = d; scopy (search.needle, needle, sizeof search.needle); search.opts = opts; search.gen = gen; search.page = 0; search.on = needle[0] != 0;
		kapi_unlock (&lk);
		kapi_event_set (ev);
	}
	void stop_search () { kapi_lock (&lk); search.on = false; kapi_unlock (&lk); }
	// a document closed: nothing more of it
	void forget (Doc *d)
	{
		kapi_lock (&lk);
		int k = 0; for (int i = 0; i < nwant; i++) if (want[i].doc != d) want[k++] = want[i];
		nwant = k; if (search.doc == d) search.on = false;
		kapi_unlock (&lk);
		kapi_lock (&busy); kapi_unlock (&busy);		// (what it was doing with it: finished)
	}
	volatile int busy;
	void stop () { quit = true; kapi_event_set (ev); if (tid > 0) kapi_thread_join (tid, 3000, 0); }

	static int thread_main (void *p) { ((Worker *) p)->loop (); return 0; }
	void loop ()
	{
		while (!quit)
		{
			Job j; bool have = false, srch = false; SearchState ss;
			kapi_lock (&lk);
			if (nwant) { j = want[0]; memmove (want, want + 1, sizeof (Job) * (nwant - 1)); nwant--; have = true; running = j; hasRunning = true; }
			else if (search.on) { ss = search; srch = true; search.page++; if (search.page >= search.doc->npages) search.on = false; }
			kapi_lock (&busy);
			kapi_unlock (&lk);
			if (have) { run (j); kapi_lock (&lk); hasRunning = false; kapi_unlock (&lk); }
			else if (srch) run_search (ss);
			kapi_unlock (&busy);
			if (!have && !srch) kapi_event_wait (ev, 500);
		}
	}
	void run (Job &j)
	{
		Done *d = (Done *) calloc (1, sizeof (Done));
		d->job = j;
		if (j.kind == J_RECENT)
		{	// a recent document's first page, j.cw px wide (its own Doc, opened and closed here)
			bool pw = false; char err[8];
			Doc *D = doc_open (ctx, j.path, &pw, err, sizeof err);
			if (D && !pw && doc_load (ctx, D, err, sizeof err))
			{
				float s = j.cw / D->pw[0]; int w, h; page_px (D, 0, s, 0, &w, &h);
				d->px = render_page (ctx, D, 0, s, 0, 0, 0, w, h); d->w = w; d->h = h; d->npages = D->npages;
			}
			if (D) doc_free (ctx, D);
		}
		else
		{
			d->px = render_page (ctx, j.doc, j.page, j.scale, j.rot, j.cx, j.cy, j.cw, j.ch);
			d->w = j.cw; d->h = j.ch;
		}
		kapi_post (onDone, d, 0);
	}
	void run_search (SearchState &s)
	{
		fz_stext_page *t = page_text (ctx, s.doc, s.page);
		SearchHit *hits = 0; int n = 0;
		if (t)
		{
			enum { MAXQ = 512 };
			fz_quad *q = (fz_quad *) malloc (sizeof (fz_quad) * MAXQ); int *mark = (int *) malloc (sizeof (int) * MAXQ);
			int nq = 0;
			fz_try (ctx) nq = fz_match_stext_page (ctx, t, s.needle, mark, q, MAXQ, (fz_search_options) s.opts);
			fz_catch (ctx) nq = 0;
			hits = (SearchHit *) calloc (nq ? nq : 1, sizeof (SearchHit));
			for (int i = 0; i < nq; i++)
			{
				if (mark[i] || n == 0) { hits[n].page = s.page; n++; }
				SearchHit &h = hits[n - 1];
				if (h.nq < 4) h.q[h.nq++] = quad_box (q[i]);
			}
			for (int i = 0; i < n; i++) { char b[160]; text_around (ctx, t, hits[i].q[0], b, sizeof b); hits[i].ctx = strdup (b); }
			free (q); free (mark);
			fz_drop_stext_page (ctx, t);
		}
		HitPack *p = (HitPack *) malloc (sizeof (HitPack));
		p->gen = s.gen; p->page = s.page; p->n = n; p->h = hits; p->doc = s.doc; p->last = s.page + 1 >= s.doc->npages;
		kapi_post (onHits, p, 0);
	}
};
} // namespace pdfv

#endif
