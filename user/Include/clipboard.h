//
// clipboard.h -- the clipboard, shared by every app (docs/clipboard/README.md): the service `clipd`
// keeps the last CLIP_RING copies (clipproto.h), its widget (the dock's clipboard button) shows
// them and moves the cursor -- the item Ctrl+V pastes. Header-only, freestanding (the kapi, new[]).
//
//   clip_set_text ("hello");            char b[256]; clip_get_text (b, sizeof b);
//   clip_set_files ("SD:/a.txt", cut);  int cut; clip_get_file (b, sizeof b, &cut);
//   clip_set_image (px, w, h);          int w, h; unsigned *px = clip_get_image (&w, &h); delete[] px;
//   clip_put (fmts, datas, lens, n);    several formats of one copy (Letters: "rtf" and "text")
//   clip_get (fmts, nf, got, cap, &data, &len)   the first format of fmts the item has (data: new[])
//
// A copy is also kept by the kernel's clipboard (one text or one path: v40), which is what is
// pasted when clipd cannot be reached (an older card, the PC's desktop simulator).
//
#ifndef _clipboard_h
#define _clipboard_h
#include "appkit/appkit.h"
#include "clipproto.h"

static inline int clip_len_ (const char *s) { int n = 0; while (s && s[n]) n++; return n; }

// ---- the service -------------------------------------------------------------------------------------
// clipd's pid; launched when it is not running (0: not there)
static inline int clip_service_ (void)
{
	int pid = kapi_ipc_lookup (CLIP_SERVICE);
	if (pid == 0)
	{
		static unsigned lastTry;			// (not every call: a card without clipd)
		unsigned now = kapi_get_ticks ();
		if (lastTry && now - lastTry < 1000) return 0;
		lastTry = now ? now : 1;
		if (!kapi_launch ("clipd")) return 0;
		for (int i = 0; i < 40 && pid == 0; i++) { kapi_msleep (25); pid = kapi_ipc_lookup (CLIP_SERVICE); }
	}
	return pid;
}
// the app's name ("SD:apps/letters.app/" -> "letters")
static inline void clip_source_ (char *out, int cap)
{
	char d[128]; int n = kapi_app_dir (d, sizeof d);
	if (n <= 0 || n >= (int) sizeof d) { out[0] = 0; return; }
	d[n] = 0;
	int e = n; while (e > 0 && d[e - 1] == '/') e--;
	int s = e; while (s > 0 && d[s - 1] != '/' && d[s - 1] != ':') s--;
	int k = 0;
	for (int i = s; i < e && k < cap - 1; i++) { if (d[i] == '.' && i + 4 == e) break; out[k++] = d[i]; }
	out[k] = 0;
}
// a fresh name in RAM:/clip (the transfers' files)
static inline void clip_tmp_ (char *out, const char *what)
{
	static unsigned seq;
	unsigned v[3] = { kapi_get_ticks (), ++seq, (unsigned) (unsigned long long) &seq };
	const char *base = CLIP_DIR "/";
	int k = 0;
	for (int i = 0; base[i]; i++) out[k++] = base[i];
	for (int i = 0; what[i]; i++) out[k++] = what[i];
	for (int j = 0; j < 3; j++)
	{
		out[k++] = '-';
		unsigned x = v[j];
		for (int i = 7; i >= 0; i--) out[k++] = "0123456789abcdef"[(x >> (i * 4)) & 15];
	}
	out[k] = 0;
}

// ---- copying -----------------------------------------------------------------------------------------
// One copy, n representations (a format and its bytes each). False: clipd could not be reached.
static inline bool clip_put (const char *const *fmt, const void *const *data, const unsigned *len, int n)
{
	int pid = clip_service_ ();
	if (pid == 0) return false;
	char src[32]; clip_source_ (src, sizeof src);
	int sl = clip_len_ (src);
	unsigned size = clipc_size (fmt, len, n);
	if (size + (unsigned) sl + 1 <= 500)
	{	// small: the message carries it
		unsigned char m[512];
		for (int i = 0; i < sl; i++) m[i] = (unsigned char) src[i];
		m[sl] = 0;
		clipc_write (m + sl + 1, fmt, data, len, n);
		return kapi_mailbox_send (pid, CLIP_PUT_INLINE, m, size + (unsigned) sl + 1) >= 0;
	}
	unsigned char *buf = new unsigned char[size];
	if (!buf) return false;
	clipc_write (buf, fmt, data, len, n);
	kapi_mkdir (CLIP_DIR);
	char path[96]; clip_tmp_ (path, "put");
	bool ok = kapi_save_file (path, buf, size) == (int) size;
	delete[] buf;
	if (!ok) { kapi_remove (path); return false; }
	char m[160]; int k = 0;
	for (int i = 0; path[i]; i++) m[k++] = path[i];
	m[k++] = 0;
	for (int i = 0; i < sl; i++) m[k++] = src[i];
	m[k++] = 0;
	if (kapi_mailbox_send (pid, CLIP_PUT, m, (unsigned) k) < 0) { kapi_remove (path); return false; }
	return true;
}

// ---- pasting -----------------------------------------------------------------------------------------
// The item under the cursor in the first of fmt[] it has (else the newest item that has one of them):
// its format into got (cap), its bytes into *data (new[]: delete[] it), *len. False: nothing suits,
// or clipd could not be reached.
static inline bool clip_get (const char *const *fmt, int nf, char *got, int cap, unsigned char **data, unsigned *len)
{
	*data = 0; *len = 0; if (got && cap) got[0] = 0;
	int pid = clip_service_ ();
	if (pid == 0) return false;
	kapi_mkdir (CLIP_DIR);
	char reply[96]; clip_tmp_ (reply, "get");
	char m[512]; int k = 0;
	for (int i = 0; reply[i]; i++) m[k++] = reply[i];
	m[k++] = 0;
	for (int f = 0; f < nf; f++)
	{
		int fl = clip_len_ (fmt[f]);
		if (k + fl + 2 > (int) sizeof m) break;
		for (int i = 0; i < fl; i++) m[k++] = fmt[f][i];
		m[k++] = 0;
	}
	m[k++] = 0;
	if (kapi_mailbox_send (pid, CLIP_GET, m, (unsigned) k) < 0) return false;
	// clipd writes the answer under that name (complete: written aside, then renamed)
	void *h = 0;
	for (int i = 0; i < 400 && !h; i++)
	{
		h = kapi_open (reply);
		if (!h) kapi_msleep (i < 50 ? 1 : 5);
	}
	if (!h) return false;
	unsigned n = kapi_fsize (h);
	unsigned char *c = new unsigned char[n ? n : 1];
	unsigned got_ = 0;
	while (got_ < n) { int r = kapi_read (h, c + got_, n - got_); if (r <= 0) break; got_ += (unsigned) r; }
	kapi_close (h);
	kapi_remove (reply);
	const unsigned char *d; unsigned dl; char f[32];
	bool ok = got_ == n && clipc_rep (c, n, 0, f, sizeof f, &d, &dl);
	if (ok)
	{
		*data = new unsigned char[dl ? dl : 1];
		for (unsigned i = 0; i < dl; i++) (*data)[i] = d[i];
		*len = dl;
		if (got && cap) { int i = 0; for (; f[i] && i < cap - 1; i++) got[i] = f[i]; got[i] = 0; }
	}
	delete[] c;
	return ok;
}

// ---- text and paths (the calls every app had) ----------------------------------------------------------------
static inline void clip_set_text_n (const char *s, int n)
{
	if (n < 0) n = 0;
	kapi_clipboard_set (CLIP_TEXT, s, (unsigned) n);		// (the kernel's too: the fallback)
	const char *f[1] = { "text" }; const void *d[1] = { s }; unsigned l[1] = { (unsigned) n };
	clip_put (f, d, l, 1);
}
static inline void clip_set_text (const char *s) { clip_set_text_n (s, clip_len_ (s)); }

// The clipboard's text into buf (NUL-terminated, truncated to cap-1) -> its length; 0: no text.
static inline int clip_get_text (char *buf, int cap)
{
	static const char *const f[1] = { "text" };
	unsigned char *d; unsigned n; char got[16];
	if (clip_get (f, 1, got, sizeof got, &d, &n))
	{
		unsigned m = n < (unsigned) cap - 1 ? n : (unsigned) cap - 1;
		for (unsigned i = 0; i < m; i++) buf[i] = (char) d[i];
		buf[m] = 0; delete[] d;
		return (int) m;
	}
	if (kapi_ipc_lookup (CLIP_SERVICE)) { buf[0] = 0; return 0; }	// (clipd there: nothing suits)
	int type = 0;
	int n2 = kapi_clipboard_get (&type, buf, (unsigned) (cap - 1), 0);
	if (type != CLIP_TEXT) { buf[0] = '\0'; return 0; }
	if (n2 > cap - 1) n2 = cap - 1;
	buf[n2] = '\0';
	return n2;
}

// File / folder paths ('\n'-separated): copied, or cut when `cut` != 0 (the paste moves them).
static inline void clip_set_files (const char *paths, int cut)
{
	int n = clip_len_ (paths);
	kapi_clipboard_set (cut ? CLIP_FILES_CUT : CLIP_FILES, paths, (unsigned) n);
	const char *f[2] = { cut ? "files-cut" : "files", "text" }; const void *d[2] = { paths, paths };
	unsigned l[2] = { (unsigned) n, (unsigned) n };
	clip_put (f, d, l, 2);
}

// The first path into buf; *cut = 1 if it was cut. 0 if no path.
static inline int clip_get_file (char *buf, int cap, int *cut)
{
	static const char *const f[2] = { "files", "files-cut" };
	unsigned char *d; unsigned n; char got[16];
	int type = 0;
	if (clip_get (f, 2, got, sizeof got, &d, &n))
	{
		unsigned m = n < (unsigned) cap - 1 ? n : (unsigned) cap - 1;
		for (unsigned i = 0; i < m; i++) buf[i] = (char) d[i];
		buf[m] = 0; delete[] d;
		type = clipc_eq (got, "files-cut") ? CLIP_FILES_CUT : CLIP_FILES;
	}
	else if (!kapi_ipc_lookup (CLIP_SERVICE))
	{
		int n2 = kapi_clipboard_get (&type, buf, (unsigned) (cap - 1), 0);
		if (n2 > cap - 1) n2 = cap - 1;
		if (n2 < 0) n2 = 0;
		buf[n2] = '\0';
	}
	if (type != CLIP_FILES && type != CLIP_FILES_CUT) { buf[0] = '\0'; return 0; }
	int len = 0;
	while (buf[len] && buf[len] != '\n') len++;
	buf[len] = 0;
	if (cut) *cut = type == CLIP_FILES_CUT;
	return len;
}

// After a cut + paste moved the files: that item goes (clipd), and the kernel's copy.
static inline void clip_clear (void)
{
	kapi_clipboard_set (0, 0, 0);
	int pid = kapi_ipc_lookup (CLIP_SERVICE);
	if (pid) kapi_mailbox_send (pid, CLIP_DROP_CUT, "", 1);
}

// ---- images ---------------------------------------------------------------------------------------
// w x h pixels 0x00RRGGBB (a row after the other)
static inline bool clip_set_image (const unsigned *px, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	unsigned n = 8 + (unsigned) w * (unsigned) h * 4;
	unsigned char *b = new unsigned char[n];
	if (!b) return false;
	clipc_put32 (b, (unsigned) w); clipc_put32 (b + 4, (unsigned) h);
	const unsigned char *s = (const unsigned char *) px;
	for (unsigned i = 0; i < n - 8; i++) b[8 + i] = s[i];
	const char *f[1] = { "image" }; const void *d[1] = { b }; unsigned l[1] = { n };
	bool ok = clip_put (f, d, l, 1);
	delete[] b;
	return ok;
}
// -> new[] pixels (delete[] them) and the size, or 0: no image
static inline unsigned *clip_get_image (int *w, int *h)
{
	static const char *const f[1] = { "image" };
	unsigned char *d; unsigned n; char got[16];
	if (!clip_get (f, 1, got, sizeof got, &d, &n)) return 0;
	unsigned W = n >= 8 ? clipc_get32 (d) : 0, H = n >= 8 ? clipc_get32 (d + 4) : 0;
	if (!W || !H || 8 + (unsigned long long) W * H * 4 > n) { delete[] d; return 0; }
	unsigned *px = new unsigned[W * H];
	const unsigned char *s = d + 8;
	unsigned char *o = (unsigned char *) px;
	for (unsigned i = 0; i < W * H * 4; i++) o[i] = s[i];
	delete[] d;
	*w = (int) W; *h = (int) H;
	return px;
}

#endif
