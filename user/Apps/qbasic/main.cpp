//
// qbasic -- the Onyx BASIC editor (QBasic style), on wtk with the global menu bar.
//
//   * The program is split into MODULES, edited one at a time: the main module and each
//     SUB / FUNCTION (View > SUBs... / ^L lists them; Edit > New SUB... / New FUNCTION...
//     creates one). On disk it is one ordinary .bas file: the main module, then every
//     SUB / FUNCTION block (what QBasic writes too).
//   * Run > Start (^R) checks the syntax (the compiler is linked in) -- an error jumps to
//     its module and line -- then saves a copy to SD:/tmp and runs it with /bin/basic -i:
//     a runtime error comes back over IPC (service "qbasic") and jumps the same way.
//   * File > Make App... turns the program into an app: SD:/apps/<name>.app/main.bas +
//     app.txt (+ an icon), launched like any app (the kernel hands main.bas to /bin/basic).
//   * Enter keeps the indentation of the line above; Page Up / Down scroll by a page.
//
#include "kapi.h"
#include "applib.h"
#include "fsutil.h"
#include "notify.h"
#include "wtk/wtk.h"
#include "basic/bas.h"

using namespace wtk;

#define W	760
#define H	540
#define HEAD_H	22
#define ST_H	20
#define MAXMOD	64
#define EDCAP	(128 * 1024)

static int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static char up (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }
static bool starts_kw (const char *p, const char *kw)	// "SUB" at p, then a blank / end
{
	int i = 0; while (kw[i]) { if (up (p[i]) != kw[i]) return false; i++; }
	return p[i] == ' ' || p[i] == '\t' || p[i] == 0 || p[i] == '\n' || p[i] == '\r' || p[i] == '(';
}

// ---- the modules ------------------------------------------------------------------------------
struct Module { char name[48]; int kind; char *text; };		// kind 0 main, 1 SUB, 2 FUNCTION
static Module g_mod[MAXMOD]; static int g_nmod = 0, g_cur = 0;
static char g_path[256] = "";
static bool g_dirty = false;

class CodeArea;
static CodeArea *g_ed = 0;
static Label *g_head = 0, *g_status = 0;
static Root *g_root = 0;

static void (*g_fkey[12]) (void);			// QBasic's keys: F1 help, F2 SUBs, F5 run
class CodeArea : public Textarea
{
public:
	CodeArea (int l, int t, int w, int h) : Textarea (l, t, w, h, EDCAP) {}
	bool onKey (long k) override
	{
		if (k >= KEY_F1 && k <= KEY_F12) { if (g_fkey[k - KEY_F1]) g_fkey[k - KEY_F1] (); return true; }
		if (k == KEY_ENTER && !readonly)			// keep the indentation
		{
			int ls = caret; while (ls > 0 && buf[ls - 1] != '\n') ls--;
			char ind[64]; int n = 0;
			while (n < 63 && (buf[ls + n] == ' ' || buf[ls + n] == '\t') && ls + n < caret) { ind[n] = buf[ls + n]; n++; }
			ind[n] = 0;
			Textarea::onKey (KEY_ENTER);
			insertText (ind);
			g_dirty = true;
			return true;
		}
		if (k == KEY_PGUP || k == KEY_PGDN)
		{
			int r = rows > 1 ? rows - 1 : 1;
			for (int i = 0; i < r; i++) Textarea::onKey (k == KEY_PGUP ? KEY_UP : KEY_DOWN);
			return true;
		}
		bool r = Textarea::onKey (k);
		if (r && ((k >= 32 && k < 127) || k == KEY_BACKSPACE || k == KEY_DEL || k == KEY_TAB || k == WK_CTRL ('X') || k == WK_CTRL ('V'))) g_dirty = true;
		return r;
	}
};

static void set_status (const char *a, const char *b = "")
{
	char s[200]; scpy (s, a, sizeof s);
	int n = slen (s); for (int i = 0; b[i] && n < 198; i++) s[n++] = b[i];
	s[n] = 0;
	g_status->setText (s);
}

static void free_modules () { for (int i = 0; i < g_nmod; i++) delete [] g_mod[i].text; g_nmod = 0; }
static char *dup_range (const char *s, int n) { char *d = new char[n + 1]; for (int i = 0; i < n; i++) d[i] = s[i]; d[n] = 0; return d; }

// Split a whole program into modules.
static void split (const char *src)
{
	free_modules ();
	int n = slen (src);
	// main module = every line not inside a SUB / FUNCTION block
	char *main = new char[n + 1]; int mn = 0;
	int i = 0;
	while (i < n)
	{
		int ls = i; while (i < n && src[i] != '\n') i++;
		int le = i; if (i < n) i++;
		const char *p = src + ls; while (*p == ' ' || *p == '\t') p++;
		int kind = starts_kw (p, "SUB") ? 1 : starts_kw (p, "FUNCTION") ? 2 : 0;
		if (kind && g_nmod < MAXMOD - 1)
		{
			// name
			const char *q = p + (kind == 1 ? 3 : 8); while (*q == ' ') q++;
			char name[48]; int k = 0;
			while (*q && *q != ' ' && *q != '(' && *q != '\n' && *q != '\r' && k < 47) name[k++] = *q++;
			name[k] = 0;
			// up to END SUB / END FUNCTION (inclusive)
			int bs = ls;
			for (;;)
			{
				if (i >= n) break;
				int l2 = i; while (i < n && src[i] != '\n') i++;
				int e2 = i; if (i < n) i++;
				const char *r = src + l2; while (*r == ' ' || *r == '\t') r++;
				if (starts_kw (r, "END"))
				{
					const char *t = r + 3; while (*t == ' ') t++;
					if (starts_kw (t, kind == 1 ? "SUB" : "FUNCTION")) { (void) e2; break; }
				}
			}
			int be = i; while (be > bs && (src[be - 1] == '\n' || src[be - 1] == '\r')) be--;
			Module md; scpy (md.name, name, sizeof md.name); md.kind = kind; md.text = dup_range (src + bs, be - bs);
			if (g_nmod == 0) g_nmod = 1;				// slot 0 = main (filled below)
			g_mod[g_nmod++] = md;
			continue;
		}
		for (int j = ls; j < le; j++) if (src[j] != '\r') main[mn++] = src[j];
		main[mn++] = '\n';
	}
	while (mn > 0 && main[mn - 1] == '\n') mn--;
	main[mn] = 0;
	if (g_nmod == 0) g_nmod = 1;
	scpy (g_mod[0].name, "(main)", sizeof g_mod[0].name); g_mod[0].kind = 0; g_mod[0].text = main;
}

// The editor's text back into the current module.
static void sync_current ()
{
	if (!g_ed || g_cur < 0 || g_cur >= g_nmod) return;
	delete [] g_mod[g_cur].text;
	g_mod[g_cur].text = dup_range (g_ed->content (), slen (g_ed->content ()));
	// a renamed header: follow it
	if (g_mod[g_cur].kind)
	{
		const char *p = g_mod[g_cur].text; while (*p == ' ' || *p == '\t' || *p == '\n') p++;
		const char *q = p; while (*q && *q != ' ') q++;
		while (*q == ' ') q++;
		char name[48]; int k = 0;
		while (*q && *q != ' ' && *q != '(' && *q != '\n' && k < 47) name[k++] = *q++;
		name[k] = 0;
		if (k) scpy (g_mod[g_cur].name, name, sizeof g_mod[g_cur].name);
	}
}

// Whole program: main, then each SUB / FUNCTION. starts[i] = first line (1-based) of module i.
static char *compose (int *starts)
{
	sync_current ();
	int total = 16;
	for (int i = 0; i < g_nmod; i++) total += slen (g_mod[i].text) + 4;
	char *s = new char[total]; int n = 0, line = 1;
	for (int i = 0; i < g_nmod; i++)
	{
		if (i > 0) { s[n++] = '\n'; line++; }
		if (starts) starts[i] = line;
		for (const char *p = g_mod[i].text; *p; p++) { s[n++] = *p; if (*p == '\n') line++; }
		s[n++] = '\n'; line++;
	}
	s[n] = 0;
	return s;
}

static void show_module (int i)
{
	if (i < 0 || i >= g_nmod) return;
	g_cur = i;
	g_ed->setContent (g_mod[i].text);
	char h[80];
	if (g_mod[i].kind == 0) scpy (h, "Main module", sizeof h);
	else
	{
		scpy (h, g_mod[i].kind == 1 ? "SUB " : "FUNCTION ", sizeof h);
		int n = slen (h); for (int k = 0; g_mod[i].name[k] && n < 78; k++) h[n++] = g_mod[i].name[k];
		h[n] = 0;
	}
	g_head->setText (h);
	g_ed->setFocus ();
	g_root->invalidate (true);
}

static void set_title_status ()
{
	set_status (g_path[0] ? g_path : "Untitled", g_dirty ? "  (modified)" : "");
}

// Jump to line (1-based) of the composed program.
static void goto_program_line (int line)
{
	int starts[MAXMOD]; char *s = compose (starts); delete [] s;
	int m = 0;
	for (int i = 0; i < g_nmod; i++) if (starts[i] <= line) m = i;
	show_module (m);
	g_ed->gotoLine (line - starts[m]);
}

// ---- small dialogs -----------------------------------------------------------------------------
static void dlg_btn (Widget &w) { ((Modal *) w.parent)->onButton (w.tag); }
static void dlg_enter (Widget &w) { ((Modal *) w.parent)->close (1); }

class InputBox : public Modal
{
	const char *m_title;
public:
	Textbox *tb;
	InputBox (const char *title, const char *init) : Modal (360, 120), m_title (title)
	{
		left = (W - width) / 2; top = (H - height) / 2;
		tb = new Textbox (12, wk_fh () + 16, width - 24, 26, init, dlg_enter);
		addChild (tb);
		Button *b;
		b = new Button (width - 180, height - 36, 82, 28, "OK", dlg_btn);     b->tag = 1; addChild (b);
		b = new Button (width - 92,  height - 36, 82, 28, "Cancel", dlg_btn); b->tag = 0; addChild (b);
		tb->setFocus ();
	}
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override
	{
		canvas.clear (C_FACE_DN);
		canvas.frameRect (0, 0, width, height, C_ACCENT);
		canvas.text (10, 6, m_title, C_TEXT);
	}
};
static bool ask_text (const char *title, char *out, int cap)
{
	InputBox d (title, out);
	if (!d.run ()) return false;
	scpy (out, d.tb->text, cap);
	return out[0] != 0;
}

// View > SUBs...: every module; Edit / Delete.
class SubsDialog : public Modal
{
public:
	ListBox *list;
	SubsDialog () : Modal (380, 320)
	{
		left = (W - width) / 2; top = (H - height) / 2;
		list = new ListBox (12, wk_fh () + 14, width - 24, height - wk_fh () - 64, 0, dlg_enter);
		for (int i = 0; i < g_nmod; i++)
		{
			char s[64];
			if (i == 0) scpy (s, "(main module)", sizeof s);
			else { scpy (s, g_mod[i].kind == 1 ? "SUB " : "FUNCTION ", sizeof s); int n = slen (s); for (int k = 0; g_mod[i].name[k] && n < 62; k++) s[n++] = g_mod[i].name[k]; s[n] = 0; }
			list->add (s);
		}
		list->setSel (g_cur);
		addChild (list);
		Button *b;
		b = new Button (12,           height - 40, 90, 28, "Edit", dlg_btn);   b->tag = 1; addChild (b);
		b = new Button (110,          height - 40, 90, 28, "Delete", dlg_btn); b->tag = 2; addChild (b);
		b = new Button (width - 102,  height - 40, 90, 28, "Cancel", dlg_btn); b->tag = 0; addChild (b);
		list->setFocus ();
	}
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { close (1); return true; }
		return false;
	}
	void onDraw () override
	{
		canvas.clear (C_FACE_DN);
		canvas.frameRect (0, 0, width, height, C_ACCENT);
		canvas.text (10, 6, "SUBs and FUNCTIONs", C_TEXT);
	}
};

// ---- commands ------------------------------------------------------------------------------------
static bool confirm_discard ()
{
	if (!g_dirty) return true;
	int r = wk_messagebox ("QBasic", "The program has changed. Save it first?", MB_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1)
	{
		extern void op_save ();
		op_save ();
		return !g_dirty;
	}
	return true;
}

static void load_text (const char *src)
{
	split (src);
	g_cur = 0;
	show_module (0);
	g_dirty = false;
}

static void op_new ()
{
	if (!confirm_discard ()) return;
	g_path[0] = 0;
	load_text ("");
	set_title_status ();
}

static bool load_file (const char *path)
{
	void *f = kapi_open (path);
	if (!f) { set_status ("Cannot open ", path); return false; }
	unsigned n = kapi_fsize (f);
	char *b = new char[n + 1];
	int r = kapi_read (f, b, n); kapi_close (f);
	b[r > 0 ? r : 0] = 0;
	load_text (b);
	delete [] b;
	scpy (g_path, path, sizeof g_path);
	set_title_status ();
	return true;
}

static void op_open ()
{
	if (!confirm_discard ()) return;
	char p[256];
	if (!wk_file_open (p, sizeof p, g_path[0] ? g_path : "SD:/basic")) return;
	load_file (p);
}

static bool write_to (const char *path)
{
	char *s = compose (0);
	bool ok = kapi_save_file (path, s, (unsigned) slen (s)) >= 0;
	delete [] s;
	return ok;
}

static void op_save_as ()
{
	char p[256];
	const char *base = g_path[0] ? fs_basename (g_path) : "program.bas";
	if (!wk_file_save (p, sizeof p, g_path[0] ? g_path : "SD:/basic", base)) return;
	int n = slen (p);
	if (!(n > 4 && p[n - 4] == '.' && (p[n - 3] | 32) == 'b')) { scpy (p + n, ".bas", sizeof p - n); }
	if (!write_to (p)) { set_status ("Cannot save ", p); return; }
	scpy (g_path, p, sizeof g_path);
	g_dirty = false;
	set_title_status ();
}

void op_save ()
{
	if (!g_path[0]) { op_save_as (); return; }
	if (!write_to (g_path)) { set_status ("Cannot save ", g_path); return; }
	g_dirty = false;
	set_title_status ();
}

static void new_proc (int kind)
{
	char name[48] = "";
	if (!ask_text (kind == 1 ? "Name of the new SUB" : "Name of the new FUNCTION (end with $ for text)", name, sizeof name)) return;
	for (int i = 0; name[i]; i++) if (name[i] == ' ') { set_status ("A name has no spaces"); return; }
	for (int i = 1; i < g_nmod; i++)
		if (fs_ci_cmp (g_mod[i].name, name) == 0) { show_module (i); set_status ("Already exists: ", name); return; }
	if (g_nmod >= MAXMOD) { set_status ("Too many SUBs / FUNCTIONs"); return; }
	sync_current ();
	char t[160]; int n = 0;
	const char *a = kind == 1 ? "SUB " : "FUNCTION ";
	for (int i = 0; a[i]; i++) t[n++] = a[i];
	for (int i = 0; name[i]; i++) t[n++] = name[i];
	const char *b = kind == 1 ? " ()\n    \nEND SUB" : " ()\n    \nEND FUNCTION";
	for (int i = 0; b[i]; i++) t[n++] = b[i];
	t[n] = 0;
	Module &m = g_mod[g_nmod];
	scpy (m.name, name, sizeof m.name); m.kind = kind; m.text = dup_range (t, n);
	g_nmod++;
	show_module (g_nmod - 1);
	g_ed->gotoLine (1);
	g_dirty = true;
	set_title_status ();
}
static void op_new_sub () { new_proc (1); }
static void op_new_function () { new_proc (2); }

static void op_subs ()
{
	sync_current ();
	SubsDialog d;
	int r = d.run ();
	int sel = d.list->sel;
	if (r == 1 && sel >= 0) show_module (sel);
	else if (r == 2 && sel > 0)
	{
		char q[120]; scpy (q, "Delete ", sizeof q);
		int n = slen (q); for (int k = 0; g_mod[sel].name[k] && n < 100; k++) q[n++] = g_mod[sel].name[k];
		scpy (q + n, "?", sizeof q - n);
		if (!wk_messagebox ("Delete", q, MB_YESNO)) return;
		delete [] g_mod[sel].text;
		for (int i = sel; i + 1 < g_nmod; i++) g_mod[i] = g_mod[i + 1];
		g_nmod--;
		g_dirty = true;
		show_module (0);
		set_title_status ();
	}
	else if (r == 2) set_status ("The main module cannot be deleted");
}

// Syntax check; on error jump there. true = fine.
static bool check (bool quiet)
{
	int starts[MAXMOD];
	char *s = compose (starts);
	bas::Error e;
	bas::Program *p = bas::compile (s, &e);
	delete [] s;
	if (!p)
	{
		goto_program_line (e.line);
		char m[160]; scpy (m, "Syntax error: ", sizeof m);
		int n = slen (m); for (int i = 0; e.msg[i] && n < 158; i++) m[n++] = e.msg[i];
		m[n] = 0;
		set_status (m);
		return false;
	}
	bas::destroy (p);
	if (!quiet) set_status ("No syntax errors.");
	return true;
}
static void op_check () { check (false); }

static void op_run ()
{
	if (!check (true)) return;
	kapi_mkdir ("SD:/tmp");
	char tmp[256]; scpy (tmp, "SD:/tmp/", sizeof tmp);
	const char *base = g_path[0] ? fs_basename (g_path) : "untitled.bas";
	int n = slen (tmp); for (int i = 0; base[i] && n < 250; i++) tmp[n++] = base[i];
	tmp[n] = 0;
	if (!write_to (tmp)) { set_status ("Cannot write ", tmp); return; }
	// basic -i [-d <the program's folder>] <copy>
	char args[600]; int k = 0;
	const char *o = "-i ";
	for (int i = 0; o[i]; i++) args[k++] = o[i];
	if (g_path[0])
	{
		char dir[256]; fs_dirname (dir, sizeof dir, g_path);
		const char *d = "-d \""; for (int i = 0; d[i]; i++) args[k++] = d[i];
		for (int i = 0; dir[i] && k < 400; i++) args[k++] = dir[i];
		args[k++] = '"'; args[k++] = ' ';
	}
	args[k++] = '"';
	for (int i = 0; tmp[i] && k < 590; i++) args[k++] = tmp[i];
	args[k++] = '"'; args[k] = 0;
	if (!kapi_exec ("SD:/bin/basic", args)) { set_status ("Cannot start SD:/bin/basic"); return; }
	set_status ("Running ", base);
}

static void op_goto ()
{
	char t[16] = "";
	if (!ask_text ("Go to line (of the whole program)", t, sizeof t)) return;
	int n = 0; for (int i = 0; t[i] >= '0' && t[i] <= '9'; i++) n = n * 10 + (t[i] - '0');
	if (n > 0) goto_program_line (n);
}

// File > Make App...: SD:/apps/<name>.app/{main.bas, app.txt, icon.bmp}.
static void op_make_app ()
{
	char name[40] = "";
	if (g_path[0]) { scpy (name, fs_basename (g_path), sizeof name); int n = slen (name); if (n > 4 && name[n - 4] == '.') name[n - 4] = 0; }
	if (!ask_text ("App folder name (apps/<name>.app)", name, sizeof name)) return;
	for (int i = 0; name[i]; i++) if (name[i] == ' ' || name[i] == '/' || name[i] == ':') { set_status ("No spaces or / in an app folder name"); return; }
	char title[64]; scpy (title, name, sizeof title);
	if (!ask_text ("App title (shown under its icon)", title, sizeof title)) return;
	char dir[128]; scpy (dir, "SD:/apps/", sizeof dir);
	int n = slen (dir); for (int i = 0; name[i] && n < 110; i++) dir[n++] = name[i];
	scpy (dir + n, ".app", sizeof dir - n);
	kapi_mkdir (dir);
	char p[160];
	fs_join (p, sizeof p, dir, "main.bas");
	if (!write_to (p)) { set_status ("Cannot write ", p); return; }
	char txt[200]; int k = 0;
	const char *a = "# Onyx application metadata (written by QBasic > Make App)\nname = ";
	for (int i = 0; a[i]; i++) txt[k++] = a[i];
	for (int i = 0; title[i] && k < 160; i++) txt[k++] = title[i];
	const char *b = "\ncategory = BASIC\n";
	for (int i = 0; b[i]; i++) txt[k++] = b[i];
	fs_join (p, sizeof p, dir, "app.txt");
	kapi_save_file (p, txt, (unsigned) k);
	// the icon: a copy of the BASIC program icon
	void *f = kapi_open ("SD:/apps/qbasic.app/program.bmp");
	if (f)
	{
		unsigned sz = kapi_fsize (f); char *ib = new char[sz + 1];
		int r = kapi_read (f, ib, sz); kapi_close (f);
		fs_join (p, sizeof p, dir, "icon.bmp");
		if (r > 0) kapi_save_file (p, ib, (unsigned) r);
		delete [] ib;
	}
	set_status ("App created: ", dir);
	notify ("QBasic", "App created: it is in the app list.");
}

static void op_help () { kapi_exec ("SD:/apps/tinypad.app/main", "SD:/apps/qbasic.app/help.txt"); }
static void op_examples ()
{
	if (!confirm_discard ()) return;
	char p[256];
	if (!wk_file_open (p, sizeof p, "SD:/basic/examples")) return;
	load_file (p);
}

// ---- the window --------------------------------------------------------------------------------
class IdeRoot : public Root
{
public:
	int lastLine = -1, lastCol = -1;
	IdeRoot () : Root (W, H, "QBasic") {}
	void onTick () override
	{
		// A runtime error from /bin/basic -i: "line\0message\0".
		int from, type; char m[200];
		int n = kapi_mailbox_recv (&from, &type, m, sizeof m - 1, 0);
		if (n > 0)
		{
			m[n] = 0;
			int line = 0; for (int i = 0; m[i] >= '0' && m[i] <= '9'; i++) line = line * 10 + (m[i] - '0');
			const char *msg = m + slen (m) + 1;
			if (line > 0) goto_program_line (line);
			set_status ("Runtime error: ", msg);
		}
		int l = g_ed->caretLine (), c = g_ed->caretCol ();
		if (l != lastLine || c != lastCol)
		{
			lastLine = l; lastCol = c;
			char s[40]; int k = 0; char t[12];
			int v = l + 1, q = 0; do { t[q++] = (char) ('0' + v % 10); v /= 10; } while (v); while (q) s[k++] = t[--q];
			s[k++] = ':';
			v = c + 1; q = 0; do { t[q++] = (char) ('0' + v % 10); v /= 10; } while (v); while (q) s[k++] = t[--q];
			s[k] = 0;
			pos->setText (s);
		}
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type != DND_FILES || !confirm_discard ()) return;
		char p[256]; int n = 0;
		while (data[n] && data[n] != '\n' && n < 255) { p[n] = data[n]; n++; }
		p[n] = 0;
		load_file (p);
	}
	Label *pos = 0;
};

int main (void)
{
	IdeRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setBg (C_FACE_DN);
	kapi_ipc_register ("qbasic");

	g_head = new Label (0, 0, W, HEAD_H, "Main module", C_TEXT, C_FACE);
	root.addChild (g_head);
	g_ed = new CodeArea (0, HEAD_H, W, H - HEAD_H - ST_H);
	g_ed->anchor = ANCHOR_FILL;
	root.addChild (g_ed);
	g_status = new Label (0, H - ST_H, W - 90, ST_H, "", C_TEXT, C_FACE_DN);
	root.addChild (g_status);
	root.pos = new Label (W - 90, H - ST_H, 90, ST_H, "1:1", C_TEXT, C_FACE_DN);
	root.addChild (root.pos);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New",          "^N", WK_CTRL ('N'), op_new);
	menu.item ("Open...",      "^O", WK_CTRL ('O'), op_open);
	menu.item ("Examples...",  "",   0,             op_examples);
	menu.item ("Save",         "^S", WK_CTRL ('S'), op_save);
	menu.item ("Save As...",   "",   0,             op_save_as);
	menu.separator ();
	menu.item ("Make App...",  "",   0,             op_make_app);
	menu.menu ("Edit");
	menu.item ("New SUB...",      "", 0,             op_new_sub);
	menu.item ("New FUNCTION...", "", 0,             op_new_function);
	menu.separator ();
	menu.item ("Go to Line...",   "^G", WK_CTRL ('G'), op_goto);
	menu.menu ("View");
	menu.item ("SUBs... (F2)", "^L", WK_CTRL ('L'), op_subs);
	menu.menu ("Run");
	menu.item ("Start (F5)",   "^R", WK_CTRL ('R'), op_run);
	menu.item ("Check Syntax", "^K", WK_CTRL ('K'), op_check);
	menu.menu ("Help");
	menu.item ("Keywords",     "",   0,             op_help);
	menu.publish ();
	g_fkey[0] = op_help; g_fkey[1] = op_subs; g_fkey[4] = op_run;

	char args[256];
	if (kapi_get_args (args, sizeof args) > 0 && args[0]) load_file (args);
	else { load_text ("' Welcome to Onyx BASIC. Run > Start (^R) runs the program.\nPRINT \"Hello, world!\"\n"); set_title_status (); }
	g_ed->setFocus ();
	root.run ();
	return 0;
}
