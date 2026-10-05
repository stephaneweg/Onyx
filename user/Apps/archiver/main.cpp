//
// Apps/archiver/main.cpp -- Archiver, Onyx's archive manager (docs/archiver/README.md): open an archive
// (ZIP; 7z, tar and RAR next), browse its folders, extract the selection or everything (the folders
// kept, from the current folder down, or flat), and change it -- files and folders dropped from the
// File Viewer go straight into the folder under the cursor (or the one shown); Add Files / Add Folder,
// Delete, Rename, New Folder. A file opened from the archive is extracted to RAM: and opened with its
// app; saved there, the Archiver offers to put it back. Rows dragged out of the list are extracted to
// RAM: and handed over as files. The work is done in a thread (the window stays alive), its progress
// in a box (Background / Cancel).
//
// A newlib uikit app with FreeType's text (user/Makefile's archiver.elf rule), zlib for Deflate. One
// translation unit: the engine (arc.h, zip.h, ops.h), the model (model.h), the widgets (widgets.h,
// icons.h), the dialogs (dialogs.h).
//
#include <strings.h>
#include "kapi.h"
#include "ft/uikitface.h"
#include "uikit/uikit.h"
#include "fileassoc.h"
#include "ops.h"
#include "model.h"
#include "icons.h"
#include "widgets.h"
#include "dialogs.h"

using namespace uikit;
using namespace ui;
using arc::u64;

static const int W = 960, H = 600;

// ---- the state -------------------------------------------------------------------------------------------
namespace ui {
Model g_model;
int   g_folder = 0;
TextFace *g_small = 0;
}
static arc::Archive *g_arc;			// the archive open (0: none)
static char g_password[128];
static int  g_hist[64], g_nhist;		// Back
static char g_lastDir[300] = "SD:/";
static char g_lastSearch[128];

static ToolStrip *g_tools;
static PathBar   *g_path;
static FolderTree *g_tree;
static InfoCard  *g_card;
static EntryList *g_list;
static StatusBar *g_status;
static Welcome   *g_welcome;
static Widget    *g_body;
static Root      *g_root;

// ---- small helpers -----------------------------------------------------------------------------------------
static void dirname_of (const char *path, char *out, int cap)
{
	arc::scopy (out, path, cap);
	char *s = strrchr (out, '/');
	if (s && s > out && s[-1] != ':') *s = 0;
	else if (s) s[1] = 0;
}
static void abs_pos (Widget *w, int *x, int *y)
{
	*x = *y = 0;
	for (Widget *p = w; p && p->parent; p = p->parent) { *x += p->left - p->parent->scrollX; *y += p->top - p->parent->scrollY; }
}
static bool inside (Widget *w, int x, int y, int *lx, int *ly)
{
	int ax, ay; abs_pos (w, &ax, &ay);
	*lx = x - ax; *ly = y - ay;
	return !w->hidden && *lx >= 0 && *ly >= 0 && *lx < w->width && *ly < w->height;
}
static bool writable () { return g_arc && g_arc->writable (); }
static int  sel_nodes (int *out, int cap)
{
	int n = 0;
	if (!g_list) return 0;
	for (int i = 0; i < g_list->nitems && n < cap; i++) if (g_list->sel[i] && g_list->items[i] >= 0) out[n++] = g_list->items[i];
	return n;
}
// the selection as archive paths (malloc'ed strings in names[]), -> how many
static int sel_paths (char **names, int cap)
{
	int nodes[4096]; int n = sel_nodes (nodes, cap < 4096 ? cap : 4096);
	for (int i = 0; i < n; i++) { char p[600]; g_model.pathOf (nodes[i], p, sizeof p); names[i] = arc::sdup (p); }
	return n;
}
static void free_names (char **names, int n) { for (int i = 0; i < n; i++) free (names[i]); }

// ---- recent archives (SD:/apps/archiver.app/recent.txt) ----------------------------------------------------------
static void recent_path (char *out, int cap)
{
	char d[200]; int n = kapi_app_dir (d, sizeof d); d[n > 0 && n < 200 ? n : 0] = 0;
	arc::scopy (out, d[0] ? d : "SD:/apps/archiver.app/", cap); arc::scat (out, "recent.txt", cap);
}
static void recent_load ()
{
	g_welcome->nrec = 0;
	char p[300]; recent_path (p, sizeof p);
	void *h = kapi_open (p);
	if (!h) return;
	char buf[4096]; int n = kapi_read (h, buf, sizeof buf - 1); kapi_close (h);
	if (n <= 0) return;
	buf[n] = 0;
	for (char *s = buf; *s && g_welcome->nrec < Welcome::MAXREC;)
	{
		char *e = s; while (*e && *e != '\n' && *e != '\r') e++;
		char c = *e; *e = 0;
		if (s[0] && arc::path_exists (s))
		{
			int k = g_welcome->nrec++;
			arc::scopy (g_welcome->rec[k], s, 300);
			void *f = kapi_open (s); g_welcome->recSize[k] = f ? kapi_fsize64 (f) : 0; if (f) kapi_close (f);
		}
		s = c ? e + 1 : e;
		while (*s == '\n' || *s == '\r') s++;
	}
	g_welcome->invalidate (true);
}
static void recent_add (const char *path)
{
	char out[2048] = ""; arc::scat (out, path, sizeof out); arc::scat (out, "\n", sizeof out);
	for (int i = 0; i < g_welcome->nrec; i++)
		if (arc::ci_cmp (g_welcome->rec[i], path)) { arc::scat (out, g_welcome->rec[i], sizeof out); arc::scat (out, "\n", sizeof out); }
	char p[300]; recent_path (p, sizeof p);
	kapi_save_file (p, out, (unsigned) strlen (out));
	recent_load ();
}

// ---- the view ------------------------------------------------------------------------------------------------
static void update_status ()
{
	if (!g_arc) { g_status->set ("No archive open", "Archiver"); return; }
	char l[160], r[160], a[32], b[32];
	int nodes[4096]; int n = sel_nodes (nodes, 4096);
	if (n)
	{
		u64 sz = 0; for (int i = 0; i < n; i++) sz += g_model.n[nodes[i]].size;
		arc::u64_str ((u64) n, a, sizeof a); arc::u64_str ((u64) (g_list->nitems - (g_list->nitems && g_list->items[0] < 0)), b, sizeof b);
		arc::scopy (l, a, sizeof l); arc::scat (l, " of ", sizeof l); arc::scat (l, b, sizeof l); arc::scat (l, " selected  (", sizeof l);
		arc::human_size (sz, a, sizeof a); arc::scat (l, a, sizeof l); arc::scat (l, ")", sizeof l);
	}
	else
	{
		int items = g_list->nitems - (g_list->nitems && g_list->items[0] < 0);
		arc::u64_str ((u64) items, a, sizeof a);
		arc::scopy (l, a, sizeof l); arc::scat (l, items == 1 ? " item" : " items", sizeof l);
		if (g_list->search) arc::scat (l, " found", sizeof l);
		else if (g_folder) { char p[300]; g_model.pathOf (g_folder, p, sizeof p); arc::scat (l, " in ", sizeof l); arc::scat (l, p, sizeof l); arc::scat (l, "/", sizeof l); }
	}
	arc::scopy (r, arc::base_of (g_arc->path), sizeof r);
	arc::u64_str ((u64) g_model.n[0].files, a, sizeof a); arc::scat (r, "  -  ", sizeof r); arc::scat (r, a, sizeof r);
	arc::scat (r, g_model.n[0].files == 1 ? " file  -  " : " files  -  ", sizeof r);
	arc::human_size (g_model.n[0].size, a, sizeof a); arc::human_size (g_arc->totalPacked (), b, sizeof b);
	arc::scat (r, a, sizeof r); arc::scat (r, " -> ", sizeof r); arc::scat (r, b, sizeof r);
	g_status->set (l, r);
}
static void update_card ()
{
	g_card->clear ();
	if (!g_arc) return;
	char t[64], u[32];
	arc::scopy (t, g_arc->format (), sizeof t);
	if (!g_arc->writable ()) arc::scat (t, ", read only", sizeof t);
	g_card->row ("Format", t);
	int folders = 0; for (int i = 1; i < g_model.count; i++) if (g_model.n[i].dir) folders++;
	arc::u64_str ((u64) g_model.n[0].files, t, sizeof t); g_card->row ("Files", t);
	arc::u64_str ((u64) folders, t, sizeof t); g_card->row ("Folders", t);
	arc::human_size (g_model.n[0].size, t, sizeof t); g_card->row ("Original size", t);
	arc::human_size (g_arc->totalPacked (), t, sizeof t); g_card->row ("Packed size", t);
	u64 s = g_model.n[0].size, p = g_arc->totalPacked ();
	arc::u64_str (s && p < s ? (s - p) * 100 / s : 0, u, sizeof u); arc::scat (u, " %", sizeof u); g_card->row ("Saved", u);
	int enc = 0; for (int i = 0; i < g_arc->n; i++) if (g_arc->e[i].encrypted) enc++;
	g_card->row ("Encrypted", enc ? (enc == g_arc->n ? "yes" : "some files") : "no");
	g_card->row ("Comment", g_arc->comment && g_arc->comment[0] ? g_arc->comment : "-");
}
static void refresh_list (int keepNode = -1)
{
	int *v; int m;
	bool search = g_path->search->text[0] != 0;
	if (search) m = g_model.search (g_path->search->text, &v);
	else m = g_model.list (g_folder, &v);
	g_list->set (v, m, search);
	free (v);
	g_list->emptyText = search ? "Nothing found" : "This folder is empty";
	if (keepNode >= 0) g_list->selectNode (keepNode);
	update_status ();
	g_tools->refresh ();
}
static void show_archive (bool on)
{
	g_welcome->hidden = on;
	g_path->hidden = !on; g_body->hidden = !on;
	g_root->invalidate (true);
	g_tools->refresh ();
}
void ui::app_navigate (int node)
{
	if (node == -2) { refresh_list (); return; }			// (sorted again)
	if (node < 0 || node >= g_model.count || !g_model.n[node].dir) return;
	if (node != g_folder && g_nhist < 64) g_hist[g_nhist++] = g_folder;
	int from = g_folder;
	g_folder = node;
	if (g_path->search->text[0]) { g_path->search->setText (""); g_lastSearch[0] = 0; }
	refresh_list (g_model.isAncestor (node, from) && from != node ? -1 : -1);
	// coming up from a child: that child selected
	if (from > 0 && g_model.n[from].parent == node) g_list->selectNode (from);
	g_model.n[node].open = true;
	g_tree->reveal (node);
	g_path->invalidate (true);
	update_status ();
}
void ui::app_back ()
{
	if (!g_nhist) { if (g_folder > 0) app_navigate (g_model.n[g_folder].parent); return; }
	int k = g_hist[--g_nhist];
	int keep = g_nhist;
	app_navigate (k);
	g_nhist = keep;				// (going back is not a step forward)
}
void ui::app_search (const char *q) { (void) q; refresh_list (); g_path->invalidate (true); }
void ui::app_selection_changed () { update_status (); g_tools->refresh (); }

// Reload the archive from the disk (after a change), back to the folder `keepPath`
static void reload (const char *keepPath)
{
	char why[200], p[300];
	arc::scopy (p, g_arc->path, sizeof p);
	arc::Archive *a = arc::archive_open (p, why, sizeof why);
	if (!a) { uk_messagebox ("Archiver", why, MB_OK); return; }
	// the folders open in the tree, open again after
	char *openP[256]; int nopen = 0;
	for (int i = 1; i < g_model.count && nopen < 256; i++)
		if (g_model.n[i].dir && g_model.n[i].open) { char q[600]; g_model.pathOf (i, q, sizeof q); openP[nopen++] = arc::sdup (q); }
	delete g_arc; g_arc = a;
	g_model.build (g_arc);
	for (int i = 0; i < nopen; i++) { int o = g_model.find (openP[i]); if (o > 0) g_model.n[o].open = true; free (openP[i]); }
	int k = g_model.find (keepPath);
	while (k < 0 && keepPath[0])
	{	// (the folder went away: its nearest parent)
		char q[300]; arc::scopy (q, keepPath, sizeof q);
		char *s = strrchr (q, '/'); if (s) *s = 0; else q[0] = 0;
		k = g_model.find (q); if (k >= 0) break;
		keepPath = ""; k = 0;
	}
	g_folder = k < 0 ? 0 : k; g_nhist = 0;
	g_model.n[g_folder].open = true;
	g_path->badge[0] = 0; arc::scopy (g_path->badge, g_arc->format (), sizeof g_path->badge);
	if (!g_arc->writable ()) arc::scat (g_path->badge, "  -  read only", sizeof g_path->badge);
	refresh_list ();
	g_tree->reveal (g_folder);
	update_card ();
	g_path->invalidate (true);
}

static bool open_archive (const char *path)
{
	char why[200];
	arc::Archive *a = arc::archive_open (path, why, sizeof why);
	if (!a)
	{
		char msg[400]; arc::scopy (msg, arc::base_of (path), sizeof msg); arc::scat (msg, "\n", sizeof msg); arc::scat (msg, why, sizeof msg);
		uk_messagebox ("Cannot open the archive", msg, MB_OK);
		return false;
	}
	delete g_arc; g_arc = a; g_password[0] = 0;
	dirname_of (path, g_lastDir, sizeof g_lastDir);
	g_model.sortCol = SORT_NAME; g_model.sortDesc = false;
	g_model.build (g_arc);
	g_folder = 0; g_nhist = 0;
	g_path->search->setText (""); g_lastSearch[0] = 0;
	arc::scopy (g_path->badge, g_arc->format (), sizeof g_path->badge);
	if (!g_arc->writable ()) arc::scat (g_path->badge, "  -  read only", sizeof g_path->badge);
	show_archive (true);
	refresh_list ();
	g_tree->top = 0; g_tree->reveal (0);
	update_card ();
	recent_add (path);
	g_list->setFocus ();
	return true;
}
static void close_archive ()
{
	delete g_arc; g_arc = 0;
	g_model.clear (); g_folder = 0;
	g_list->set (0, 0, false);
	show_archive (false);
	recent_load ();
	update_status ();
}

// ---- jobs: the work in a thread -------------------------------------------------------------------------------
enum { JOB_EXTRACT, JOB_ADD, JOB_DELETE, JOB_RENAME, JOB_MKDIR, JOB_TEST, JOB_OPEN, JOB_CREATE };
struct Job : arc::Progress
{
	int kind; bool ok; char error[200];
	volatile bool cancel; volatile u64 done, total; char cur[200];
	unsigned lastPost; int tid;
	arc::Archive *a;			// the job's own instance
	arc::Extract x; char *sel;
	char **paths; int npaths; AddChoice add;
	char from[600], to[600], keepPath[300], openPath[300], destShow[300];
	bool openAfter;
	int  askEvent, answer;
	int  files;
	Job () : kind (0), ok (false), cancel (false), done (0), total (0), lastPost (0), tid (-1), a (0), sel (0), paths (0), npaths (0),
		 openAfter (false), askEvent (-1), answer (0), files (0)
	{ error[0] = cur[0] = from[0] = to[0] = keepPath[0] = openPath[0] = destShow[0] = 0; memset (&add, 0, sizeof add); }
	~Job () { delete a; free (sel); free_names (paths, npaths); free (paths); if (askEvent >= 0) kapi_sync_close (askEvent); }
	bool step (u64 b) override;
	void file_ (const char *n) { arc::scopy (cur, n, sizeof cur); }
	void file (const char *n) override { file_ (n); }
};
static Job *g_job;			// the job running (one at a time)
static ProgressBox *g_pbox;
static void job_tick (void *ctx, long);
bool Job::step (u64 b)
{
	done += b;
	unsigned now = kapi_get_ticks ();
	if (now - lastPost >= 10) { lastPost = now; kapi_post (job_tick, this, 0); }
	return !cancel;
}
static void job_show (Job *j)
{
	int pct = j->total ? (int) (j->done * 100 / j->total) : 0;
	if (pct > 100) pct = 100;
	if (g_pbox)
	{
		g_pbox->pct = pct;
		arc::scopy (g_pbox->file, j->cur, sizeof g_pbox->file);
		char a[24], b[24]; arc::human_size (j->done, a, sizeof a); arc::human_size (j->total, b, sizeof b);
		arc::scopy (g_pbox->line, a, sizeof g_pbox->line); arc::scat (g_pbox->line, " of ", sizeof g_pbox->line); arc::scat (g_pbox->line, b, sizeof g_pbox->line);
		g_pbox->invalidate (true);
	}
	g_status->busy = pct; g_status->invalidate (true);
}
static void job_tick (void *ctx, long) { if (ctx == g_job) job_show ((Job *) ctx); }
// "it exists": asked on the main thread, the worker waiting
static void ask_main (void *ctx, long)
{
	Job *j = (Job *) ctx;
	ExistsBox b (j->cur);
	int r = b.run ();
	j->answer = r ? r : arc::ANS_CANCEL;
	kapi_event_set (j->askEvent);
}
static int ask_exists (void *ctx, const char *path)
{
	Job *j = (Job *) ctx;
	arc::scopy (j->cur, path, sizeof j->cur);
	if (j->askEvent < 0) j->askEvent = kapi_event_create (0, 0);
	kapi_post (ask_main, j, 0);
	kapi_event_wait (j->askEvent, KAPI_WAIT_FOREVER);
	return j->answer;
}
static void job_done (void *ctx, long);
static int job_thread (void *arg)
{
	Job *j = (Job *) arg;
	arc::Archive *a = j->a;
	switch (j->kind)
	{
	case JOB_EXTRACT: case JOB_OPEN:
		j->x.a = a; j->x.sel = j->sel; j->x.ask = ask_exists; j->x.askCtx = j;
		j->ok = arc::run_extract (j->x, j);
		j->files = j->x.files;
		if (!j->ok) arc::scopy (j->error, j->x.error, sizeof j->error);
		break;
	case JOB_TEST:
	{
		j->ok = true;
		for (int i = 0; i < a->n && j->ok; i++)
		{
			if (a->e[i].dir) continue;
			j->file_ (a->e[i].name);
			arc::NullSink ns;
			if (!a->extract (i, ns, j)) { j->ok = false; arc::scopy (j->error, a->error, sizeof j->error); }
			else j->files++;
		}
		break;
	}
	case JOB_ADD: case JOB_CREATE:
		j->ok = arc::op_add (*a, (const char *const *) j->paths, j->npaths, j->add.into, j->add.keepFolders, j->add.replace,
				     j->add.level, j, &j->files);
		break;
	case JOB_DELETE: j->ok = arc::op_delete (*a, j->sel, j); break;
	case JOB_RENAME: j->ok = arc::op_rename (*a, j->from, j->to, j); break;
	case JOB_MKDIR: j->ok = arc::op_new_folder (*a, j->to, j); break;
	}
	if (!j->ok && !j->error[0]) arc::scopy (j->error, a->error, sizeof j->error);
	kapi_post (job_done, j, 0);
	return 0;
}

// watched files: opened from the archive into RAM:, put back when they change
struct Watch { char ram[300], entry[600], archive[300]; u64 size; unsigned crc; };
static Watch g_watch[16]; static int g_nwatch;
static bool file_sum (const char *p, u64 *size, unsigned *crc)
{
	arc::Reader r;
	if (!r.open (p)) return false;
	*size = r.size; *crc = 0;
	if (r.size > 64u * 1024 * 1024) return true;		// (too big to read each time: the size alone)
	unsigned char *b = (unsigned char *) malloc (65536); unsigned c = (unsigned) crc32 (0, 0, 0);
	for (u64 left = r.size; b && left;) { unsigned k = left > 65536 ? 65536 : (unsigned) left; if (!r.read (b, k)) break; c = (unsigned) crc32 (c, b, k); left -= k; }
	free (b); *crc = c;
	return true;
}

static void start_job (Job *j, const char *title);
static void job_done (void *ctx, long again)
{
	Job *j = (Job *) ctx;
	// (the progress box first closed: its loop ends, then the rest -- a message box -- here again)
	if (g_pbox && !again) { g_pbox->close (1); kapi_post (job_done, ctx, 1); return; }
	kapi_thread_join (j->tid, 1000, 0);
	g_job = 0; g_status->busy = -1; g_status->invalidate (true);
	char msg[400];
	if (j->kind == JOB_EXTRACT || j->kind == JOB_OPEN || j->kind == JOB_TEST)
	{
		if (!j->ok) { arc::scopy (msg, j->error, sizeof msg); uk_messagebox (j->kind == JOB_TEST ? "Test" : "Extract", msg, MB_OK); }
		else if (j->kind == JOB_TEST)
		{
			char n[24]; arc::u64_str ((u64) j->files, n, sizeof n);
			arc::scopy (msg, "No errors: ", sizeof msg); arc::scat (msg, n, sizeof msg); arc::scat (msg, " files checked.", sizeof msg);
			uk_messagebox ("Test", msg, MB_OK);
		}
		else if (j->kind == JOB_OPEN)
		{
			fa_open (j->openPath);
			if (g_nwatch < 16)
			{
				Watch &w = g_watch[g_nwatch++];
				arc::scopy (w.ram, j->openPath, sizeof w.ram); arc::scopy (w.entry, j->from, sizeof w.entry);
				arc::scopy (w.archive, g_arc->path, sizeof w.archive);
				file_sum (w.ram, &w.size, &w.crc);
			}
		}
		else
		{
			char n[24]; arc::u64_str ((u64) j->files, n, sizeof n);
			arc::scopy (msg, n, sizeof msg); arc::scat (msg, j->files == 1 ? " file extracted to " : " files extracted to ", sizeof msg);
			arc::scat (msg, j->x.dest, sizeof msg);
			g_status->set (msg, g_status->right);
			if (j->openAfter) fa_open (j->destShow[0] ? j->destShow : j->x.dest);
		}
		delete j;
		return;
	}
	// a change: the archive again from the disk
	if (!j->ok) { uk_messagebox ("Archiver", j->error, MB_OK); }
	if (j->kind == JOB_CREATE) { if (j->ok) open_archive (j->a->path); }
	else if (g_arc) reload (j->keepPath);
	if (j->ok && (j->kind == JOB_ADD || j->kind == JOB_CREATE))
	{
		char n[24]; arc::u64_str ((u64) j->files, n, sizeof n);
		arc::scopy (msg, n, sizeof msg); arc::scat (msg, j->files == 1 ? " item added" : " items added", sizeof msg);
		g_status->set (msg, g_status->right);
	}
	delete j;
}
static void start_job (Job *j, const char *title)
{
	if (g_job) { uk_messagebox ("Archiver", "Another job is running: wait for it, or cancel it.", MB_OK); delete j; return; }
	g_job = j;
	j->tid = kapi_thread_create (job_thread, j, 0, "job");
	if (j->tid < 0) { g_job = 0; uk_messagebox ("Archiver", "Cannot start the job (no thread).", MB_OK); delete j; return; }
	ProgressBox box (title);
	g_pbox = &box;
	job_show (j);
	box.run ();
	g_pbox = 0;
	if (box.cancelled && g_job == j) j->cancel = true;
}
// a new instance of the archive open, for a job (with the password)
static arc::Archive *job_archive ()
{
	char why[200];
	arc::Archive *a = arc::archive_open (g_arc->path, why, sizeof why);
	if (!a) { uk_messagebox ("Archiver", why, MB_OK); return 0; }
	if (!strcmp (a->format (), "ZIP")) arc::scopy (((arc::ZipArchive *) a)->password, g_password, 128);
	return a;
}
static void keep_here (Job *j) { g_model.pathOf (g_folder, j->keepPath, sizeof j->keepPath); }

// ---- the commands ----------------------------------------------------------------------------------------
static bool need_password (const char *sel)
{
	if (g_password[0]) return true;
	bool any = false;
	for (int i = 0; i < g_arc->n; i++) if ((!sel || sel[i]) && g_arc->e[i].encrypted) any = true;
	if (!any) return true;
	char pw[128];
	if (!ask_text ("Password", "Some files are encrypted. The password:", "", pw, sizeof pw, true)) return false;
	arc::scopy (g_password, pw, sizeof g_password);
	return true;
}
static void do_extract (bool all)
{
	if (!g_arc) return;
	char *names[4096]; int n = all ? 0 : sel_paths (names, 4096);
	u64 selSize = 0; int nodes[4096]; int nn = sel_nodes (nodes, 4096);
	for (int i = 0; i < nn; i++) selSize += g_model.n[nodes[i]].size;
	char cur[300]; g_model.pathOf (g_folder, cur, sizeof cur);
	char sub[300]; arc::scopy (sub, cur[0] ? cur : "the archive's top", sizeof sub); arc::scat (sub, "/", sizeof sub);
	char base[128]; arc::scopy (base, arc::base_of (g_arc->path), sizeof base);
	char *dot = strrchr (base, '.'); if (dot && dot != base) *dot = 0;
	// the hints: the first file selected (or the first file there is) under each layout
	char hFull[300] = "", hCur[300] = "", hFlat[300] = "";
	const char *ex = 0;
	for (int i = 0; i < nn && !ex; i++) if (!g_model.n[nodes[i]].dir) { static char p[600]; g_model.pathOf (nodes[i], p, sizeof p); ex = p; }
	for (int i = 0; i < g_arc->n && !ex; i++) if (!g_arc->e[i].dir && arc::under (g_arc->e[i].name, cur)) ex = g_arc->e[i].name;
	if (ex)
	{
		arc::scopy (hFull, ex, sizeof hFull);
		int cl = (int) strlen (cur);
		arc::scopy (hCur, cl && arc::under (ex, cur) ? ex + cl + 1 : ex, sizeof hCur);
		arc::scopy (hFlat, arc::base_of (ex), sizeof hFlat);
	}
	bool crypt = false; for (int i = 0; i < g_arc->n; i++) if (g_arc->e[i].encrypted) crypt = true;
	ExtractBox box (arc::base_of (g_arc->path), sub, nn, selSize, g_model.n[0].files, g_model.n[0].size, g_lastDir, base,
			hFull, hCur, hFlat, crypt);
	if (all) { box.rAll->select (); }
	if (crypt && g_password[0]) box.pw->setText (g_password);
	int r = box.run ();
	if (!r) { free_names (names, n); return; }
	ExtractChoice c; box.get (c);
	if (c.password[0]) arc::scopy (g_password, c.password, sizeof g_password);
	Job *j = new Job; j->kind = JOB_EXTRACT;
	j->a = job_archive (); if (!j->a) { delete j; free_names (names, n); return; }
	if (!c.all && n) j->sel = arc::select_names (*g_arc, names, n);
	free_names (names, n);
	if (!need_password (j->sel)) { delete j; return; }
	if (!strcmp (j->a->format (), "ZIP")) arc::scopy (((arc::ZipArchive *) j->a)->password, g_password, 128);
	arc::scopy (j->x.dest, c.dest, sizeof j->x.dest);
	if (c.newFolder && c.folderName[0]) { char s[128]; arc::safe_rel (c.folderName, s, sizeof s); arc::join (j->x.dest, sizeof j->x.dest, c.dest, s); }
	arc::scopy (g_lastDir, c.dest, sizeof g_lastDir);
	arc::scopy (j->x.current, cur, sizeof j->x.current);
	j->x.layout = c.layout; j->x.overwrite = c.overwrite;
	j->x.a = j->a; j->x.sel = j->sel;
	j->total = arc::extract_total (j->x);
	j->openAfter = c.openAfter;
	start_job (j, "Extracting");
}
static void extract_here ()
{
	if (!g_arc) return;
	char *names[4096]; int n = sel_paths (names, 4096);
	Job *j = new Job; j->kind = JOB_EXTRACT;
	j->a = job_archive (); if (!j->a) { delete j; free_names (names, n); return; }
	if (n) j->sel = arc::select_names (*g_arc, names, n);
	free_names (names, n);
	if (!need_password (j->sel)) { delete j; return; }
	if (!strcmp (j->a->format (), "ZIP")) arc::scopy (((arc::ZipArchive *) j->a)->password, g_password, 128);
	dirname_of (g_arc->path, j->x.dest, sizeof j->x.dest);
	g_model.pathOf (g_folder, j->x.current, sizeof j->x.current);
	j->x.layout = arc::LAY_FROM_CURRENT; j->x.overwrite = arc::OW_ASK;
	j->x.a = j->a; j->x.sel = j->sel;
	j->total = arc::extract_total (j->x);
	start_job (j, "Extracting");
}
static void add_paths (char **paths, int np, const char *into, bool keepFolders, int level, bool replace, bool askConflicts)
{
	if (!writable ()) { uk_messagebox ("Archiver", "This archive is read only.", MB_OK); return; }
	if (askConflicts)
	{
		int c = arc::add_conflicts (*g_arc, (const char *const *) paths, np, into, keepFolders);
		if (c)
		{
			char msg[200], n[16]; arc::u64_str ((u64) c, n, sizeof n);
			arc::scopy (msg, n, sizeof msg); arc::scat (msg, c == 1 ? " file is in the archive already.\nReplace it?" : " files are in the archive already.\nReplace them?", sizeof msg);
			int r = uk_messagebox ("Add", msg, MB_YESNOCANCEL);
			if (!r) return;
			replace = r == 1;
		}
	}
	Job *j = new Job; j->kind = JOB_ADD;
	j->a = job_archive (); if (!j->a) { delete j; return; }
	j->paths = (char **) malloc (sizeof (char *) * (np ? np : 1)); j->npaths = np;
	for (int i = 0; i < np; i++) j->paths[i] = arc::sdup (paths[i]);
	arc::scopy (j->add.into, into, sizeof j->add.into); j->add.keepFolders = keepFolders; j->add.level = level; j->add.replace = replace;
	arc::AddSet s; arc::plan_add (*g_arc, s, (const char *const *) paths, np, into, keepFolders, replace);
	for (int k = 0; k < s.plan.nkeep; k++) j->total += g_arc->e[s.plan.keep[k]].packed;
	j->total += s.bytes;
	keep_here (j);
	start_job (j, "Adding");
}
static void do_add ()
{
	if (!writable ()) { uk_messagebox ("Archiver", "This archive is read only.", MB_OK); return; }
	char cur[300]; g_model.pathOf (g_folder, cur, sizeof cur);
	AddBox box (arc::base_of (g_arc->path), cur, g_lastDir);
	if (!box.run () || !box.nitem) return;
	AddChoice c; box.get (c);
	char *p[AddBox::MAXI]; for (int i = 0; i < box.nitem; i++) p[i] = box.item[i];
	add_paths (p, box.nitem, c.into, c.keepFolders, c.level, c.replace, false);
}
static void do_delete ()
{
	if (!writable ()) return;
	char *names[4096]; int n = sel_paths (names, 4096);
	if (!n) return;
	char msg[300];
	if (n == 1) { arc::scopy (msg, "Delete ", sizeof msg); arc::scat (msg, arc::base_of (names[0]), sizeof msg); arc::scat (msg, " from the archive?", sizeof msg); }
	else { char c[16]; arc::u64_str ((u64) n, c, sizeof c); arc::scopy (msg, "Delete these ", sizeof msg); arc::scat (msg, c, sizeof msg); arc::scat (msg, " items from the archive?", sizeof msg); }
	if (uk_messagebox ("Delete", msg, MB_YESNO) != 1) { free_names (names, n); return; }
	Job *j = new Job; j->kind = JOB_DELETE;
	j->a = job_archive (); if (!j->a) { delete j; free_names (names, n); return; }
	j->sel = arc::select_names (*g_arc, names, n);
	free_names (names, n);
	for (int i = 0; i < g_arc->n; i++) if (!j->sel[i]) j->total += g_arc->e[i].packed;
	keep_here (j);
	start_job (j, "Deleting");
}
static void do_rename ()
{
	if (!writable ()) return;
	int nodes[2]; if (sel_nodes (nodes, 2) != 1) { uk_messagebox ("Rename", "Select one file or folder to rename.", MB_OK); return; }
	char old[600]; g_model.pathOf (nodes[0], old, sizeof old);
	char name[256];
	if (!ask_text ("Rename", "The new name:", g_model.n[nodes[0]].name, name, sizeof name)) return;
	if (strchr (name, '/') || strchr (name, '\\')) { uk_messagebox ("Rename", "A name cannot hold / or \\.", MB_OK); return; }
	if (!strcmp (name, g_model.n[nodes[0]].name)) return;
	Job *j = new Job; j->kind = JOB_RENAME;
	j->a = job_archive (); if (!j->a) { delete j; return; }
	arc::scopy (j->from, old, sizeof j->from);
	g_model.pathOf (g_model.n[nodes[0]].parent, j->to, sizeof j->to);
	if (j->to[0]) arc::scat (j->to, "/", sizeof j->to);
	arc::scat (j->to, name, sizeof j->to);
	if (g_model.find (j->to) >= 0) { uk_messagebox ("Rename", "That name is taken in this folder.", MB_OK); delete j; return; }
	j->total = g_arc->totalPacked ();
	keep_here (j);
	start_job (j, "Renaming");
}
static void do_new_folder ()
{
	if (!writable ()) return;
	char name[256];
	if (!ask_text ("New Folder", "The folder's name:", "New Folder", name, sizeof name)) return;
	if (strchr (name, '/') || strchr (name, '\\')) { uk_messagebox ("New Folder", "A name cannot hold / or \\.", MB_OK); return; }
	Job *j = new Job; j->kind = JOB_MKDIR;
	j->a = job_archive (); if (!j->a) { delete j; return; }
	g_model.pathOf (g_folder, j->to, sizeof j->to);
	if (j->to[0]) arc::scat (j->to, "/", sizeof j->to);
	arc::scat (j->to, name, sizeof j->to);
	if (g_model.find (j->to) >= 0) { uk_messagebox ("New Folder", "That name is taken in this folder.", MB_OK); delete j; return; }
	j->total = g_arc->totalPacked ();
	keep_here (j);
	start_job (j, "New folder");
}
static void do_test ()
{
	if (!g_arc || !need_password (0)) return;
	Job *j = new Job; j->kind = JOB_TEST;
	j->a = job_archive (); if (!j->a) { delete j; return; }
	j->total = g_model.n[0].size;
	start_job (j, "Testing");
}
static void do_properties ()
{
	if (!g_arc) return;
	PropsBox b (9);
	char t[160], u[32];
	b.row ("Archive", arc::base_of (g_arc->path));
	dirname_of (g_arc->path, t, sizeof t); b.row ("Folder", t);
	arc::scopy (t, g_arc->format (), sizeof t); if (!g_arc->writable ()) arc::scat (t, " (read only)", sizeof t); b.row ("Format", t);
	{ arc::Reader r; r.open (g_arc->path); arc::human_size (r.size, t, sizeof t); b.row ("File size", t); }
	arc::u64_str ((u64) g_model.n[0].files, t, sizeof t); b.row ("Files", t);
	arc::human_size (g_model.n[0].size, t, sizeof t); b.row ("Original size", t);
	arc::human_size (g_arc->totalPacked (), t, sizeof t);
	u64 s = g_model.n[0].size, p = g_arc->totalPacked ();
	arc::u64_str (s && p < s ? (s - p) * 100 / s : 0, u, sizeof u); arc::scat (t, "  (", sizeof t); arc::scat (t, u, sizeof t); arc::scat (t, " % saved)", sizeof t);
	b.row ("Packed size", t);
	int enc = 0; for (int i = 0; i < g_arc->n; i++) if (g_arc->e[i].encrypted) enc++;
	arc::u64_str ((u64) enc, t, sizeof t); arc::scat (t, enc == 1 ? " file" : " files", sizeof t); b.row ("Encrypted", enc ? t : "none");
	b.row ("Comment", g_arc->comment && g_arc->comment[0] ? g_arc->comment : "-");
	b.run ();
}
static void do_open_dialog ()
{
	char p[300];
	if (uk_file_open (p, sizeof p, g_lastDir)) open_archive (p);
}
static void do_new (char **paths = 0, int np = 0)
{
	char p[300];
	char def[128] = "New archive.zip";
	if (np) { arc::scopy (def, arc::base_of (paths[0]), sizeof def); char *d = strrchr (def, '.'); if (d && d != def && !arc::path_is_dir (paths[0])) *d = 0; arc::scat (def, ".zip", sizeof def); }
	char dir[300]; if (np) dirname_of (paths[0], dir, sizeof dir); else arc::scopy (dir, g_lastDir, sizeof dir);
	if (!uk_file_save (p, sizeof p, dir, def)) return;
	char x[12]; arc::ext_of (p, x, sizeof x);
	if (strcmp (x, "zip")) arc::scat (p, ".zip", sizeof p);
	if (arc::path_exists (p) && uk_messagebox ("New Archive", "That file exists. Replace it?", MB_YESNO) != 1) return;
	if (arc::path_exists (p)) kapi_remove (p);
	arc::Archive *a = arc::archive_new (p);
	if (!np)
	{
		arc::Plan plan;
		if (!a->rewrite (plan, p, 0)) { uk_messagebox ("New Archive", a->error, MB_OK); delete a; return; }
		delete a;
		open_archive (p);
		return;
	}
	Job *j = new Job; j->kind = JOB_CREATE; j->a = a;
	j->paths = (char **) malloc (sizeof (char *) * np); j->npaths = np;
	for (int i = 0; i < np; i++) j->paths[i] = arc::sdup (paths[i]);
	j->add.into[0] = 0; j->add.keepFolders = true; j->add.level = 6; j->add.replace = true;
	arc::AddSet s; arc::plan_add (*a, s, (const char *const *) paths, np, "", true, true);
	j->total = s.bytes;
	start_job (j, "Making the archive");
}

// Double click / Enter: a folder is entered, a file is opened (extracted to RAM:, then its app)
static int g_openSeq;
void ui::app_activate (int item)
{
	if (item < 0 || item >= g_list->nitems) return;
	int node = g_list->items[item];
	if (node < 0) { app_navigate (g_model.n[g_folder].parent); return; }
	if (g_model.n[node].dir) { app_navigate (node); return; }
	char name[600]; g_model.pathOf (node, name, sizeof name);
	int e = g_model.n[node].entry;
	if (e < 0) return;
	char x[12]; arc::ext_of (name, x, sizeof x);
	if (!strcmp (x, "zip"))
	{	// an archive in the archive: opened in its turn (from RAM:)
	}
	if (g_arc->e[e].encrypted && !g_password[0])
	{
		char pw[128];
		if (!ask_text ("Password", "This file is encrypted. The password:", "", pw, sizeof pw, true)) return;
		arc::scopy (g_password, pw, sizeof g_password);
	}
	Job *j = new Job; j->kind = JOB_OPEN;
	j->a = job_archive (); if (!j->a) { delete j; return; }
	j->sel = (char *) calloc ((size_t) g_arc->n, 1); j->sel[e] = 1;
	char seq[16]; arc::u64_str ((u64) ++g_openSeq, seq, sizeof seq);
	arc::scopy (j->x.dest, "RAM:/archiver/open/", sizeof j->x.dest); arc::scat (j->x.dest, seq, sizeof j->x.dest);
	j->x.layout = arc::LAY_FLAT; j->x.overwrite = arc::OW_REPLACE;
	j->x.a = j->a; j->x.sel = j->sel;
	arc::join (j->openPath, sizeof j->openPath, j->x.dest, arc::base_of (name));
	arc::scopy (j->from, name, sizeof j->from);
	j->total = g_arc->e[e].size;
	start_job (j, "Opening");
}

// put back what was changed in RAM:
static void watch_tick ()
{
	static unsigned last;
	unsigned now = kapi_get_ticks ();
	if (now - last < 150 || !g_nwatch || g_job) return;
	last = now;
	for (int i = 0; i < g_nwatch; i++)
	{
		Watch &w = g_watch[i];
		u64 sz; unsigned crc;
		if (!file_sum (w.ram, &sz, &crc)) { g_watch[i--] = g_watch[--g_nwatch]; continue; }
		if (sz == w.size && crc == w.crc) continue;
		w.size = sz; w.crc = crc;
		if (!g_arc || arc::ci_cmp (g_arc->path, w.archive) || !writable ()) continue;
		char msg[400]; arc::scopy (msg, arc::base_of (w.entry), sizeof msg); arc::scat (msg, " was changed.\nPut it back in the archive?", sizeof msg);
		if (uk_messagebox ("Archiver", msg, MB_YESNO) != 1) continue;
		char into[600]; arc::scopy (into, w.entry, sizeof into);
		char *s = strrchr (into, '/'); if (s) *s = 0; else into[0] = 0;
		char *p[1] = { w.ram };
		add_paths (p, 1, into, true, 6, true, false);
		return;
	}
}

// ---- drag & drop ---------------------------------------------------------------------------------------------
static int g_dragSeq;
void ui::app_drag_out ()
{
	if (!g_arc || g_job) return;
	char *names[256]; int n = sel_paths (names, 256);
	if (!n) return;
	arc::Extract x; x.a = g_arc;
	char *sel = arc::select_names (*g_arc, names, n); x.sel = sel;
	u64 tot = arc::extract_total (x);
	if (tot > 96u * 1024 * 1024) { free (sel); free_names (names, n); g_status->set ("Too large to drag: use Extract...", g_status->right); return; }
	char seq[16]; arc::u64_str ((u64) ++g_dragSeq, seq, sizeof seq);
	arc::scopy (x.dest, "RAM:/archiver/drag/", sizeof x.dest); arc::scat (x.dest, seq, sizeof x.dest);
	if (g_list->search) x.layout = arc::LAY_FLAT;
	else { x.layout = arc::LAY_FROM_CURRENT; g_model.pathOf (g_folder, x.current, sizeof x.current); }
	x.overwrite = arc::OW_REPLACE;
	if (!strcmp (g_arc->format (), "ZIP")) arc::scopy (((arc::ZipArchive *) g_arc)->password, g_password, 128);
	bool ok = arc::run_extract (x, 0);
	free (sel);
	if (!ok) { free_names (names, n); g_status->set (x.error, g_status->right); return; }
	char buf[4096]; buf[0] = 0;
	for (int i = 0; i < n; i++)
	{
		char p[300]; arc::join (p, sizeof p, x.dest, arc::base_of (names[i]));
		if (buf[0]) arc::scat (buf, "\n", sizeof buf);
		arc::scat (buf, p, sizeof buf);
	}
	char label[64];
	if (n == 1) arc::scopy (label, arc::base_of (names[0]), sizeof label);
	else { arc::u64_str ((u64) n, label, sizeof label); arc::scat (label, " items", sizeof label); }
	free_names (names, n);
	kapi_drag_begin (DND_FILES, buf, (unsigned) strlen (buf) + 1, label);
}

// The folder a drop at (x, y) goes into (-1: not on the archive)
static int drop_target (int x, int y, int *row)
{
	int lx, ly;
	*row = -1;
	if (!g_arc) return -1;
	if (inside (g_list, x, y, &lx, &ly))
	{
		int r = g_list->rowAt (ly);
		if (r >= 0 && g_list->items[r] >= 0 && g_model.n[g_list->items[r]].dir) { *row = r; return g_list->items[r]; }
		if (r >= 0 && g_list->items[r] < 0) { *row = r; return g_model.n[g_folder].parent; }
		return g_list->search ? 0 : g_folder;
	}
	if (inside (g_tree, x, y, &lx, &ly))
	{
		int r = g_tree->rowAt (ly);
		return r >= 0 ? g_tree->rows[r] : g_folder;
	}
	return -1;
}
static void drop_clear ()
{
	if (g_list->dropRow != -1) { g_list->dropRow = -1; g_list->dropText[0] = 0; g_list->invalidate (true); }
	if (g_tree->dropNode != -1) { g_tree->dropNode = -1; g_tree->invalidate (true); }
	if (g_welcome->dropHot) { g_welcome->dropHot = false; g_welcome->invalidate (true); }
}

static bool looks_like_archive (const char *p)
{
	char x[12]; arc::ext_of (p, x, sizeof x);
	static const char *e[] = { "zip", "jar", "7z", "rar", "tar", "tgz", "gz", "xz", "bz2", "zst", 0 };
	for (int i = 0; e[i]; i++) if (!strcmp (x, e[i])) return true;
	return false;
}

class ArcRoot : public Root
{
public:
	ArcRoot () : Root (W, H, "Archiver") {}
	void onDragOver (int x, int y, bool leave, unsigned) override
	{
		if (leave) { drop_clear (); return; }
		if (!g_arc) { if (!g_welcome->dropHot) { g_welcome->dropHot = true; g_welcome->invalidate (true); } return; }
		int row; int k = drop_target (x, y, &row);
		int lx, ly;
		bool onList = inside (g_list, x, y, &lx, &ly);
		int listRow = onList ? (row >= 0 ? row : -2) : -1;
		int treeNode = !onList && k >= 0 ? k : -1;
		if (!writable ()) { listRow = -1; treeNode = -1; }
		char t[160] = "";
		if (k >= 0 && writable ())
		{
			arc::scopy (t, arc::base_of (g_arc->path), sizeof t);
			char p[300]; g_model.pathOf (k, p, sizeof p);
			for (char *s = p; *s; s++) if (*s == '/') { }
			if (p[0]) { arc::scat (t, "  >  ", sizeof t); arc::scat (t, p, sizeof t); arc::scat (t, "/", sizeof t); }
		}
		if (listRow != g_list->dropRow || strcmp (t, g_list->dropText))
		{ g_list->dropRow = listRow; arc::scopy (g_list->dropText, onList ? t : "", sizeof g_list->dropText); g_list->invalidate (true); }
		if (treeNode != g_tree->dropNode) { g_tree->dropNode = treeNode; g_tree->invalidate (true); }
	}
	void onDrop (int x, int y, int type, const char *data, int, unsigned) override
	{
		drop_clear ();
		if (type != DND_FILES || !data[0]) return;
		// the paths, one a line
		char *paths[64]; int np = 0;
		static char buf[4097]; arc::scopy (buf, data, sizeof buf);
		for (char *s = buf; *s && np < 64;)
		{
			char *e = s; while (*e && *e != '\n') e++;
			char c = *e; *e = 0;
			if (s[0]) paths[np++] = s;
			s = c ? e + 1 : e;
		}
		if (!np) return;
		if (!g_arc)
		{
			if (np == 1 && looks_like_archive (paths[0])) open_archive (paths[0]);
			else do_new (paths, np);
			return;
		}
		int row; int k = drop_target (x, y, &row);
		if (k < 0) return;
		if (!writable ()) { uk_messagebox ("Archiver", "This archive is read only: nothing can be added to it.", MB_OK); return; }
		char into[300]; g_model.pathOf (k, into, sizeof into);
		add_paths (paths, np, into, true, 6, true, true);
	}
	void onTick () override
	{
		if (g_path && strcmp (g_path->search->text, g_lastSearch))
		{
			arc::scopy (g_lastSearch, g_path->search->text, sizeof g_lastSearch);
			if (g_arc) app_search (g_lastSearch);
		}
		watch_tick ();
	}
	bool onKey (long k) override
	{
		if (!g_arc) return false;
		if (k == KEY_BACKSPACE) { if (g_folder > 0) app_navigate (g_model.n[g_folder].parent); return true; }
		if (k == KEY_DEL) { do_delete (); return true; }
		if (k == KEY_F1 + 1) { do_rename (); return true; }
		if (k == 27 && g_path->search->text[0]) { g_path->search->setText (""); return true; }
		return false;
	}
};

// ---- the toolbar, the menus, the context menu ---------------------------------------------------------------------
bool ui::app_tool_enabled (int id)
{
	switch (id)
	{
	case IC_OPEN: case IC_NEW: return true;
	case IC_ADD: return writable ();
	case IC_DELETE: { int n[1]; return writable () && sel_nodes (n, 1) > 0; }
	default: return g_arc != 0;
	}
}
void ui::app_tool (int id)
{
	switch (id)
	{
	case IC_OPEN: do_open_dialog (); break;
	case IC_NEW: do_new (); break;
	case IC_ADD: do_add (); break;
	case IC_EXTRACT: do_extract (false); break;
	case IC_EXTRACT_ALL: do_extract (true); break;
	case IC_DELETE: do_delete (); break;
	case IC_TEST: do_test (); break;
	case IC_INFO: do_properties (); break;
	}
}
void ui::app_welcome_button (int which) { if (which == 0) do_open_dialog (); else do_new (); }
void ui::app_open_recent (int i) { if (i >= 0 && i < g_welcome->nrec) { char p[300]; arc::scopy (p, g_welcome->rec[i], sizeof p); open_archive (p); } }
void ui::app_context (int item, int lx, int ly)
{
	int ax, ay; abs_pos (g_list, &ax, &ay);
	PopupMenu m (ax + lx, ay + ly);
	bool w = writable ();
	if (item >= 0)
	{
		int node = g_list->items[item];
		bool dir = node >= 0 && g_model.n[node].dir;
		m.add (dir ? "Open Folder" : "Open", 1, node >= 0, "Enter");
		m.separator ();
		m.add ("Extract...", 2, true, "^E");
		m.add ("Extract Here", 3, true);
		m.separator ();
		m.add ("Rename...", 4, w && node >= 0, "F2");
		m.add ("Delete from Archive", 5, w && node >= 0, "Del");
		m.separator ();
		m.add ("New Folder...", 6, w);
		m.add ("Properties", 7, true);
	}
	else
	{
		m.add ("Add Files...", 8, w);
		m.add ("New Folder...", 6, w);
		m.separator ();
		m.add ("Select All", 9, g_list->nitems > 0, "^A");
		m.add ("Extract All...", 10, true);
		m.add ("Properties", 7, true);
	}
	switch (m.run ())
	{
	case 1: app_activate (item); break;
	case 2: do_extract (false); break;
	case 3: extract_here (); break;
	case 4: do_rename (); break;
	case 5: do_delete (); break;
	case 6: do_new_folder (); break;
	case 7: do_properties (); break;
	case 8: do_add (); break;
	case 9: g_list->selectAll (); break;
	case 10: do_extract (true); break;
	}
}
static void m_new () { do_new (); }
static void m_open () { do_open_dialog (); }
static void m_close () { if (g_arc) close_archive (); }
static void m_add () { if (g_arc) do_add (); }
static void m_extract () { if (g_arc) do_extract (false); }
static void m_extract_all () { if (g_arc) do_extract (true); }
static void m_extract_here () { extract_here (); }
static void m_test () { do_test (); }
static void m_props () { do_properties (); }
static void m_select_all () { if (g_arc) g_list->selectAll (); }
static void m_invert () { if (g_arc) g_list->invert (); }
static void m_delete () { do_delete (); }
static void m_rename () { do_rename (); }
static void m_new_folder () { do_new_folder (); }
static void m_find () { if (g_arc) g_path->search->setFocus (); }
static void m_up () { if (g_arc && g_folder > 0) app_navigate (g_model.n[g_folder].parent); }
static void m_back () { if (g_arc) app_back (); }
static void sort_by (int c) { if (!g_arc) return; if (g_model.sortCol == c) g_model.sortDesc = !g_model.sortDesc; else { g_model.sortCol = c; g_model.sortDesc = c != SORT_NAME; } refresh_list (); }
static void m_sort_name () { sort_by (SORT_NAME); }
static void m_sort_size () { sort_by (SORT_SIZE); }
static void m_sort_ratio () { sort_by (SORT_RATIO); }
static void m_sort_time () { sort_by (SORT_TIME); }

// ---- the window ----------------------------------------------------------------------------------------------
int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);			// (before the widgets)
	FtTextFace *sm = new FtTextFace;
	if (sm->open ("DejaVu Sans", 11)) g_small = sm;
	kapi_mkdir ("RAM:/archiver"); kapi_mkdir ("RAM:/archiver/open"); kapi_mkdir ("RAM:/archiver/drag");

	ArcRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);

	g_tools = new ToolStrip (0, 0, W, 64);
	g_tools->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_tools->add (IC_OPEN, "Open"); g_tools->add (IC_NEW, "New"); g_tools->sep ();
	g_tools->add (IC_ADD, "Add"); g_tools->add (IC_EXTRACT, "Extract"); g_tools->add (IC_EXTRACT_ALL, "Extract All");
	g_tools->add (IC_DELETE, "Delete"); g_tools->sep ();
	g_tools->add (IC_TEST, "Test"); g_tools->add (IC_INFO, "Properties");
	root.addChild (g_tools);

	g_path = new PathBar (8, 70, W - 16, 32);
	g_path->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	root.addChild (g_path);

	int bodyH = H - 108 - 30;
	HSplitter *split = new HSplitter (8, 108, W - 16, bodyH, 240);
	split->anchor = ANCHOR_FILL; split->minA = 160; split->minB = 300;
	Panel *left = new Panel (0, 0, 240, bodyH);
	int cardH = 28 + 8 * 19 + 8;
	g_tree = new FolderTree (0, 0, 240, bodyH - cardH - 8);
	g_tree->anchor = ANCHOR_FILL;
	g_card = new InfoCard (6, bodyH - cardH, 228, cardH);
	g_card->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	left->addChild (g_tree); left->addChild (g_card);
	g_list = new EntryList (0, 0, W - 16 - 240 - 6, bodyH);
	split->setPanes (left, g_list);
	root.addChild (split);
	g_body = split;

	g_welcome = new Welcome (8, 70, W - 16, H - 70 - 30);
	g_welcome->anchor = ANCHOR_FILL;
	root.addChild (g_welcome);

	g_status = new StatusBar (0, H - 26, W, 26);
	g_status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.addChild (g_status);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New Archive...", "^N", UK_CTRL ('N'), m_new);
	menu.item ("Open...", "^O", UK_CTRL ('O'), m_open);
	menu.item ("Close", "^W", UK_CTRL ('W'), m_close);
	menu.separator ();
	menu.item ("Add Files...", "^D", UK_CTRL ('D'), m_add);
	menu.separator ();
	menu.item ("Extract...", "^E", UK_CTRL ('E'), m_extract);
	menu.item ("Extract All...", "", 0, m_extract_all);
	menu.item ("Extract Here", "", 0, m_extract_here);
	menu.separator ();
	menu.item ("Test", "^T", UK_CTRL ('T'), m_test);
	menu.item ("Properties", "^P", UK_CTRL ('P'), m_props);
	menu.menu ("Edit");
	menu.item ("Select All", "^A", UK_CTRL ('A'), m_select_all);
	menu.item ("Invert Selection", "", 0, m_invert);
	menu.separator ();
	menu.item ("Rename...", "F2", 0, m_rename);
	menu.item ("Delete from Archive", "Del", 0, m_delete);
	menu.item ("New Folder...", "^K", UK_CTRL ('K'), m_new_folder);
	menu.separator ();
	menu.item ("Find", "^F", UK_CTRL ('F'), m_find);
	menu.menu ("View");
	menu.item ("Up", "Backspace", 0, m_up);
	menu.item ("Back", "", 0, m_back);
	menu.separator ();
	menu.item ("Sort by Name", "", 0, m_sort_name);
	menu.item ("Sort by Size", "", 0, m_sort_size);
	menu.item ("Sort by Ratio", "", 0, m_sort_ratio);
	menu.item ("Sort by Date", "", 0, m_sort_time);
	menu.publish ();

	recent_load ();
	show_archive (false);
	update_status ();
	root.fitWorkArea ();

	char args[300]; int na = kapi_get_args (args, sizeof args); args[na > 0 && na < 300 ? na : 0] = 0;
	char *a = args; while (*a == ' ') a++;
	int al = (int) strlen (a); while (al && a[al - 1] == ' ') a[--al] = 0;
	if (a[0]) { if (a[0] == '"') { a++; char *q = strchr (a, '"'); if (q) *q = 0; } open_archive (a); }

	root.run ();
	if (g_job) { g_job->cancel = true; kapi_thread_join (g_job->tid, 3000, 0); }
	return 0;
}
