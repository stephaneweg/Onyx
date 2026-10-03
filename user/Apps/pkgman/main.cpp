//
// pkgman -- the Onyx Package Manager: the Control Panel's applet for the packages (docs/pkg/README.md;
// alone, a window of its own). On the library pkg/pkglib.h, as the `pkg` command:
//
//   Updates     the packages with a newer version in the repository: a box each (Install N Updates),
//               the system's marked "restart"; Check Now reads the index again.
//   Installed   every package installed: its version, size, its updates mode (Manual / Auto /
//               Never: three pills) and Remove (not the system's).
//   Available   the repository's packages not installed, or with an update: Install / Update.
//
// The search field filters the three (the name, the title, the category, the summary). A job (the
// index read again, installs, a removal) runs in a thread, the window alive: its progress in the
// footer and on the package's row; one job at a time. A system update is staged: a banner offers to
// restart (the packages are moved in at the next boot: `wait pkg commit`, etc/autostart). At the start
// the last index (SD:/var/pkg/index.txt) is shown at once, then read again from the repository.
// `pkgman --updates` opens on Updates (pkgd's notification).
//
#include "kapi.h"
#include "applib.h"
#include "bmp.hpp"
#include "pkg/pkglib.h"
#include "wtk/wtk.h"
#include "ft/wtkface.h"

using namespace wtk;
using namespace pkg;

#define W	700
#define H	470
#define TABH	30
#define ROWH	58
#define FOOT	46
#define ICON	32

enum { T_UPDATES, T_INSTALLED, T_AVAILABLE };
enum { ST_NONE, ST_WAIT, ST_WORK, ST_DONE, ST_STAGED, ST_FAILED };

// ---- what is shown: a snapshot of the index and the database (rebuilt between two jobs) -----------------
struct Row
{
	char name[40], title[48], summary[120], category[24], inst[24], avail[24], mode[8];
	u64 size, installed;
	bool isInst, hasUpdate, required, restart, staged, checked;
	int state, pct;				// during a job
	unsigned *icon; int iw, ih;
};
static Row *g_rows; static int g_nrows;
static int g_tab = T_UPDATES;
static int g_vis[512], g_nvis;			// the rows shown (g_rows indexes)
static char g_search[64];
static char g_status[200] = "";
static char g_indexDate[24] = "";
static bool g_verified = false, g_haveIndex = false;
static FtTextFace *g_small;
static Manager *g_M;

static bool contains_ci (const char *h, const char *n)
{
	if (!*n) return true;
	for (; *h; h++)
	{
		int i = 0;
		while (n[i] && h[i] && (h[i] | 32) == (n[i] | 32)) i++;
		if (!n[i]) return true;
	}
	return false;
}
static void size_text (u64 b, char *out, int cap)
{
	if (b >= 1048576) snprintf (out, cap, "%llu.%llu MB", b / 1048576, (b % 1048576) * 10 / 1048576);
	else snprintf (out, cap, "%llu KB", (b + 1023) / 1024);
}
static unsigned *load_icon (const char *name, int *w, int *h)
{
	char p[200];
	snprintf (p, sizeof p, PKG_VAR "/icons/%s.bmp", name);			// the repository's (cached)
	unsigned *px = exists (p) ? ui::bmp_decode (p, w, h) : 0;
	if (!px) { snprintf (p, sizeof p, "SD:/apps/%s.app/icon.bmp", name); px = exists (p) ? ui::bmp_decode (p, w, h) : 0; }
	if (!px && (eq (name, "onyx") || eq (name, "pi-firmware"))) px = ui::bmp_decode ("SD:/apps/control.app/icon.bmp", w, h);
	return px;
}

// Rebuild the rows from the manager (no job running)
static void snapshot (void)
{
	Manager &m = *g_M;
	for (int i = 0; i < g_nrows; i++) delete [] g_rows[i].icon;
	free (g_rows); g_rows = 0; g_nrows = 0;
	int cap = m.index.n + m.db.n + 1;
	g_rows = (Row *) calloc ((size_t) cap, sizeof (Row));
	auto add = [&] (const char *name) -> Row &
	{
		Row &r = g_rows[g_nrows++];
		cpy (r.name, name, sizeof r.name); cpy (r.title, name, sizeof r.title); cpy (r.mode, "manual", sizeof r.mode);
		r.icon = load_icon (name, &r.iw, &r.ih);
		return r;
	};
	for (int i = 0; i < m.db.n; i++)
	{
		Inst &in = *m.db.v[i];
		Row &r = add (in.name);
		cpy (r.title, in.ini.get ("package", "title", in.name), sizeof r.title);
		cpy (r.summary, in.ini.get ("package", "summary"), sizeof r.summary);
		cpy (r.category, in.ini.get ("package", "category"), sizeof r.category);
		cpy (r.inst, in.version (), sizeof r.inst); cpy (r.mode, in.mode (), sizeof r.mode);
		r.isInst = true; r.required = in.required (); r.staged = m.staged (in.name);
		for (int k = 0; k < in.ini.n; k++) if (eq (in.ini.kv[k].sec, "files")) r.installed++;
	}
	for (int i = 0; i < m.index.n; i++)
	{
		const Pkg &p = m.index.p[i];
		Row *r = 0;
		for (int k = 0; k < g_nrows; k++) if (eq (g_rows[k].name, p.name)) r = &g_rows[k];
		if (!r) { r = &add (p.name); r->restart = p.restart; }
		cpy (r->title, p.title, sizeof r->title);
		if (p.summary[0]) cpy (r->summary, p.summary, sizeof r->summary);
		cpy (r->category, p.category, sizeof r->category);
		cpy (r->avail, p.version, sizeof r->avail);
		r->size = p.size; r->installed = p.installed; r->restart = p.restart; r->required = p.required || r->required;
		r->hasUpdate = r->isInst && vcmp (p.version, r->inst) > 0 && !r->staged;
		r->checked = r->hasUpdate && !eq (r->mode, "never");
	}
	g_haveIndex = m.haveIndex; g_verified = m.verified; cpy (g_indexDate, m.index.date, sizeof g_indexDate);
}

static bool shown (const Row &r)
{
	if (g_tab == T_UPDATES && !r.hasUpdate) return false;
	if (g_tab == T_INSTALLED && !r.isInst) return false;
	if (g_tab == T_AVAILABLE && (r.isInst && !r.hasUpdate)) return false;
	if (g_tab == T_AVAILABLE && !r.avail[0]) return false;
	return contains_ci (r.name, g_search) || contains_ci (r.title, g_search) || contains_ci (r.category, g_search) || contains_ci (r.summary, g_search);
}
static void refilter (void)
{
	g_nvis = 0;
	for (int i = 0; i < g_nrows && g_nvis < 512; i++) if (shown (g_rows[i])) g_vis[g_nvis++] = i;
	if (g_tab == T_UPDATES)						// the system first
		for (int i = 1; i < g_nvis; i++) for (int j = i; j > 0 && g_rows[g_vis[j]].required && !g_rows[g_vis[j - 1]].required; j--) { int t = g_vis[j]; g_vis[j] = g_vis[j - 1]; g_vis[j - 1] = t; }
}
static int count (int tab) { int t = g_tab; g_tab = tab; int n = 0; for (int i = 0; i < g_nrows; i++) if (shown (g_rows[i])) n++; g_tab = t; return n; }
static int checked_count (u64 *bytes)
{
	int n = 0; if (bytes) *bytes = 0;
	for (int i = 0; i < g_nrows; i++) if (g_rows[i].hasUpdate && g_rows[i].checked) { n++; if (bytes) *bytes += g_rows[i].size; }
	return n;
}
static bool any_staged (void) { for (int i = 0; i < g_nrows; i++) if (g_rows[i].staged) return true; return false; }
static Row *row_of (const char *name) { for (int i = 0; i < g_nrows; i++) if (eq (g_rows[i].name, name)) return &g_rows[i]; return 0; }

// ---- the job: in a thread; what it says read by the window each frame ------------------------------------------
enum { J_CHECK, J_INSTALL, J_REMOVE, J_MODE };
struct Job
{
	int kind; char names[64][40]; int n; char mode[8];
	volatile bool running, done, cancel;
	int tid;
	// what it says (the window reads)
	char cur[40]; volatile int pct; char msg[200]; volatile int rc;
	char failed[64][40]; int nfailed;
	char staged[64][40]; int nstaged;
	char finished[96][40]; volatile int nfinished;	// done, in their order
};
static Job g_job;
struct JobReport : Report
{
	void say (const char *s) override
	{
		cpy (g_job.msg, s, sizeof g_job.msg);
		const char *st = strstr (s, "staged");
		if (st) { char nm[40]; int k = 0; while (s[k] && s[k] != ' ' && k < 39) { nm[k] = s[k]; k++; } nm[k] = 0; if (g_job.nstaged < 64) cpy (g_job.staged[g_job.nstaged++], nm, 40); }
	}
	void step (const char *what, u64 done, u64 total) override
	{
		(void) what;
		g_job.pct = total ? (int) (done * 100 / total) : 0;
		if (g_job.pct > 100) g_job.pct = 100;
	}
	bool cancelled () override { return g_job.cancel; }
};

static void fetch_icons (Report &r)
{
	kapi_mkdir (PKG_VAR "/icons");
	for (int i = 0; i < g_M->index.n && !g_job.cancel; i++)
	{
		const Pkg &p = g_M->index.p[i];
		if (!p.icon[0]) continue;
		char path[200]; snprintf (path, sizeof path, PKG_VAR "/icons/%s.bmp", p.name);
		char app[200]; snprintf (app, sizeof app, "SD:/apps/%s.app/icon.bmp", p.name);
		if (exists (path) || exists (app)) continue;
		u64 n = 0; char *b = g_M->fetch (p.icon, &n, 0, r);
		if (b) { write_file (path, b, n); free (b); }
	}
}

static int job_thread (void *)
{
	JobReport r;
	Manager &m = *g_M;
	int rc = OK;
	if (g_job.kind == J_CHECK)
	{
		cpy (g_job.cur, "index", sizeof g_job.cur);
		rc = m.refresh (r);
		if (m.haveIndex) fetch_icons (r);
	}
	else if (g_job.kind == J_INSTALL)
	{
		const Pkg *list[96]; int n = 0;
		for (int i = 0; i < g_job.n && !g_job.cancel; i++)
		{
			Inst *in = m.db.find (g_job.names[i]);
			const Pkg *p = m.index.find (g_job.names[i]);
			if (!p) continue;
			if (in) { list[n++] = p; continue; }
			int err = 0; const Pkg *sub[32];
			int k = m.resolve (g_job.names[i], sub, 32, &err, r);
			for (int j = 0; j < k && n < 96; j++) { bool dup = false; for (int q = 0; q < n; q++) if (list[q] == sub[j]) dup = true; if (!dup) list[n++] = sub[j]; }
			if (!k) { rc = err; if (g_job.nfailed < 64) cpy (g_job.failed[g_job.nfailed++], g_job.names[i], 40); }
		}
		for (int i = 1; i < n; i++)			// the apps first, the system's packages last
			for (int j = i; j > 0 && list[j - 1]->restart && !list[j]->restart; j--) { const Pkg *t = list[j]; list[j] = list[j - 1]; list[j - 1] = t; }
		for (int i = 0; i < n && !g_job.cancel; i++)
		{
			cpy (g_job.cur, list[i]->name, sizeof g_job.cur); g_job.pct = 0;
			int x = m.install (*list[i], r);
			if (x != OK) { rc = x; if (g_job.nfailed < 64) cpy (g_job.failed[g_job.nfailed++], list[i]->name, 40); }
			if (g_job.nfinished < 96) { cpy (g_job.finished[g_job.nfinished], list[i]->name, 40); g_job.nfinished++; }
		}
		if (g_job.cancel && rc == OK) rc = E_CANCEL;
	}
	else if (g_job.kind == J_REMOVE)
	{
		cpy (g_job.cur, g_job.names[0], sizeof g_job.cur);
		rc = m.remove (g_job.names[0], false, r);
	}
	else if (g_job.kind == J_MODE)
	{
		if (!m.set_mode (g_job.names[0], g_job.mode)) rc = E_NOTINST;
	}
	m.refresh_desktop ();				// (an app installed or removed: the dock started again)
	g_job.rc = rc;
	g_job.cur[0] = 0;
	g_job.done = true;
	return 0;
}
static bool start_job (int kind, const char *const *names, int n, const char *mode = 0)
{
	if (g_job.running) return false;
	memset (&g_job, 0, sizeof g_job);
	g_job.kind = kind; g_job.running = true;
	for (int i = 0; i < n && i < 64; i++) cpy (g_job.names[g_job.n++], names[i], 40);
	if (mode) cpy (g_job.mode, mode, sizeof g_job.mode);
	for (int i = 0; i < g_nrows; i++) { g_rows[i].state = ST_NONE; g_rows[i].pct = 0; }
	if (kind == J_INSTALL) for (int i = 0; i < n; i++) { Row *r = row_of (names[i]); if (r) r->state = ST_WAIT; }
	g_job.tid = kapi_thread_create (job_thread, 0, 0, "pkgjob");
	if (g_job.tid < 0) { g_job.running = false; cpy (g_status, "Cannot start the job (no thread).", sizeof g_status); return false; }
	return true;
}

// ---- drawing helpers -------------------------------------------------------------------------------------------
static void small (Canvas &cv, int x, int y, const char *s, unsigned c, int style = 0)
{
	if (g_small) g_small->draw (cv, x, y, s, c, style); else wk_text (cv, x, y, s, c, style);
}
static int small_w (const char *s, int style = 0) { return g_small ? g_small->width (s, style) : wk_tw (s, style); }
static void blit_icon (Canvas &cv, const Row &r, int x, int y, int s)
{
	if (!r.icon) { wk_rbox (cv, x + 2, y + 2, s - 4, s - 4, 6, wk_tone (C_ACCENT, 150), C_ACCENT); return; }
	for (int j = 0; j < s; j++)
		for (int k = 0; k < s; k++)
		{
			int sx = k * r.iw / s, sy = j * r.ih / s;
			unsigned c = r.icon[sy * r.iw + sx] & 0xFFFFFF;
			if (c == 0xFF00FF) continue;
			cv.fillRect (x + k, y + j, 1, 1, c);
		}
}
// a small pill button: -> its width
static int pill (Canvas &cv, int x, int y, int w, int h, const char *s, int state, bool accent = false)
{
	int lx, ly, lw, lh;
	wk_framed (cv, x, y, w, h, accent ? C_ACCENT : C_BUTTON, state, &lx, &ly, &lw, &lh);
	wk_text_c (cv, lx, ly, lw, lh, s, accent ? C_SEL_TEXT : (state == WK_DISABLED ? C_DIS : C_BUTTON_TEXT), accent ? 2 : 0);
	return w;
}
static void badge (Canvas &cv, int x, int y, const char *s, unsigned bg)
{
	int w = small_w (s, 2) + 12;
	wk_rbox (cv, x, y, w, 16, 8, bg, bg);
	small (cv, x + 6, y + 1, s, 0xFFFFFF, 2);
}

// ---- the tabs ---------------------------------------------------------------------------------------------------
// wtk's segmented control (as the Calendar's Day / Week / Month), each segment its number of packages.
static SegmentedControl *g_tabs;
static void tabs_labels ()
{
	static const char *const names[] = { "Updates", "Installed", "Available" };
	static char lab[3][32]; const char *p[3];
	bool same = g_tabs->count () == 3;
	for (int i = 0; i < 3; i++)
	{
		snprintf (lab[i], sizeof lab[i], "%s (%d)", names[i], count (i)); p[i] = lab[i];
		if (same && strcmp (lab[i], g_tabs->label (i))) same = false;
	}
	if (!same) g_tabs->setLabels (p, 3);
}
static void on_tab (Widget &)
{
	if (g_tabs->selected < 0 || g_tabs->selected == g_tab) return;
	g_tab = g_tabs->selected; refilter ();
	extern void app_tab_changed (); app_tab_changed ();
}

// ---- the list ---------------------------------------------------------------------------------------------------
class PkgList : public Widget
{
public:
	int top, hotRow, hotPart; bool wasDown; int pressRow, pressPart;
	PkgList (int l, int t, int w, int h) : Widget (l, t, w, h), top (0), hotRow (-1), hotPart (0), wasDown (false), pressRow (-1), pressPart (0) {}
	unsigned bgColor () override { return C_FIELD; }
	int banner () { return any_staged () && g_tab == T_UPDATES ? 64 : 0; }
	int rows_h () { return banner () + g_nvis * ROWH + 8; }
	void clamp () { int mx = rows_h () - height; if (mx < 0) mx = 0; if (top > mx) top = mx; if (top < 0) top = 0; }
	// the parts of a row: 1 its box, 2 its button (Install / Update / Remove), 10..12 the mode pills, 3 the row
	int partAt (int vi, int x, int y)
	{
		const Row &r = g_rows[g_vis[vi]];
		int rw = width - 14;
		if (g_tab == T_UPDATES && x >= 8 && x < 34) return 1;
		if (g_tab == T_INSTALLED)
		{
			int bx = rw - 92; if (!r.required && x >= bx && x < bx + 84 && y >= 14 && y < 44) return 2;
			int mx = rw - 92 - 8 - 186;
			if (y >= 16 && y < 42 && x >= mx && x < mx + 186) return 10 + (x - mx) / 62;
		}
		if (g_tab == T_AVAILABLE && x >= rw - 96 && x < rw - 8 && y >= 14 && y < 44) return 2;
		return 3;
	}
	void onDraw () override
	{
		clamp ();
		canvas.fillRect (0, 0, width, height, C_FIELD);
		int y = 4 - top;
		int rw = width - 14;
		if (banner ())
		{
			wk_rbox (canvas, 6, y, rw - 6, 56, 8, wk_mix (C_FIELD, 0xE2A03A, 40), wk_mix (C_FIELD, 0xE2A03A, 40));
			wk_rline (canvas, 6, y, rw - 6, 56, 8, wk_mix (C_FIELD, 0xE2A03A, 160));
			wk_text (canvas, 18, y + 8, "Restart to finish the system update", C_FIELD_TEXT, 2);
			small (canvas, 18, y + 30, "It is moved in at the next boot (the previous kernel kept as .old).", wk_mix (C_FIELD, C_FIELD_TEXT, 150));
			pill (canvas, rw - 100, y + 14, 90, 28, "Restart", hotRow == -2 ? WK_HOT : WK_NORMAL, true);
			y += 64;
		}
		if (!g_nvis)
		{
			const char *t = g_tab == T_UPDATES ? (g_haveIndex ? "Everything is up to date." : "The repository has not been read yet: Check Now.")
				      : g_tab == T_INSTALLED ? "Nothing installed matches." : (g_haveIndex ? "Nothing more to install." : "The repository has not been read yet: Check Now.");
			wk_text_c (canvas, 0, y + 30, width, 24, t, C_DIS);
		}
		for (int vi = 0; vi < g_nvis; vi++, y += ROWH)
		{
			if (y + ROWH < 0 || y > height) continue;
			const Row &r = g_rows[g_vis[vi]];
			bool hot = vi == hotRow;
			if (hot) wk_rbox (canvas, 4, y + 1, rw - 2, ROWH - 2, 6, wk_mix (C_FIELD, C_ACCENT, 26), wk_mix (C_FIELD, C_ACCENT, 26));
			if (vi + 1 < g_nvis) canvas.fillRect (12, y + ROWH - 1, rw - 16, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 30));
			int x = 10;
			unsigned dim = wk_mix (C_FIELD, C_FIELD_TEXT, 150);
			if (g_tab == T_UPDATES)
			{
				if (r.state == ST_NONE) wk_check_mark (canvas, x, y + (ROWH - 16) / 2, 16, r.checked, hot && hotPart == 1 ? WK_HOT : WK_NORMAL);
				x += 26;
			}
			blit_icon (canvas, r, x, y + (ROWH - ICON) / 2, ICON);
			x += ICON + 10;
			// the title, the versions
			wk_text (canvas, x, y + 9, r.title, C_FIELD_TEXT, 2);
			int vx = x + wk_tw (r.title, 2) + 8;
			char v[64];
			if ((g_tab == T_UPDATES || (g_tab == T_AVAILABLE && r.hasUpdate)) && r.hasUpdate)
			{
				snprintf (v, sizeof v, "%s  \xe2\x86\x92  %s", r.inst, r.avail);
				small (canvas, vx, y + 11, v, dim);
				vx += small_w (v) + 8;
			}
			else if (r.staged && r.avail[0] && vcmp (r.avail, r.inst) > 0) { snprintf (v, sizeof v, "%s  \xe2\x86\x92  %s", r.inst, r.avail); small (canvas, vx, y + 11, v, dim); vx += small_w (v) + 8; }
			else { snprintf (v, sizeof v, "%s", r.isInst ? r.inst : r.avail); small (canvas, vx, y + 11, v, dim); vx += small_w (v) + 8; }
			if (r.restart && (g_tab == T_UPDATES || r.staged)) badge (canvas, vx, y + 10, r.staged ? "staged" : "restart", 0xE2A03A);
			// the second line: the summary (or the files)
			char line[160]; char sz[24]; size_text (g_tab == T_INSTALLED ? r.installed : r.size, sz, sizeof sz);
			if (g_tab == T_INSTALLED && (r.required || r.summary[0])) snprintf (line, sizeof line, "%s  -  %s", r.category, r.required ? "part of the system" : r.summary);
			else if (g_tab == T_INSTALLED) snprintf (line, sizeof line, "%s", r.category);
			else snprintf (line, sizeof line, "%s", r.summary[0] ? r.summary : r.category);
			char fit[160]; int maxw = (g_tab == T_INSTALLED ? rw - 92 - 8 - 186 - 12 : rw - 110) - x;
			wk_text_fit (line, maxw, fit, sizeof fit);
			if (g_small) { int k = 0; while (fit[k] && small_w (fit) > maxw && k < 300) { int l = (int) strlen (fit); if (l > 4) { fit[l - 4] = '.'; fit[l - 3] = '.'; fit[l - 2] = '.'; fit[l - 1] = 0; } k++; } }
			small (canvas, x, y + 32, fit, dim);
			// the right side
			if (r.state == ST_WORK)
			{
				wk_progress_bar (canvas, rw - 170, y + 22, 110, 12, r.pct * 110 / 100);
				char p[8]; snprintf (p, sizeof p, "%d %%", r.pct); small (canvas, rw - 50, y + 21, p, C_ACCENT, 2);
				continue;
			}
			auto right = [&] (const char *t, unsigned c, int st) { small (canvas, rw - 12 - small_w (t, st), y + 21, t, c, st); };
			if (r.state == ST_WAIT) { right ("Waiting", C_DIS, 0); continue; }
			if (r.state == ST_DONE) { right ("Installed", 0x3E8A4E, 2); continue; }
			if (r.state == ST_STAGED) { right ("Ready: at the restart", 0xC07A1A, 2); continue; }
			if (r.state == ST_FAILED) { right ("Failed", 0xC84A40, 2); continue; }
			if (g_tab == T_UPDATES) { char s2[24]; size_text (r.size, s2, sizeof s2); small (canvas, rw - 10 - small_w (s2), y + 21, s2, dim); }
			else if (g_tab == T_INSTALLED)
			{
				int mx = rw - 92 - 8 - 186;
				static const char *const modes[] = { "Manual", "Auto", "Never" }, *const keys[] = { "manual", "auto", "never" };
				wk_rbox (canvas, mx, y + 16, 186, 26, 13, wk_tone (C_FIELD, 110), wk_tone (C_FIELD, 110));
				for (int k = 0; k < 3; k++)
				{
					bool on = eq (r.mode, keys[k]);
					if (on) wk_rbox (canvas, mx + 2 + k * 62, y + 18, 58, 22, 11, wk_tone (C_ACCENT, 150), C_ACCENT);
					else if (hot && hotPart == 10 + k) wk_rbox (canvas, mx + 2 + k * 62, y + 18, 58, 22, 11, wk_tone (C_FIELD, 150), wk_tone (C_FIELD, 140));
					int tw = small_w (modes[k], on ? 2 : 0);
					small (canvas, mx + 2 + k * 62 + (58 - tw) / 2, y + 22, modes[k], on ? C_SEL_TEXT : C_FIELD_TEXT, on ? 2 : 0);
				}
				if (!r.required) pill (canvas, rw - 92, y + 14, 84, 30, "Remove", g_job.running ? WK_DISABLED : hot && hotPart == 2 ? WK_HOT : WK_NORMAL);
			}
			else
			{
				bool upd = r.hasUpdate;
				pill (canvas, rw - 96, y + 14, 88, 30, upd ? "Update" : "Install", g_job.running ? WK_DISABLED : hot && hotPart == 2 ? WK_HOT : WK_NORMAL, !upd);
				char s2[24]; size_text (r.size, s2, sizeof s2); small (canvas, rw - 106 - small_w (s2), y + 21, s2, dim);
			}
		}
		// the scroll bar
		int total = rows_h ();
		if (total > height)
		{
			WkThumb t = wk_thumb (total, height, top, height - 4);
			wk_draw_vscroll (canvas, width - 12, 2, 10, height - 4, t, C_FIELD);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0 || my < 0 || mx >= width || my >= height) { if (hotRow != -1) { hotRow = -1; invalidate (true); } wasDown = false; return false; }
		if (wheel) { top -= wheel * ROWH; clamp (); invalidate (true); return true; }
		int y = my + top - 4, b = banner ();
		int row = -1, part = 0;
		if (b && y >= 0 && y < 56) { int rw = width - 14; if (mx >= rw - 100 && mx < rw - 10 && y >= 14 && y < 42) row = -2; }
		else { int k = (y - b) / ROWH; if (y >= b && k < g_nvis) { row = k; part = partAt (k, mx, (y - b) % ROWH); } }
		if (row != hotRow || part != hotPart) { hotRow = row; hotPart = part; invalidate (true); }
		if (bl && !wasDown) { pressRow = row; pressPart = part; }
		if (!bl && wasDown && row == pressRow && part == pressPart) { extern void app_click (int, int); app_click (row, part); }
		wasDown = bl != 0;
		return true;
	}
};

static PkgList *g_list; static Textbox *g_find;
static Button *g_btnCheck, *g_btnMain;
static Label *g_lblStatus;

void app_tab_changed ()
{
	g_list->top = 0;
	g_btnMain->hidden = g_tab != T_UPDATES;
	g_btnMain->invalidate (true);
	g_list->invalidate (true);
	if (g_btnMain->parent) g_btnMain->parent->invalidate (true);
}
static void update_main_button (void)
{
	u64 b; int n = checked_count (&b);
	static char t[48];
	snprintf (t, sizeof t, n ? "Install %d Update%s" : "Install Updates", n, n == 1 ? "" : "s");
	cpy (g_btnMain->text, t, sizeof g_btnMain->text);
	g_btnMain->disabled = !n || g_job.running;
	g_btnMain->invalidate (true);
	g_btnCheck->disabled = g_job.running;
	g_btnCheck->invalidate (true);
}
static void set_status (const char *s)
{
	cpy (g_status, s, sizeof g_status);
	char fit[200]; wk_text_fit (s, g_lblStatus->width, fit, sizeof fit);
	g_lblStatus->setText (fit);
}
static void status_idle (void)
{
	char t[200];
	if (!g_haveIndex) snprintf (t, sizeof t, "The repository has not been read yet.");
	else snprintf (t, sizeof t, "%d installed  -  index of %s%s", count (T_INSTALLED), g_indexDate, g_verified ? ", signed" : " (the last one read)");
	set_status (t);
}

static void install_names (const char *const *names, int n)
{
	if (start_job (J_INSTALL, names, n)) { set_status ("Installing..."); update_main_button (); g_list->invalidate (true); }
}
void app_click (int row, int part)
{
	if (row == -2)
	{
		if (wk_messagebox ("Restart", "Restart Onyx now to finish the system update?", MB_YESNO)) kapi_reboot ();
		return;
	}
	if (row < 0 || row >= g_nvis || g_job.running) return;
	Row &r = g_rows[g_vis[row]];
	if (g_tab == T_UPDATES && (part == 1 || part == 3)) { r.checked = !r.checked; update_main_button (); g_list->invalidate (true); return; }
	if (g_tab == T_AVAILABLE && part == 2) { const char *n[1] = { r.name }; install_names (n, 1); return; }
	if (g_tab == T_INSTALLED && part >= 10 && part <= 12)
	{
		static const char *const keys[] = { "manual", "auto", "never" };
		if (eq (r.mode, keys[part - 10])) return;
		cpy (r.mode, keys[part - 10], sizeof r.mode);
		const char *n[1] = { r.name }; start_job (J_MODE, n, 1, keys[part - 10]);
		g_list->invalidate (true); return;
	}
	if (g_tab == T_INSTALLED && part == 2 && !r.required)
	{
		char q[160]; snprintf (q, sizeof q, "Remove %s %s? (the settings you changed are kept)", r.title, r.inst);
		if (!wk_messagebox ("Remove", q, MB_YESNO)) return;
		const char *n[1] = { r.name };
		if (start_job (J_REMOVE, n, 1)) { set_status ("Removing..."); update_main_button (); }
	}
}
static void on_check (Widget &) { if (start_job (J_CHECK, 0, 0)) { set_status ("Reading the repository..."); update_main_button (); } }
static void on_install (Widget &)
{
	const char *names[64]; int n = 0;
	for (int i = 0; i < g_nrows && n < 64; i++) if (g_rows[i].hasUpdate && g_rows[i].checked) names[n++] = g_rows[i].name;
	if (n) install_names (names, n);
}

// ---- the window: the job's progress each frame, the search ------------------------------------------------------
class App : public Root
{
public:
	App (int w, int h) : Root (w, h, "Onyx Package Manager") {}
	void onTick () override
	{
		if (strcmp (g_search, g_find->text)) { cpy (g_search, g_find->text, sizeof g_search); refilter (); tabs_labels (); g_list->top = 0; g_list->invalidate (true); }
		if (!g_job.running) return;
		if (g_job.done)
		{
			kapi_thread_join (g_job.tid, 1000, 0);
			g_job.running = false;
			int rc = g_job.rc; char msg[200]; cpy (msg, g_job.msg, sizeof msg);
			int kind = g_job.kind;
			// the states to keep shown on the rows after the rebuild
			char done[64][40]; int nd = 0;
			if (kind == J_INSTALL) for (int i = 0; i < g_job.n; i++) cpy (done[nd++], g_job.names[i], 40);
			g_M->db.load ();
			snapshot (); refilter ();
			for (int i = 0; i < nd; i++)
			{
				Row *r = row_of (done[i]); if (!r) continue;
				bool failed = false; for (int k = 0; k < g_job.nfailed; k++) if (eq (g_job.failed[k], done[i])) failed = true;
				r->state = failed ? ST_FAILED : r->staged ? ST_STAGED : ST_DONE;
			}
			if (kind == J_INSTALL && g_tab == T_UPDATES)		// keep the rows just updated in view
				for (int i = 0; i < nd; i++) { Row *r = row_of (done[i]); if (r && !r->hasUpdate) { int k = (int) (r - g_rows); bool in = false; for (int j = 0; j < g_nvis; j++) if (g_vis[j] == k) in = true; if (!in && g_nvis < 512) g_vis[g_nvis++] = k; } }
			if (rc == OK && kind != J_INSTALL) status_idle ();
			else if (rc == OK) set_status (any_staged () ? "Done. The system update waits for the restart." : "Done: the apps open with their new version next time.");
			else if (rc == E_CANCEL) set_status ("Cancelled.");
			else set_status (msg[0] ? msg : "It did not work.");
			tabs_labels (); g_list->invalidate (true); update_main_button ();
			return;
		}
		// the progress
		static int lastPct = -1, lastFin = -1; static char lastCur[40];
		if (g_job.pct != lastPct || strcmp (lastCur, g_job.cur) || g_job.nfinished != lastFin)
		{
			lastPct = g_job.pct; lastFin = g_job.nfinished; cpy (lastCur, g_job.cur, sizeof lastCur);
			for (int k = 0; k < g_job.nfinished; k++)
			{
				Row *r = row_of (g_job.finished[k]); if (!r) continue;
				bool failed = false; for (int q = 0; q < g_job.nfailed; q++) if (eq (g_job.failed[q], r->name)) failed = true;
				r->state = failed ? ST_FAILED : r->restart ? ST_STAGED : ST_DONE;
			}
			Row *c = g_job.cur[0] ? row_of (g_job.cur) : 0;
			if (c && c->state != ST_DONE && c->state != ST_STAGED && c->state != ST_FAILED) { c->state = ST_WORK; c->pct = g_job.pct; }
			char t[200];
			if (g_job.kind == J_INSTALL && g_job.cur[0]) { Row *r = row_of (g_job.cur); snprintf (t, sizeof t, "Installing %s: %d %%", r ? r->title : g_job.cur, g_job.pct); set_status (t); }
			g_list->invalidate (true);
		}
	}
};

int main (void)
{
	char args[64]; int an = kapi_get_args (args, sizeof args); args[an > 0 && an < 64 ? an : 0] = 0;
	ft_wtk_install ("DejaVu Sans", 13);
	g_small = new FtTextFace; if (!g_small->open ("DejaVu Sans", 11)) g_small = 0;
	static Manager m; g_M = &m;
	m.load_cached_quiet ();
	snapshot ();
	if (strstr (args, "--updates")) g_tab = T_UPDATES;
	else if (!count (T_UPDATES) && g_haveIndex) g_tab = T_INSTALLED;
	refilter ();
	App root (W, H);
	if (root.canvas.px == 0) return 1;
	int X = root.width > W ? (root.width - W) / 2 : 0;
	g_tabs = new SegmentedControl (X + 12, 10, 420, TABH, 0, 0, g_tab, on_tab); tabs_labels (); g_tabs->select (g_tab); root.addChild (g_tabs);
	g_find = new Textbox (X + W - 12 - 210, 10, 210, TABH, "", 0); root.addChild (g_find);
	g_list = new PkgList (X + 12, 10 + TABH + 8, W - 24, H - (10 + TABH + 8) - FOOT); root.addChild (g_list);
	g_lblStatus = new Label (X + 14, H - FOOT + 12, W - 24 - 300, 22, "", C_DIS, root.bg); root.addChild (g_lblStatus);
	g_btnCheck = new Button (X + W - 12 - 290, H - FOOT + 8, 110, 30, "Check Now", on_check); root.addChild (g_btnCheck);
	g_btnMain = new Button (X + W - 12 - 172, H - FOOT + 8, 172, 30, "Install Updates", on_install); root.addChild (g_btnMain);
	g_btnMain->hidden = g_tab != T_UPDATES;
	status_idle (); update_main_button ();
	on_check (*g_btnCheck);					// the repository read again at once
	root.run ();
	if (g_job.running) { g_job.cancel = true; kapi_thread_join (g_job.tid, 3000, 0); }
	return 0;
}
