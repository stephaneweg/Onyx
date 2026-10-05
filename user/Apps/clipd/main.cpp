//
// Apps/clipd/main.cpp -- clipd, the shared clipboard's service (docs/clipboard/README.md; the protocol:
// user/Include/clipproto.h; the apps' side: user/Include/clipboard.h). No window: it registers the IPC service
// "clipboard", keeps the last 10 copies in its memory (ring.h) and answers the apps:
//   CLIP_PUT / CLIP_PUT_INLINE   a copy -> the ring's front, the cursor on it, a notification
//                                ("Text copied", "Image copied", "2 files cut"...)
//   CLIP_GET                     the cursor's item in a format the app takes (else the newest that
//                                has one), written into the file the app named
//   CLIP_DROP_CUT                a cut pasted: that item goes
//   CLIP_LIST / CURSOR / DELETE / CLEAR / SUBSCRIBE   the widget's (apps/clipboard)
// Started on demand by the first copy (clipboard.h) or by autostart; lives until the Pi stops: the
// history is lost then (the user's choice).
//
#include <stdio.h>
#include "appkit/appkit.h"
#include "notify.h"
#include "ring.h"

static ClipRing g_ring;
static int g_sub[8], g_nsub;			// the widgets told of the changes

static void changed ()
{
	for (int i = 0; i < g_nsub; i++)
		if (kapi_mailbox_send (g_sub[i], CLIP_CHANGED, "", 1) < 0) { g_sub[i--] = g_sub[--g_nsub]; }
}
static void subscribe (int pid)
{
	for (int i = 0; i < g_nsub; i++) if (g_sub[i] == pid) return;
	if (g_nsub < 8) g_sub[g_nsub++] = pid;
	else { for (int i = 0; i < 7; i++) g_sub[i] = g_sub[i + 1]; g_sub[7] = pid; }
}

// the notification of a copy
static void tell (unsigned id)
{
	int i = g_ring.index (id);
	if (i < 0) return;
	ClipItemMsg m; g_ring.describe (i, m);
	char t[64];
	if (!strcmp (m.kind, "image")) snprintf (t, sizeof t, "Image copied (%u x %u)", m.w, m.h);
	else if (!strcmp (m.kind, "files") || !strcmp (m.kind, "files-cut"))
		snprintf (t, sizeof t, "%u %s %s", m.nfiles, m.nfiles == 1 ? "item" : "items", m.kind[5] ? "cut" : "copied");
	else if (!strcmp (m.kind, "url")) snprintf (t, sizeof t, "Link copied");
	else snprintf (t, sizeof t, "Text copied");
	notify ("Clipboard", t);
}

static unsigned put (const unsigned char *c, unsigned n, const char *src)
{
	int y, mo, d, h = 0, mi = 0, s;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, &s);
	unsigned id = g_ring.put (c, n, src, h, mi);
	if (id) { tell (id); changed (); }
	return id;
}

// CLIP_PUT: "path\0source\0" -- the container in that file
static void put_file (const char *msg, int n)
{
	const char *path = msg, *src = msg + strlen (msg) + 1;
	if (src > msg + n) src = "";
	void *h = kapi_open (path);
	if (!h) return;
	unsigned len = kapi_fsize (h);
	unsigned char *c = (unsigned char *) malloc (len ? len : 1);
	unsigned got = 0;
	while (c && got < len) { int r = kapi_read (h, c + got, len - got); if (r <= 0) break; got += (unsigned) r; }
	kapi_close (h);
	kapi_remove (path);
	if (c && got == len) put (c, len, src);
	free (c);
}

// CLIP_GET: "reply\0fmt\0fmt\0...\0" -- the answer written aside, then renamed to `reply`
static void get (const char *msg, int n)
{
	const char *reply = msg;
	const char *fmts[16]; int nf = 0;
	const char *p = msg + strlen (msg) + 1;
	while (p < msg + n && *p && nf < 16) { fmts[nf++] = p; p += strlen (p) + 1; }
	const ClipRep *r = g_ring.best (fmts, nf);
	const char *f[1]; const void *d[1]; unsigned l[1]; int cnt = 0;
	if (r) { f[0] = r->fmt; d[0] = r->d; l[0] = r->n; cnt = 1; }
	unsigned size = clipc_size (f, l, cnt);
	unsigned char *c = (unsigned char *) malloc (size);
	if (!c) return;
	clipc_write (c, f, d, l, cnt);
	char tmp[128]; snprintf (tmp, sizeof tmp, "%s.part", reply);
	if (kapi_save_file (tmp, c, size) == (int) size) kapi_rename (tmp, reply);
	else kapi_remove (tmp);
	free (c);
}

static void list (int to)
{
	for (int i = 0; i < g_ring.n; i++)
	{
		ClipItemMsg m; g_ring.describe (i, m);
		kapi_mailbox_send (to, CLIP_ITEM, &m, sizeof m);
	}
	unsigned cnt = (unsigned) g_ring.n;
	kapi_mailbox_send (to, CLIP_END, &cnt, sizeof cnt);
}

static void handle (int from, int type, const unsigned char *b, int n)
{
	switch (type)
	{
	case CLIP_PUT: put_file ((const char *) b, n); break;
	case CLIP_PUT_INLINE:
	{
		int sl = 0; while (sl < n && b[sl]) sl++;
		if (sl < n) put (b + sl + 1, (unsigned) (n - sl - 1), (const char *) b);
		break;
	}
	case CLIP_GET: get ((const char *) b, n); break;
	case CLIP_DROP_CUT: if (g_ring.dropCut ()) changed (); break;
	case CLIP_LIST: list (from); break;
	case CLIP_CURSOR: if (n >= 4) { g_ring.setCursor (clipc_get32 (b)); changed (); } break;
	case CLIP_DELETE: if (n >= 4) { g_ring.remove (clipc_get32 (b)); changed (); } break;
	case CLIP_CLEAR: g_ring.clear (); changed (); break;
	case CLIP_SUBSCRIBE: subscribe (from); break;
	}
}

#ifndef CLIPD_NO_MAIN
int main (void)
{
	if (!kapi_ipc_register (CLIP_SERVICE)) return 0;		// another clipd is running
	kapi_mkdir (CLIP_DIR);
	static unsigned char buf[520];
	for (;;)
	{
		int from = 0, type = 0;
		int n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 1);
		if (n < 0) { kapi_msleep (20); continue; }
		buf[n] = 0;
		handle (from, type, buf, n);
	}
	return 0;
}
#endif
