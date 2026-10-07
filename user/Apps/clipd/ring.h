//
// clipd/ring.h -- the shared clipboard's memory (docs/clipboard/README.md): the last CLIP_RING copies,
// newest first, each with its representations (a format, its bytes), its source app and its time,
// and the cursor -- the item Ctrl+V pastes. Plain C++ over malloc (newlib, or the PC for the tests).
//
#ifndef _clipd_ring_h
#define _clipd_ring_h

#include <stdlib.h>
#include <string.h>
#include "systemkit/systemkit.h"

struct ClipRep { char fmt[24]; unsigned char *d; unsigned n; };
struct ClipEntry
{
	unsigned id; int nr; ClipRep r[8];
	char src[32]; unsigned char hour, minute;
	unsigned bytes;
};

class ClipRing
{
public:
	enum { MAXBYTES = 64u * 1024 * 1024, MAXITEM = 48u * 1024 * 1024 };
	ClipEntry e[CLIP_RING]; int n;		// e[0]: the newest
	unsigned cursor, nextId;		// the cursor's item id (0: none)

	ClipRing () : n (0), cursor (0), nextId (1) {}
	~ClipRing () { clear (); }

	static void freeEntry (ClipEntry &x) { for (int i = 0; i < x.nr; i++) free (x.r[i].d); x.nr = 0; }
	int index (unsigned id) const { for (int i = 0; i < n; i++) if (e[i].id == id) return i; return -1; }
	unsigned total () const { unsigned t = 0; for (int i = 0; i < n; i++) t += e[i].bytes; return t; }
	void removeAt (int i)
	{
		if (i < 0 || i >= n) return;
		bool wasCursor = e[i].id == cursor;
		freeEntry (e[i]);
		for (int k = i; k < n - 1; k++) e[k] = e[k + 1];
		n--;
		if (wasCursor) cursor = n ? e[i < n ? i : n - 1].id : 0;	// (the next older one, else the newest left)
	}
	void clear () { while (n) removeAt (n - 1); cursor = 0; }

	// A copy (a container) from `src` at hh:mm -> its id; 0: refused (empty, damaged, too big)
	unsigned put (const unsigned char *c, unsigned len, const char *src, int hh, int mm)
	{
		int cnt = clipc_count (c, len);
		if (cnt <= 0 || len > MAXITEM) return 0;
		ClipEntry x; memset (&x, 0, sizeof x);
		for (int i = 0; i < cnt && x.nr < 8; i++)
		{
			const unsigned char *d; unsigned dl; ClipRep &r = x.r[x.nr];
			if (!clipc_rep (c, len, i, r.fmt, sizeof r.fmt, &d, &dl)) break;
			r.d = (unsigned char *) malloc (dl ? dl : 1);
			if (!r.d) { freeEntry (x); return 0; }
			memcpy (r.d, d, dl); r.n = dl; x.bytes += dl; x.nr++;
		}
		if (!x.nr) return 0;
		strncpy (x.src, src ? src : "", sizeof x.src - 1);
		x.hour = (unsigned char) hh; x.minute = (unsigned char) mm;
		x.id = nextId++;
		if (n == CLIP_RING) removeAt (n - 1);
		for (int k = n; k > 0; k--) e[k] = e[k - 1];
		e[0] = x; n++;
		while (n > 1 && total () > MAXBYTES) removeAt (n - 1);	// (the oldest go first)
		cursor = x.id;
		return x.id;
	}
	// The cursor's item in the first format of fmts it has; else the newest item that has one
	const ClipRep *best (const char *const *fmts, int nf, unsigned *itemId = 0) const
	{
		int c = index (cursor);
		for (int pass = 0; pass < 2; pass++)
			for (int i = pass ? 0 : (c < 0 ? n : c); i < (pass ? n : (c < 0 ? n : c + 1)); i++)
				for (int f = 0; f < nf; f++)
					for (int r = 0; r < e[i].nr; r++)
						if (!strcmp (e[i].r[r].fmt, fmts[f])) { if (itemId) *itemId = e[i].id; return &e[i].r[r]; }
		return 0;
	}
	void setCursor (unsigned id) { if (index (id) >= 0) cursor = id; }
	void remove (unsigned id) { removeAt (index (id)); }
	// after a cut + paste: the cursor's item goes if it is a cut
	bool dropCut ()
	{
		int i = index (cursor);
		if (i < 0) return false;
		for (int r = 0; r < e[i].nr; r++) if (!strcmp (e[i].r[r].fmt, "files-cut")) { removeAt (i); return true; }
		return false;
	}

	// What the widget shows of item i
	void describe (int i, ClipItemMsg &m) const
	{
		const ClipEntry &x = e[i];
		memset (&m, 0, sizeof m);
		m.id = x.id; m.cursor = x.id == cursor; m.hour = x.hour; m.minute = x.minute;
		strncpy (m.source, x.src, sizeof m.source - 1);
		// the main representation: the first that is not plain text, else the text
		const ClipRep *main = &x.r[0];
		for (int r = 0; r < x.nr; r++) if (strcmp (x.r[r].fmt, "text")) { main = &x.r[r]; break; }
		strncpy (m.kind, main->fmt, sizeof m.kind - 1);
		m.size = main->n;
		if (!strcmp (main->fmt, "image") && main->n >= 8)
		{
			m.w = clipc_get32 (main->d); m.h = clipc_get32 (main->d + 4);
			return;
		}
		// a line of text: the text's own (else the main one's, if it reads as text)
		const ClipRep *t = main;
		for (int r = 0; r < x.nr; r++) if (!strcmp (x.r[r].fmt, "text")) t = &x.r[r];
		bool files = !strncmp (main->fmt, "files", 5);
		unsigned k = 0; int nf = 0;
		for (unsigned i2 = 0; i2 < t->n && k < sizeof m.preview - 1; i2++)
		{
			unsigned char ch = t->d[i2];
			if (files && ch == '\n') { nf++; if (k + 2 < sizeof m.preview - 1) { m.preview[k++] = ','; m.preview[k++] = ' '; } continue; }
			m.preview[k++] = (ch == '\n' || ch == '\r' || ch == '\t') ? ' ' : (char) ch;
		}
		m.preview[k] = 0;
		if (files)
		{	// "SD:/a/b.txt, SD:/c" -> "b.txt, c"
			char out[sizeof m.preview]; unsigned o = 0; const char *s = m.preview;
			while (*s && o < sizeof out - 1)
			{
				const char *end = strstr (s, ", "); if (!end) end = s + strlen (s);
				const char *b = s; for (const char *q = s; q < end; q++) if (*q == '/' || *q == ':') b = q + 1;
				if (o) { out[o++] = ','; out[o++] = ' '; }
				while (b < end && o < sizeof out - 1) out[o++] = *b++;
				s = *end ? end + 2 : end;
			}
			out[o] = 0; strcpy (m.preview, out);
			m.nfiles = (unsigned char) (nf + (t->n && t->d[t->n - 1] != '\n' ? 1 : 0));
		}
	}
};

#endif
