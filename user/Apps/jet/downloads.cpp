//
// web/downloads.cpp -- Web's downloads window: a window of its own (not drawn in the page), the
// browser program started again as `web --onyx-downloads` by the browser window (engine_spawn_self),
// its standard input and output two pipes to it. One row a download: its name, a progress bar, the
// bytes, a Cancel button while it runs (then "Done", "Failed: why" or "Cancelled").
//
// The browser writes lines:  add <id> <total> <path> / prog <id> <done> <total> / done <id> <bytes> /
// fail <id> <why> / cancelled <id>.  This window answers:  cancel <id>.
// When the browser window closes (its end of the pipe), the window stays until the user closes it.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#include "kapi.h"
#include "uikit/uikit.h"
#include "uikit/paint.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace uikit;

namespace {

enum { ST_RUNNING, ST_DONE, ST_FAILED, ST_CANCELLED };
enum { MAXROWS = 32, ROW_H = 58, PAD = 10, BTN_W = 78, BTN_H = 26, W = 440 };

struct Row
{
	int id, state;
	long long done, total;
	char name[96], why[96];
};
Row g_rows[MAXROWS];
int g_n;

Row *find (int id)
{
	for (int i = 0; i < g_n; i++) if (g_rows[i].id == id) return &g_rows[i];
	return 0;
}

void bytes (char *o, int cap, long long n)
{
	if (n >= 1024 * 1024) snprintf (o, (size_t) cap, "%.1f MB", (double) n / (1024.0 * 1024.0));
	else if (n >= 1024) snprintf (o, (size_t) cap, "%lld KB", n / 1024);
	else snprintf (o, (size_t) cap, "%lld bytes", n);
}

class List : public Widget
{
public:
	List (int l, int t, int w, int h) : Widget (l, t, w, h) { anchor = ANCHOR_FILL; }
	void onDraw () override
	{
		canvas.clear (C_BG);
		int fh = uk_fh ();
		if (g_n == 0) { uk_text_c (canvas, 0, 0, width, height, "No downloads", C_DIS); return; }
		for (int i = 0; i < g_n; i++)
		{
			const Row &r = g_rows[g_n - 1 - i];			// (the newest first)
			int y = i * ROW_H + PAD;
			if (y > height) break;
			uk_text_l (canvas, PAD, y, fh, r.name, C_TEXT, 2);
			int bw = width - 2 * PAD - (r.state == ST_RUNNING ? BTN_W + PAD : 0);
			int fill = r.total > 0 ? (int) ((double) bw * (double) r.done / (double) r.total) : 0;
			if (r.state == ST_DONE) fill = bw;
			uk_progress_bar (canvas, PAD, y + fh + 4, bw, 10, fill);
			char a[32], b[32], t[160];
			bytes (a, sizeof a, r.done);
			if (r.state == ST_RUNNING)
			{
				if (r.total > 0) { bytes (b, sizeof b, r.total); snprintf (t, sizeof t, "%s of %s", a, b); }
				else snprintf (t, sizeof t, "%s", a);
				uk_framed (canvas, width - PAD - BTN_W, y + 4, BTN_W, BTN_H, C_BUTTON, UK_NORMAL);
				uk_text_c (canvas, width - PAD - BTN_W, y + 4, BTN_W, BTN_H, "Cancel", C_BUTTON_TEXT);
			}
			else if (r.state == ST_DONE) snprintf (t, sizeof t, "Done -- %s, in SD:/Downloads", a);
			else if (r.state == ST_CANCELLED) snprintf (t, sizeof t, "Cancelled");
			else snprintf (t, sizeof t, "Failed: %s", r.why);
			uk_text_l (canvas, PAD, y + fh + 18, fh, t, r.state == ST_FAILED ? 0x00C03030 : C_DIS);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		static int was;
		bool click = bl && !was;
		was = bl;
		if (!click) return true;
		int i = (my - PAD) / ROW_H;
		if (i < 0 || i >= g_n) return true;
		Row &r = g_rows[g_n - 1 - i];
		int y = i * ROW_H + PAD;
		if (r.state == ST_RUNNING && mx >= width - PAD - BTN_W && mx < width - PAD && my >= y + 4 && my < y + 4 + BTN_H)
		{
			char line[32];
			int n = snprintf (line, sizeof line, "cancel %d\n", r.id);
			(void) !write (1, line, (size_t) n);
		}
		return true;
	}
};

List *g_list;
char g_in[4096];
int g_inLen;
bool g_eof;

// This window to the front (a new download, File > Downloads): found among the windows by its owner.
void raise_self ()
{
	static kapi_win_info w[64];
	int n = kapi_win_list (w, 64);
	unsigned me = (unsigned) getpid ();
	for (int i = 0; i < n; i++)
		if (w[i].pid == me) { kapi_win_raise (w[i].id); return; }
}

void line (char *s)
{
	char *cmd = strtok (s, " ");
	char *ids = strtok (0, " ");
	if (!cmd) { raise_self (); return; }			// (an empty line: "show yourself")
	if (!ids) return;
	int id = atoi (ids);
	Row *r = find (id);
	if (!strcmp (cmd, "add"))
	{
		raise_self ();
		if (!r)
		{
			if (g_n == MAXROWS) { memmove (g_rows, g_rows + 1, sizeof g_rows[0] * (MAXROWS - 1)); g_n--; }
			r = &g_rows[g_n++];
			memset (r, 0, sizeof *r);
			r->id = id;
		}
		char *tot = strtok (0, " ");
		char *path = strtok (0, "");
		r->total = tot ? atoll (tot) : 0;
		const char *base = path ? strrchr (path, '/') : 0;
		snprintf (r->name, sizeof r->name, "%s", base ? base + 1 : path ? path : "download");
		r->state = ST_RUNNING;
	}
	if (!r) return;
	if (!strcmp (cmd, "prog"))
	{
		char *d = strtok (0, " "), *t = strtok (0, " ");
		if (d) r->done = atoll (d);
		if (t) r->total = atoll (t);
	}
	else if (!strcmp (cmd, "done")) { char *d = strtok (0, " "); if (d) r->done = atoll (d); r->state = ST_DONE; }
	else if (!strcmp (cmd, "fail")) { char *w = strtok (0, ""); snprintf (r->why, sizeof r->why, "%s", w ? w : "?"); r->state = ST_FAILED; }
	else if (!strcmp (cmd, "cancelled")) r->state = ST_CANCELLED;
}

class DlRoot : public Root
{
public:
	DlRoot (int x, int y, int h) : Root (x, y, W, h, "Downloads", 0) {}
	void onTick () override
	{
		if (g_eof) return;
		int n = (int) read (0, g_in + g_inLen, sizeof g_in - 1 - (size_t) g_inLen);
		if (n == 0) { g_eof = true; return; }
		if (n < 0) return;
		g_inLen += n;
		g_in[g_inLen] = 0;
		char *s = g_in, *nl;
		while ((nl = strchr (s, '\n')) != 0) { *nl = 0; line (s); s = nl + 1; }
		g_inLen = (int) strlen (s);
		memmove (g_in, s, (size_t) g_inLen + 1);
		g_list->invalidate (true);
	}
};

} // namespace

int downloads_main ()
{
	fcntl (0, F_SETFL, fcntl (0, F_GETFL) | O_NONBLOCK);
	fcntl (1, F_SETFL, fcntl (1, F_GETFL) | O_NONBLOCK);		// (a browser that no longer reads never holds this window)
	int sw = 1920, sh = 1080;
	kapi_screen_size (&sw, &sh);
	int h = 4 * ROW_H + 2 * PAD;
	DlRoot root (sw - W - 24, sh - h - 140, h);			// (bottom right, above the dock)
	if (root.canvas.px == 0) return 1;
	g_list = new List (0, 0, W, h);
	root.addChild (g_list);
	root.setResizable (true);
	root.run ();
	return 0;
}
