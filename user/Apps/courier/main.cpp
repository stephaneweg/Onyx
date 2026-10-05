//
// courier -- Onyx's HTTP client, made after Postman: requests (GET, POST, PUT, PATCH, DELETE, HEAD,
// OPTIONS) with their query params, headers, authorization (bearer, basic, API key; inherited from the
// folder / the collection), bodies (raw JSON / XML / HTML / text / JavaScript, x-www-form-urlencoded,
// multipart form-data with files, a binary file); {{variables}} everywhere from the environment chosen,
// the collection, the globals ({{$guid}}, {{$timestamp}}... made on the spot); collections with folders;
// environments; the history; the cookie jar; the response pretty-printed (JSON, XML), its headers, its
// cookies, an image or an HTML page previewed, the exchange in a console; tests and captures (a value of
// the response set as a variable: a login's token); the request as code (cURL, HTTP, Python, JavaScript);
// Postman's collections and environments imported and exported, a cURL command imported (or pasted in
// the URL). http:// and https:// (mbedTLS), on a thread of its own: the window keeps answering, Cancel
// works.
//
// The pieces: util.h (strings, arrays), model.h (the data, Postman's formats), vars.h (the variables),
// net.h (the HTTP engine), tools.h (tests, snippets, cURL, formatting), widgets.h (the controls),
// views.h (the request's editor and its response), rail.h (the sidebar), dialogs.h, app.h (the tabs).
// Files: SD:/courier/ (model.h says which). "courier <file.json>" imports it.
//
#include "uikit/uikit.h"
#include "ft/uikitface.h"
#include "docguard.h"
#include "Apps/courier/dialogs.h"

namespace cr {

static Sidebar *g_side;
static unsigned g_saveT;			// the ticks of the last change (saved a second later)
static bool g_stateDirty;
static int g_spinT;

// ================================================================================================================
// the status bar
// ================================================================================================================
static void app_status (const char *msg, bool error)
{
	ui.statusMsg = msg; ui.statusErr = error; ui.statusT = kapi_get_ticks ();
	if (ui.status) ui.status->invalidate (true);
}
static void paint_status (Pane &p)
{
	unsigned bg = p.bg, ink = uk_ink_for (bg);
	p.canvas.fillRect (0, 0, p.width, 1, c_bgline ());
	if (!ui.statusMsg.empty ())
	{
		unsigned c = ui.statusErr ? on_field (C_BAD) : uk_mix (bg, ink, 200);
		if (ui.statusErr) uk_glyph (p.canvas, WKG_RING, 16, p.height / 2, 10, c);
		text_fit (p.canvas, ui.statusErr ? 28 : 12, 0, p.height, p.width / 2, ui.statusMsg.c (), c);
	}
	// the right: the environment, the cookies
	char b[160];
	Environment *e = g_store.env ();
	snprintf (b, sizeof b, "%s   |   %d cookie%s", e ? e->name.c () : "No environment", g_jar.list.size (), g_jar.list.size () == 1 ? "" : "s");
	int w = uk_tw (b);
	text_v (p.canvas, p.width - 12 - w, 0, p.height, b, uk_mix (bg, ink, 150));
}

// ================================================================================================================
// the tabs
// ================================================================================================================
static void refresh_doctabs ()
{
	DocTabs *d = ui.docTabs;
	d->tabs.clear ();
	for (int i = 0; i < g_tabs.size (); i++)
	{
		Tab *t = g_tabs[i];
		DocTabInfo info; info.kind = t->kind; info.dirty = t->dirty && t->kind == TAB_REQUEST;
		if (t->kind == TAB_REQUEST) { info.title = t->req.name.empty () ? "Untitled Request" : t->req.name.c (); info.method = t->req.method; }
		else if (t->kind == TAB_ENV) { Environment *e = tab_env (t); info.title = e ? e->name.c () : "?"; }
		else { Collection *c = tab_coll (t); info.title = c ? c->name.c () : "?"; }
		d->tabs.push (info);
	}
	d->sel = g_cur;
	d->invalidate (true);
}
static void refresh_env_pick ()
{
	int n = 0;
	ui.envNames[n++] = "No Environment";
	for (int i = 0; i < g_store.envs.size () && n < 16; i++) ui.envNames[n++] = g_store.envs[i]->name.c ();
	ui.envPick->setOptions (ui.envNames, n, g_store.activeEnv + 1);
}
static void lay_main (Pane &p);
static void show_views ()
{
	Tab *t = cur_tab ();
	show (ui.welcome, !t);
	show (ui.reqView, t && t->kind == TAB_REQUEST);
	show (ui.envView, t && t->kind == TAB_ENV);
	show (ui.collView, t && t->kind == TAB_COLL);
	ui.main->invalidate (true);
}
static void bind_env (Tab *t)
{
	Environment *e = tab_env (t);
	if (!e) return;
	bool glob = e == &g_store.globals;
	ui.envName->setText (e->name.c ());
	ui.envName->readonly = glob;
	ui.envVars->setRows (&e->vars);
	int idx = glob ? -2 : g_store.findEnv (e->id.c ());
	ui.envActive->hidden = glob;
	bool active = idx == g_store.activeEnv;
	ui.envActive->setText (active ? "Active" : "Set Active");
	ui.envActive->kind = active ? BTN_PRIMARY : BTN_SECONDARY;
	ui.envActive->setGlyph (active ? WKG_CHECK : -1);
	ui.envView->layout (); ui.envView->invalidate (true);
}
static void coll_section (int s)
{
	show (ui.collDesc, s == 0); show (ui.collAuth, s == 1); show (ui.collVars, s == 2);
	ui.collView->invalidate (true);
}
static void bind_coll (Tab *t)
{
	Collection *c = tab_coll (t);
	if (!c) return;
	ui.collName->setText (c->name.c ());
	ui.collDesc->setText (c->desc.c ());
	ui.collAuth->bind (&c->auth);
	ui.collVars->setRows (&c->vars);
	coll_section (ui.collTabs->sel);
	ui.collView->layout (); ui.collView->invalidate (true);
}
static void select_tab (int i)
{
	Tab *old = cur_tab ();
	if (old && old->kind == TAB_REQUEST) old->split = ui.vsplit->split;
	g_cur = i;
	update_scope ();
	Tab *t = cur_tab ();
	if (t)
	{
		if (t->kind == TAB_REQUEST) bind_request (t);
		else if (t->kind == TAB_ENV) bind_env (t);
		else bind_coll (t);
	}
	show_views ();
	refresh_doctabs ();
	g_side->invalidate (true);
	ui.status->invalidate (true);
	g_stateDirty = true;
}
static int add_tab (Tab *t)
{
	// after the current one
	int at = g_cur >= 0 ? g_cur + 1 : g_tabs.size ();
	g_tabs.insert (at, t);
	select_tab (at);
	return at;
}
static void save_item_from_tab (Tab *t);
static void close_tab (int i)
{
	if (i < 0 || i >= g_tabs.size ()) return;
	Tab *t = g_tabs[i];
	if (t->kind == TAB_REQUEST && t->dirty)
	{
		Str q; q.addf ("\"%s\" has changes not saved. Close it anyway?", t->req.name.empty () ? "Untitled Request" : t->req.name.c ());
		if (!ask ("Close Tab", q.c (), "Close", "Cancel", true)) return;
	}
	if (t->job) { t->job->cancel = 1; t->job->user = 0; t->job = 0; }	// (the job deletes itself when it ends)
	g_tabs.remove (i);
	delete t;
	int n = g_tabs.size ();
	g_cur = -1;
	select_tab (n ? imin (i, n - 1) : -1);
}
static int find_tab (int kind, const char *id)
{
	for (int i = 0; i < g_tabs.size (); i++)
	{
		Tab *t = g_tabs[i];
		if (t->kind != kind) continue;
		if (kind == TAB_REQUEST && t->itemId.eq (id)) return i;
		if (kind == TAB_ENV && t->envId.eq (id)) return i;
		if (kind == TAB_COLL && t->collId.eq (id)) return i;
	}
	return -1;
}
static void app_tab_changed ()
{
	refresh_doctabs ();
	g_stateDirty = true;
	g_saveT = kapi_get_ticks ();
}

// ================================================================================================================
// the model changed
// ================================================================================================================
static void app_refresh ()
{
	update_scope ();
	g_side->rebuild ();
	refresh_env_pick ();
	refresh_doctabs ();
	Tab *t = cur_tab ();
	if (t && t->kind == TAB_REQUEST) { inherit_text (); ui.reqPane->invalidate (true); ui.url->invalidate (true); }
	if (t && t->kind == TAB_ENV) bind_env (t);
	ui.status->invalidate (true);
	g_stateDirty = true;
}
static void app_collection_changed (Collection *c) { if (c) c->dirty = true; g_saveT = kapi_get_ticks (); }
static void app_env_changed (Environment *e) { if (e) e->dirty = true; g_saveT = kapi_get_ticks (); }

// ================================================================================================================
// opening
// ================================================================================================================
static void app_open_item (Item *it)
{
	if (!it || it->folder) return;
	int i = find_tab (TAB_REQUEST, it->id.c ());
	if (i >= 0) { select_tab (i); return; }
	Tab *t = new Tab;
	t->req = it->req; t->req.name = it->name; t->itemId = it->id;
	add_tab (t);
}
static void app_open_coll (Collection *c)
{
	if (!c) return;
	int i = find_tab (TAB_COLL, c->id.c ());
	if (i >= 0) { select_tab (i); return; }
	Tab *t = new Tab; t->kind = TAB_COLL; t->collId = c->id;
	add_tab (t);
}
static void app_open_env (int idx)
{
	const char *id = idx < 0 ? "globals" : g_store.envs[idx]->id.c ();
	int i = find_tab (TAB_ENV, id);
	if (i >= 0) { select_tab (i); return; }
	Tab *t = new Tab; t->kind = TAB_ENV; t->envId = id;
	add_tab (t);
}
static void app_open_history (int idx)
{
	if (idx < 0 || idx >= g_store.history.size ()) return;
	Tab *t = new Tab;
	t->req = g_store.history[idx].req;
	if (t->req.name.empty ()) t->req.name = t->req.url;
	t->dirty = false;
	add_tab (t);
}
static void app_new_request (Item *folder)
{
	if (folder)
	{
		Collection *c = g_store.collectionOf (folder);
		Str nm;
		if (!ask_name (nm, "New Request", "The request's name", "New Request", "Add")) return;
		Item *it = new Item; uuid (it->id); it->name = nm; it->req.name = nm;
		folder->add (it); folder->open = true;
		if (c) { c->root.open = true; save_collection (*c); }
		app_refresh ();
		app_open_item (it);
		return;
	}
	Tab *t = new Tab;
	t->req.name = "Untitled Request";
	add_tab (t);
	ui.url->setFocus ();
}
static void app_new_folder (Item *parent)
{
	if (!parent) return;
	Str nm;
	if (!ask_name (nm, "New Folder", "The folder's name", "New Folder", "Add")) return;
	Item *it = new Item; uuid (it->id); it->name = nm; it->folder = true; it->open = true;
	parent->add (it); parent->open = true;
	Collection *c = g_store.collectionOf (parent);
	if (c) { c->root.open = true; save_collection (*c); }
	app_refresh ();
}
static void app_new_collection ()
{
	Str nm;
	if (!ask_name (nm, "New Collection", "The collection's name", "New Collection", "Create")) return;
	Collection *c = new Collection;
	uuid (c->id); c->name = nm; c->root.open = true;
	g_store.colls.push (c);
	save_collection (*c);
	g_side->setMode (SB_COLL);
	app_refresh ();
	app_status ("The collection was made: save requests in it (Ctrl+S).");
}
static void app_new_env ()
{
	Str nm;
	if (!ask_name (nm, "New Environment", "The environment's name (e.g. Local, Staging, Production)", "New Environment", "Create")) return;
	Environment *e = new Environment;
	uuid (e->id); e->name = nm;
	e->vars.push (KV ("baseUrl", "http://localhost:8080"));
	g_store.envs.push (e);
	save_environment (*e);
	g_side->setMode (SB_ENV);
	app_refresh ();
	app_open_env (g_store.envs.size () - 1);
}

// ================================================================================================================
// the collections' items
// ================================================================================================================
static void app_rename_item (Item *it)
{
	Str nm;
	if (!ask_name (nm, it->folder ? "Rename Folder" : "Rename Request", "The new name", it->name.c (), "Rename")) return;
	it->name = nm; it->req.name = nm;
	int i = find_tab (TAB_REQUEST, it->id.c ());
	if (i >= 0) { g_tabs[i]->req.name = nm; if (i == g_cur) ui.name->setText (nm.c ()); }
	Collection *c = g_store.collectionOf (it);
	if (c) save_collection (*c);
	app_refresh ();
}
static void app_duplicate_item (Item *it)
{
	Item *p = it->parent;
	if (!p) return;
	Item *d = it->clone ();
	d->name.add (" Copy"); d->req.name = d->name;
	p->add (d, p->indexOf (it) + 1);
	Collection *c = g_store.collectionOf (p);
	if (c) save_collection (*c);
	app_refresh ();
}
// the tabs of an item (and of what it holds) no longer in a collection: kept, not saved
static void detach_tabs (Item *it)
{
	for (int i = 0; i < g_tabs.size (); i++) if (g_tabs[i]->kind == TAB_REQUEST && g_tabs[i]->itemId.eq (it->id.c ())) { g_tabs[i]->itemId.clear (); g_tabs[i]->dirty = true; }
	for (int k = 0; k < it->kids.size (); k++) detach_tabs (it->kids[k]);
}
static void app_delete_item (Item *it)
{
	Str q; q.addf ("Delete \"%s\"%s? This cannot be undone.", it->name.c (), it->folder ? " and everything in it" : "");
	if (!ask (it->folder ? "Delete Folder" : "Delete Request", q.c (), "Delete", "Cancel", true)) return;
	Collection *c = g_store.collectionOf (it);
	detach_tabs (it);
	if (it->parent) it->parent->detach (it);
	delete it;
	if (c) save_collection (*c);
	app_refresh ();
}
static void app_move_item (Item *it, int dir)
{
	Item *p = it ? it->parent : 0;
	if (!p) return;
	int i = p->indexOf (it), j = i + dir;
	if (j < 0 || j >= p->kids.size ()) return;
	p->kids.move (i, j);
	Collection *c = g_store.collectionOf (p);
	if (c) save_collection (*c);
	app_refresh ();
}
static void app_rename_coll (Collection *c)
{
	Str nm;
	if (!ask_name (nm, "Rename Collection", "The new name", c->name.c (), "Rename")) return;
	c->name = nm; save_collection (*c);
	if (cur_tab () && cur_tab ()->kind == TAB_COLL && tab_coll (cur_tab ()) == c) ui.collName->setText (nm.c ());
	app_refresh ();
}
static void app_duplicate_coll (Collection *c)
{
	json::Writer w (false); write_collection (w, *c);
	Collection *d = new Collection; char err[120];
	if (!parse_collection (*d, w.data (), (int) w.size (), err, sizeof err)) { delete d; return; }
	uuid (d->id); d->name.add (" Copy");
	g_store.colls.push (d); save_collection (*d);
	app_refresh ();
}
static void app_delete_coll (Collection *c)
{
	Str q; q.addf ("Delete the collection \"%s\" and its %s? This cannot be undone (export it first to keep a copy).", c->name.c (), c->root.kids.size () ? "requests" : "settings");
	if (!ask ("Delete Collection", q.c (), "Delete", "Cancel", true)) return;
	detach_tabs (&c->root);
	for (int i = g_tabs.size () - 1; i >= 0; i--) if (g_tabs[i]->kind == TAB_COLL && g_tabs[i]->collId.eq (c->id.c ())) { g_tabs[i]->dirty = false; close_tab (i); }
	if (!c->file.empty ()) kapi_remove (c->file.c ());
	for (int i = 0; i < g_store.colls.size (); i++) if (g_store.colls[i] == c) { g_store.colls.remove (i); break; }
	delete c;
	update_scope ();
	app_refresh ();
}
static void app_export_coll (Collection *c)
{
	Str nm; safe_name (nm, c->name.c ()); nm.add (".postman_collection.json");
	char p[256];
	if (!uk_file_save (p, sizeof p, "SD:/", nm.c ())) return;
	json::Writer w (true); write_collection (w, *c);
	if (save_json (p, w)) { Str m; m.addf ("Exported to %s (Postman collection v2.1).", p); app_status (m.c ()); }
	else app_status ("The collection could not be written.", true);
}

// ================================================================================================================
// the environments
// ================================================================================================================
static void app_set_env (int idx)
{
	g_store.activeEnv = idx;
	g_stateDirty = true;
	app_refresh ();
	Str m; m.addf ("Environment: %s", idx >= 0 ? g_store.envs[idx]->name.c () : "none");
	app_status (m.c ());
}
static void app_rename_env (int idx)
{
	Environment *e = g_store.envs[idx];
	Str nm;
	if (!ask_name (nm, "Rename Environment", "The new name", e->name.c (), "Rename")) return;
	e->name = nm; save_environment (*e);
	app_refresh ();
}
static void app_duplicate_env (int idx)
{
	Environment *e = new Environment (*g_store.envs[idx]);
	uuid (e->id); e->file.clear (); e->name.add (" Copy");
	g_store.envs.push (e); save_environment (*e);
	app_refresh ();
}
static void app_delete_env (int idx)
{
	Environment *e = g_store.envs[idx];
	Str q; q.addf ("Delete the environment \"%s\" and its variables?", e->name.c ());
	if (!ask ("Delete Environment", q.c (), "Delete", "Cancel", true)) return;
	for (int i = g_tabs.size () - 1; i >= 0; i--) if (g_tabs[i]->kind == TAB_ENV && g_tabs[i]->envId.eq (e->id.c ())) close_tab (i);
	if (!e->file.empty ()) kapi_remove (e->file.c ());
	if (g_store.activeEnv == idx) g_store.activeEnv = -1;
	else if (g_store.activeEnv > idx) g_store.activeEnv--;
	g_store.envs.remove (idx);
	delete e;
	app_refresh ();
}
static void app_export_env (int idx)
{
	Environment *e = idx < 0 ? &g_store.globals : g_store.envs[idx];
	Str nm; safe_name (nm, idx < 0 ? "globals" : e->name.c ()); nm.add (idx < 0 ? ".postman_globals.json" : ".postman_environment.json");
	char p[256];
	if (!uk_file_save (p, sizeof p, "SD:/", nm.c ())) return;
	json::Writer w (true); write_environment (w, *e, idx < 0 ? "globals" : "environment");
	if (save_json (p, w)) { Str m; m.addf ("Exported to %s.", p); app_status (m.c ()); }
	else app_status ("The environment could not be written.", true);
}

// ================================================================================================================
// the history
// ================================================================================================================
static void app_clear_history ()
{
	if (!g_store.history.size ()) return;
	if (!ask ("Clear History", "Delete every request of the history?", "Clear", "Cancel", true)) return;
	g_store.history.clear ();
	save_history (g_store);
	app_refresh ();
}
static void app_delete_history (int idx)
{
	g_store.history.remove (idx);
	g_store.historyDirty = true; g_saveT = kapi_get_ticks ();
	app_refresh ();
}

// ================================================================================================================
// sending
// ================================================================================================================
static void run_tests (Tab *t)
{
	t->results.clear ();
	if (!t->resp || t->resp->status < 0) return;
	Eval ev (*t->resp);
	for (int i = 0; i < t->req.tests.size (); i++)
	{
		KV &k = t->req.tests[i];
		if (!k.on || k.key.empty ()) continue;
		TestResult r; r.capture = false;
		Str exp; s_trim (exp, k.value.c ());
		r.name.addf ("%s %s", k.key.c (), exp.empty () ? "exists" : exp.c ());
		r.pass = run_test (ev, k.key.c (), k.value.c (), r.detail);
		t->results.push (r);
	}
	Environment *target = g_store.env () ? g_store.env () : &g_store.globals;
	for (int i = 0; i < t->req.captures.size (); i++)
	{
		KV &k = t->req.captures[i];
		if (!k.on || k.key.empty ()) continue;
		TestResult r; r.capture = true;
		Str v, var; s_trim (var, k.key.c ());
		r.name.addf ("%s  <-  %s", var.c (), k.value.c ());
		if (ev.get (k.value.c (), v))
		{
			set_var (target->vars, var.c (), v.c ());
			target->dirty = true;
			Str shown; shown.set (v.c (), imin (v.len (), 60)); if (v.len () > 60) shown.add ("...");
			r.detail.addf ("= %s  (%s)", shown.c (), target == &g_store.globals ? "globals" : target->name.c ());
			r.pass = true;
		}
		else { r.detail = "not found in the response"; r.pass = false; }
		t->results.push (r);
	}
	if (target->dirty) g_saveT = kapi_get_ticks ();
}
static void send_button ()
{
	Tab *t = cur_tab ();
	bool busy = t && t->job;
	ui.send->setText (busy ? "Cancel" : "Send");
	ui.send->kind = busy ? BTN_SECONDARY : BTN_PRIMARY;
	ui.send->invalidate (true);
}
static void job_done (Job *j)
{
	Tab *t = (Tab *) j->user;
	bool alive = false;
	for (int i = 0; i < g_tabs.size (); i++) if (g_tabs[i] == t) alive = true;
	if (!t || !alive) { delete j; return; }
	t->job = 0;
	delete t->last;
	t->last = j; t->resp = &j->resp;
	if (!j->resp.cancelled) { g_jar = j->jar; g_jar.dirty = true; }
	run_tests (t);
	// the history (the most recent first)
	HistoryEntry h; h.when = now_unix (); h.status = j->resp.status >= 0 ? j->resp.status : -1; h.ms = j->resp.msTotal; h.req = t->req;
	h.req.events.clear ();
	g_store.history.insert (0, h);
	while (g_store.history.size () > HISTORY_MAX) g_store.history.remove (g_store.history.size () - 1);
	g_store.historyDirty = true; g_saveT = kapi_get_ticks ();
	if (t == cur_tab ()) { t->respSection = t->respSection == RS_CONSOLE && j->resp.status >= 0 ? RS_BODY : t->respSection; resp_fill (); send_button (); }
	if (g_side->mode == SB_HIST) g_side->rebuild ();
	Str m;
	if (j->resp.status >= 0)
	{
		int fails = 0; for (int i = 0; i < t->results.size (); i++) if (!t->results[i].pass && !t->results[i].capture) fails++;
		m.addf ("%s %s: %d %s in %d ms", t->req.method.c (), t->req.name.c (), j->resp.status, j->resp.reason.c (), j->resp.msTotal);
		if (fails) m.addf (" -- %d test%s failed", fails, fails > 1 ? "s" : "");
		app_status (m.c (), fails > 0);
	}
	else app_status (j->resp.error.c (), !j->resp.cancelled);
	ui.status->invalidate (true);
	g_side->invalidate (true);
}
static void app_send ()
{
	Tab *t = cur_tab ();
	if (!t || t->kind != TAB_REQUEST) return;
	if (t->job) { app_cancel (); return; }
	Collection *c = 0; Item *it = tab_item (t, &c);
	update_scope ();
	Job *j = new Job;
	j->jar = g_jar;
	prepare (j->prep, t->req, g_scope, effective_auth (t->req, it, c), g_jar);
	j->onDone = job_done; j->user = t;
	t->job = j;
	send_button ();
	resp_shown ();
	app_status ("Sending...");
	job_start (j);
}
static void app_cancel ()
{
	Tab *t = cur_tab ();
	if (t && t->job) { t->job->cancel = 1; app_status ("Cancelling..."); }
}

// ================================================================================================================
// saving
// ================================================================================================================
static void app_save ()
{
	Tab *t = cur_tab ();
	if (!t) return;
	if (t->kind != TAB_REQUEST) { app_status ("Environments and collections are saved as you edit them."); return; }
	Collection *c = 0; Item *it = tab_item (t, &c);
	if (!it) { app_save_as (); return; }
	it->req = t->req; it->name = t->req.name;
	if (c && save_collection (*c)) { t->dirty = false; Str m; m.addf ("Saved in %s.", c->name.c ()); app_status (m.c ()); }
	else app_status ("The collection could not be saved.", true);
	app_refresh ();
}
static void app_save_as ()
{
	Tab *t = cur_tab ();
	if (!t || t->kind != TAB_REQUEST) return;
	Item *cur = tab_item (t);
	SaveDialog d (t->req.name.empty () ? "Untitled Request" : t->req.name.c (), cur ? cur->parent : 0);
	d.name->setFocus ();
	if (d.run () != 1) return;
	if (d.sel < 0 || d.sel >= d.dests.size ()) { app_status ("Choose a collection or a folder to save the request to.", true); return; }
	Str nm; s_trim (nm, d.name->text.c ());
	if (nm.empty ()) nm = "Untitled Request";
	SaveDialog::Dest &dst = d.dests[d.sel];
	Item *it = new Item; uuid (it->id); it->name = nm;
	t->req.name = nm;
	it->req = t->req;
	dst.folder->add (it); dst.folder->open = true; dst.coll->root.open = true;
	t->itemId = it->id;
	if (save_collection (*dst.coll)) { t->dirty = false; Str m; m.addf ("Saved in %s.", dst.coll->name.c ()); app_status (m.c ()); }
	ui.name->setText (nm.c ());
	g_side->setMode (SB_COLL);
	app_refresh ();
}
// every change kept on the card (a second after the last)
static void save_state ();
static void flush (bool now)
{
	if (!now && kapi_get_ticks () - g_saveT < 100) return;
	for (int i = 0; i < g_store.colls.size (); i++) if (g_store.colls[i]->dirty) save_collection (*g_store.colls[i]);
	for (int i = 0; i < g_store.envs.size (); i++) if (g_store.envs[i]->dirty) save_environment (*g_store.envs[i]);
	if (g_store.globals.dirty) save_globals (g_store.globals);
	if (g_store.historyDirty) save_history (g_store);
	if (g_jar.dirty)
	{
		json::Writer w (false); w.beginArr ();
		long long now_ = now_unix ();
		for (int i = 0; i < g_jar.list.size (); i++)
		{
			Cookie &c = g_jar.list[i];
			if (!c.expires || c.expires < now_) continue;		// (the session's cookies end with it)
			w.beginObj (true);
			w.key ("domain"); w.str (c.domain.c ()); w.key ("path"); w.str (c.path.c ()); w.key ("name"); w.str (c.name.c ());
			w.key ("value"); w.str (c.value.c ()); w.key ("expires"); w.num ((long long) c.expires);
			w.key ("secure"); w.boolean (c.secure); w.key ("hostOnly"); w.boolean (c.hostOnly); w.key ("httpOnly"); w.boolean (c.httpOnly);
			w.endObj ();
		}
		w.endArr ();
		store_dirs (); save_json (F_COOKIES, w);
		g_jar.dirty = false;
	}
	if (g_stateDirty) save_state ();
}
static void load_cookies ()
{
	int n = 0; char *b = read_file (F_COOKIES, &n);
	if (!b) return;
	json::Doc d;
	if (d.parse (b, (unsigned long) n, json::TOLERANT))
		for (const json::Value *e = d.root ().first (); e; e = e->next)
		{
			Cookie c;
			jstr (c.domain, (*e)["domain"]); jstr (c.path, (*e)["path"]); jstr (c.name, (*e)["name"]); jstr (c.value, (*e)["value"]);
			c.expires = (*e)["expires"].asLong (0); c.secure = (*e)["secure"].asBool (false);
			c.hostOnly = (*e)["hostOnly"].asBool (true); c.httpOnly = (*e)["httpOnly"].asBool (false);
			g_jar.list.push (c);
		}
	free (b);
}

// ---- the session: the tabs open, the environment chosen, the sidebar -------------------------------------------------------
static void save_state ()
{
	Tab *ct = cur_tab ();
	if (ct && ct->kind == TAB_REQUEST) ct->split = ui.vsplit->split;
	json::Writer w (true);
	w.beginObj ();
	Environment *e = g_store.env ();
	w.key ("environment"); w.str (e ? e->id.c () : "");
	w.key ("sidebar"); w.num (g_side->mode);
	w.key ("sidebarWidth"); w.num (ui.hsplit->split);
	w.key ("current"); w.num (g_cur);
	w.key ("tabs"); w.beginArr ();
	for (int i = 0; i < g_tabs.size (); i++)
	{
		Tab *t = g_tabs[i];
		w.beginObj ();
		w.key ("kind"); w.num (t->kind);
		if (t->kind == TAB_ENV) { w.key ("env"); w.str (t->envId.c ()); }
		else if (t->kind == TAB_COLL) { w.key ("coll"); w.str (t->collId.c ()); }
		else
		{
			w.key ("item"); w.str (t->itemId.c ());
			w.key ("section"); w.num (t->reqSection);
			if (t->split > 0) { w.key ("split"); w.num (t->split); }
			// a request changed, or in no collection: its working copy kept
			if (t->dirty || t->itemId.empty ()) { w.key ("dirty"); w.boolean (t->dirty); w.key ("request"); write_request_alone (w, t->req); }
		}
		w.endObj ();
	}
	w.endArr ();
	w.endObj ();
	store_dirs ();
	save_json (F_STATE, w);
	g_stateDirty = false;
}
static void load_state ()
{
	int n = 0; char *b = read_file (F_STATE, &n);
	if (!b) return;
	json::Doc d;
	if (d.parse (b, (unsigned long) n, json::TOLERANT))
	{
		const json::Value &r = d.root ();
		g_store.activeEnv = g_store.findEnv (r["environment"].asStr (""));
		g_side->setMode (iclamp (r["sidebar"].asInt (SB_COLL), 0, 2));
		int sw = r["sidebarWidth"].asInt (0);
		if (sw > 0) ui.hsplit->setSplit (sw);
		for (const json::Value *e = r["tabs"].first (); e; e = e->next)
		{
			int kind = (*e)["kind"].asInt (0);
			Tab *t = new Tab; t->kind = kind;
			if (kind == TAB_ENV)
			{
				t->envId.set ((*e)["env"].asStr (""));
				if (!t->envId.eq ("globals") && g_store.findEnv (t->envId.c ()) < 0) { delete t; continue; }
			}
			else if (kind == TAB_COLL)
			{
				t->collId.set ((*e)["coll"].asStr (""));
				if (!g_store.findColl (t->collId.c ())) { delete t; continue; }
			}
			else
			{
				t->itemId.set ((*e)["item"].asStr (""));
				t->reqSection = iclamp ((*e)["section"].asInt (0), 0, 5);
				t->split = (*e)["split"].asInt (-1);
				Item *it = g_store.findItem (t->itemId.c ());
				if (!it) t->itemId.clear ();
				if (e->has ("request")) { read_request_alone (t->req, (*e)["request"]); t->dirty = (*e)["dirty"].asBool (false); }
				else if (it) { t->req = it->req; t->req.name = it->name; }
				else { delete t; continue; }
			}
			g_tabs.push (t);
		}
		int c = r["current"].asInt (0);
		g_cur = -1;
		if (g_tabs.size ()) select_tab (iclamp (c, 0, g_tabs.size () - 1));
	}
	free (b);
}

// ================================================================================================================
// import, code, cookies
// ================================================================================================================
static bool import_text (const char *s, int n, const char *from)
{
	Str t; s_trim (t, s, n);
	if (t.empty ()) return false;
	char err[200]; err[0] = 0;
	if (s_startsi (t.c (), "curl"))
	{
		Tab *tab = new Tab; Str e;
		if (!parse_curl (tab->req, t.c (), e)) { delete tab; Str m; m.addf ("The cURL command could not be read: %s", e.c ()); tell ("Import", m.c ()); return false; }
		tab->dirty = true;
		add_tab (tab);
		app_status ("The cURL command is a new request: Save it in a collection (Ctrl+S).");
		return true;
	}
	json::Doc d;
	if (!d.parse (t.c (), (unsigned long) t.len (), json::TOLERANT))
	{
		Str m; m.addf ("%s is neither a cURL command nor JSON (%s, line %d).", from, d.error (), d.errLine ());
		tell ("Import", m.c ());
		return false;
	}
	const json::Value &r = d.root ();
	if (r["info"].isObj () && r["item"].isArr ())
	{
		Collection *c = new Collection;
		if (!parse_collection (*c, t.c (), t.len (), err, sizeof err)) { delete c; tell ("Import", err); return false; }
		if (g_store.findColl (c->id.c ())) uuid (c->id);		// (already here: a copy)
		c->root.open = true;
		g_store.colls.push (c);
		save_collection (*c);
		g_side->setMode (SB_COLL);
		app_refresh ();
		int reqs = 0;
		struct C { static int count (Item *it) { int n = it->folder ? 0 : 1; for (int i = 0; i < it->kids.size (); i++) n += count (it->kids[i]); return n; } };
		reqs = C::count (&c->root);
		Str m; m.addf ("Imported the collection \"%s\" (%d request%s).", c->name.c (), reqs, reqs == 1 ? "" : "s");
		app_status (m.c ());
		return true;
	}
	if (r["values"].isArr ())
	{
		bool glob = s_eq (r["_postman_variable_scope"].asStr (""), "globals");
		Environment *e = new Environment;
		if (!parse_environment (*e, t.c (), t.len (), err, sizeof err)) { delete e; tell ("Import", err); return false; }
		if (glob)
		{
			for (int i = 0; i < e->vars.size (); i++) set_var (g_store.globals.vars, e->vars[i].key.c (), e->vars[i].value.c ());
			delete e;
			save_globals (g_store.globals);
			app_refresh ();
			app_status ("The globals were imported (merged).");
			return true;
		}
		if (g_store.findEnv (e->id.c ()) >= 0) uuid (e->id);
		g_store.envs.push (e);
		save_environment (*e);
		g_side->setMode (SB_ENV);
		app_refresh ();
		Str m; m.addf ("Imported the environment \"%s\" (%d variable%s).", e->name.c (), e->vars.size (), e->vars.size () == 1 ? "" : "s");
		app_status (m.c ());
		return true;
	}
	tell ("Import", "This JSON is neither a Postman collection (info, item) nor an environment (values).");
	return false;
}
static bool import_file (const char *path)
{
	int n = 0; char *b = read_file (path, &n, 32 << 20);
	if (!b) { Str m; m.addf ("%s cannot be read.", path); tell ("Import", m.c ()); return false; }
	bool ok = import_text (b, n, base_name (path));
	free (b);
	return ok;
}
static void app_import ()
{
	ImportDialog d;
	d.text->setFocus ();
	int r = d.run ();
	if (r == 2) import_file (d.path.c ());
	else if (r == 1) import_text (d.text->text (), d.text->length (), "The text");
}
static bool app_paste_curl (const char *text)
{
	Str t; s_trim (t, text);
	if (!s_startsi (t.c (), "curl ")) return false;
	Tab *tab = cur_tab ();
	if (!tab || tab->kind != TAB_REQUEST) return false;
	Request r; Str e;
	if (!parse_curl (r, t.c (), e)) return false;
	r.name = tab->req.name; r.tests = tab->req.tests; r.captures = tab->req.captures;
	tab->req = r;
	tab->dirty = true;
	bind_request (tab);
	app_tab_changed ();
	app_status ("The cURL command was read into the request.");
	return true;
}
static void app_code ()
{
	Tab *t = cur_tab ();
	if (!t || t->kind != TAB_REQUEST) return;
	Collection *c = 0; Item *it = tab_item (t, &c);
	update_scope ();
	CodeDialog d (t->req, effective_auth (t->req, it, c));
	d.run ();
}
static void app_cookies () { CookiesDialog d; d.run (); ui.status->invalidate (true); }

// ================================================================================================================
// the window's parts
// ================================================================================================================
static void paint_top (Pane &p)
{
	unsigned bg = p.bg, ink = uk_ink_for (bg);
	p.canvas.fillRect (0, p.height - 1, p.width, 1, c_bgline ());
	// the app's mark: a paper plane in a rounded square
	int x = 16, y = (p.height - 30) / 2;
	uk_rbox (p.canvas, x, y, 30, 30, 8, uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 115));
	unsigned w = uk_ink_on (C_ACCENT);
	for (int i = 0; i < 16; i++)
	{
		int lx = x + 7 + i;
		int t0 = y + 9 + i * 7 / 16, t1 = y + 22 - i * 5 / 16;
		if (t1 > t0) p.canvas.fillRect (lx, t0, 1, t1 - t0, w);
	}
	p.canvas.fillRect (x + 7, y + 15, 12, 2, uk_tone (C_ACCENT, 130));
	TextFace *big = ui.envName ? ui.envName->face : 0;
	{
		UkFaceScope fs (big);
		text_v (p.canvas, x + 40, 0, p.height, "Courier", ink, 2);
	}
	text_v (p.canvas, p.width - 16 - 44 - 8 - 240 - 8 - uk_tw ("Environment"), 0, p.height, "Environment", uk_mix (bg, ink, 150));
}
static void lay_top (Pane &p)
{
	int x = 16 + 40 + 110;
	place (ui.newBtn, x, 9, 88, 32);
	place (ui.importBtn, x + 96, 9, 96, 32);
	place (ui.gear, p.width - 16 - 44, 9, 44, 32);
	place (ui.envPick, p.width - 16 - 44 - 8 - 240, 9, 240, 32);
}
static void lay_main (Pane &p)
{
	int W = p.width, H = p.height;
	place (ui.docTabs, 0, 0, W, 40);
	Widget *v[] = { ui.welcome, ui.reqView, ui.envView, ui.collView };
	for (int i = 0; i < 4; i++) place (v[i], 0, 40, W, H - 40);
}
static void lay_reqview (Pane &p)
{
	static bool once;
	place (ui.vsplit, 0, 0, p.width, p.height);
	if (!once && p.height > 300) { once = true; ui.vsplit->setSplit (p.height * 50 / 100); }
}
static void lay_welcome (Pane &p)
{
	int cx = p.width / 2, y = p.height / 2 + 30;
	Btn *b[3]; int n = 0;
	for (Widget *c = p.firstChild; c && n < 3; c = c->nextSib) b[n++] = (Btn *) c;
	int total = 150 * 3 + 16;
	for (int i = 0; i < n; i++) place (b[i], cx - total / 2 + i * 158, y, 150, 36);
}
static void paint_welcome (Pane &p)
{
	int cx = p.width / 2, y = p.height / 2 - 110;
	// the mark, large
	uk_rbox (p.canvas, cx - 36, y, 72, 72, 18, uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 112));
	unsigned w = uk_ink_on (C_ACCENT);
	for (int i = 0; i < 38; i++)
	{
		int lx = cx - 20 + i, t0 = y + 20 + i * 16 / 38, t1 = y + 52 - i * 12 / 38;
		if (t1 > t0) p.canvas.fillRect (lx, t0, 1, t1 - t0, w);
	}
	p.canvas.fillRect (cx - 20, y + 34, 28, 4, uk_tone (C_ACCENT, 130));
	const char *t = "Courier";
	{ UkFaceScope fs (ui.envName ? ui.envName->face : 0); text_at (p.canvas, cx - uk_tw (t, 2) / 2, y + 86, t, C_FIELD_TEXT, 2); }
	const char *s = "Build, send and test HTTP requests -- the way Postman does, on Onyx.";
	text_at (p.canvas, cx - uk_tw (s) / 2, y + 118, s, c_dim ());
	const char *tips[] = { "Ctrl+N a new request    Ctrl+Enter send    Ctrl+S save    Ctrl+W close the tab",
			       "Paste a cURL command in the URL, or import Postman's collections and environments (Ctrl+O)." };
	for (int i = 0; i < 2; i++) text_at (p.canvas, cx - uk_tw (tips[i]) / 2, p.height / 2 + 100 + i * 24, tips[i], c_faint ());
}
static void lay_env (Pane &p)
{
	int W = p.width, H = p.height;
	place (ui.envActive, W - 16 - 130, 16, 130, 32);
	place (ui.envExport, W - 16 - 130 - 8 - 100, 16, 100, 32);
	place (ui.envName, 12, 14, imax (100, W - 16 - 130 - 8 - 100 - 24 - 12), 36);
	place (ui.envVars, 16, 96, W - 32, H - 96 - 16);
}
static void paint_env (Pane &p)
{
	Environment *e = tab_env (cur_tab ());
	bool glob = e == &g_store.globals;
	const char *h = glob ? "The globals: seen by every request, whatever the environment (an environment's variable of the same name wins)."
			     : "Use its variables as {{name}} in the URLs, the params, the headers, the bodies and the auth, while it is the active one.";
	text_fit (p.canvas, 18, 58, 28, p.width - 36, h, c_dim ());
}
static void lay_coll (Pane &p)
{
	int W = p.width, H = p.height;
	place (ui.collNewReq, W - 16 - 140, 16, 140, 32);
	place (ui.collExport, W - 16 - 140 - 8 - 100, 16, 100, 32);
	place (ui.collName, 12, 14, imax (100, W - 16 - 140 - 8 - 100 - 24 - 12), 36);
	place (ui.collTabs, 0, 60, W, 36);
	place (ui.collDesc, 16, 96 + 52, W - 32, H - 96 - 52 - 16);
	place (ui.collAuth, 0, 96, W, H - 96);
	place (ui.collVars, 16, 96 + 16, W - 32, H - 96 - 32);
}
static int count_items (Item *it, int *folders)
{
	int n = 0;
	for (int i = 0; i < it->kids.size (); i++) { if (it->kids[i]->folder) { (*folders)++; n += count_items (it->kids[i], folders); } else n++; }
	return n;
}
static void paint_coll (Pane &p)
{
	Collection *c = tab_coll (cur_tab ());
	if (!c || ui.collTabs->sel != 0) return;
	int f = 0, n = count_items (&c->root, &f);
	char b[128]; snprintf (b, sizeof b, "%d request%s in %d folder%s", n, n == 1 ? "" : "s", f, f == 1 ? "" : "s");
	text_v (p.canvas, 18, 104, 30, b, c_dim (), 2);
	const char *a = c->auth.type != AUTH_NONE ? AUTH_NAMES[c->auth.type] : "no auth";
	char v[128]; snprintf (v, sizeof v, "Auth: %s   |   %d variable%s", a, c->vars.size () ? c->vars.size () - (c->vars.last ().blank () ? 1 : 0) : 0, c->vars.size () == 2 ? "" : "s");
	text_v (p.canvas, 30 + uk_tw (b, 2), 104, 30, v, c_faint ());
}

// ---- the callbacks of the request's editor ---------------------------------------------------------------------------------------
static void on_name (Widget &) { Tab *t = cur_tab (); if (!t || ui.binding) return; t->req.name = ui.name->text; edited (); }
static void on_method (Widget &) { Tab *t = cur_tab (); if (!t || ui.binding) return; t->req.method = METHODS[ui.method->sel]; edited (); }
static void on_url (Widget &)
{
	Tab *t = cur_tab (); if (!t || ui.binding) return;
	t->req.url = ui.url->text;
	params_from_url (t->req);
	ui.params->refresh ();
	edited ();
}
static void on_params (Widget &)
{
	Tab *t = cur_tab (); if (!t || ui.binding) return;
	url_from_params (t->req);
	ui.url->setText (t->req.url.c ());
	edited ();
}
static void on_table (Widget &) { edited (); }
static void on_section (Widget &) { show_section (ui.sections->sel); }
static void on_body_mode (Widget &) { Tab *t = cur_tab (); if (!t) return; t->req.bodyMode = ui.bodyMode->sel; body_mode_shown (); ui.secBody->invalidate (true); edited (); }
static void on_raw_lang (Widget &) { Tab *t = cur_tab (); if (!t) return; t->req.rawLang = ui.rawLang->sel; body_mode_shown (); edited (); }
static void on_raw (Widget &) { Tab *t = cur_tab (); if (!t || ui.binding) return; t->req.raw.set (ui.raw->text (), ui.raw->length ()); edited (); }
static void on_beautify (Widget &)
{
	Tab *t = cur_tab (); if (!t) return;
	Str out;
	if (t->req.rawLang == RAW_JSON && pretty_json (out, t->req.raw.c (), t->req.raw.len ())) { ui.raw->setText (out.c ()); t->req.raw = out; edited (); }
	else if (t->req.rawLang == RAW_XML && pretty_xml (out, t->req.raw.c (), t->req.raw.len ())) { ui.raw->setText (out.c ()); t->req.raw = out; edited (); }
	else app_status ("The body could not be formatted (is it valid JSON?).", true);
}
static void on_binary (Widget &)
{
	Tab *t = cur_tab (); if (!t) return;
	char p[256];
	if (!uk_file_open (p, sizeof p, "SD:/")) return;
	t->req.binary = p; ui.binaryRow->invalidate (true); edited ();
}
static void on_follow (Widget &) { Tab *t = cur_tab (); if (!t || ui.binding) return; t->req.followRedirects = ui.follow->on; edited (); }
static void on_timeout (Widget &) { Tab *t = cur_tab (); if (!t || ui.binding) return; t->req.timeoutMs = atoi (ui.timeout->text.c ()); if (t->req.timeoutMs < 0) t->req.timeoutMs = 0; edited (); }
static void on_auth (Widget &) { inherit_text (); edited (); }
static void on_resp_tab (Widget &) { Tab *t = cur_tab (); if (!t) return; t->respSection = ui.respTabs->sel; resp_shown (); }
static void on_body_view (Widget &) { Tab *t = cur_tab (); if (!t) return; t->bodyView = ui.bodyView->selected; resp_body_fill (); resp_shown (); }
static void on_find (Widget &) { if (!ui.find->text.empty ()) ui.bodyText->find (ui.find->text.c (), true); }
static void on_find_next (Widget &) { if (!ui.find->text.empty () && !ui.bodyText->find (ui.find->text.c ())) app_status ("Not found."); }
static void on_copy_body (Widget &)
{
	Tab *t = cur_tab (); if (!t || !t->resp) return;
	clip_set_text_n (ui.bodyText->text (), ui.bodyText->length ());
	app_status ("The response's body is in the clipboard.");
}
static void on_save_body (Widget &)
{
	Tab *t = cur_tab (); if (!t || !t->resp) return;
	const char *ct = t->resp->header ("Content-Type"); if (!ct) ct = "";
	int l = (int) strlen (ct);
	const char *ext = s_findi (ct, l, "json") >= 0 ? "json" : s_findi (ct, l, "html") >= 0 ? "html" : s_findi (ct, l, "xml") >= 0 ? "xml"
			: s_findi (ct, l, "png") >= 0 ? "png" : s_findi (ct, l, "jpeg") >= 0 ? "jpg" : s_findi (ct, l, "gif") >= 0 ? "gif" : "txt";
	char nm[64]; snprintf (nm, sizeof nm, "response.%s", ext);
	char p[256];
	if (!uk_file_save (p, sizeof p, "SD:/", nm)) return;
	if (kapi_save_file (p, t->resp->body ? t->resp->body : "", (unsigned) t->resp->bodyLen) >= 0) { Str m; m.addf ("The response was saved: %s", p); app_status (m.c ()); }
	else app_status ("The response could not be written.", true);
}
// the environment's, the collection's editors
static void on_env_name (Widget &)
{
	Environment *e = tab_env (cur_tab ());
	if (!e || e == &g_store.globals || ui.envName->text.empty ()) return;
	e->name = ui.envName->text; app_env_changed (e);
	refresh_doctabs (); refresh_env_pick (); g_side->invalidate (true);
}
static void on_env_vars (Widget &) { Environment *e = tab_env (cur_tab ()); app_env_changed (e); if (e == &g_store.globals) g_store.globals.dirty = true; ui.url->invalidate (true); }
static void on_env_active (Widget &)
{
	Environment *e = tab_env (cur_tab ());
	if (!e || e == &g_store.globals) return;
	int idx = g_store.findEnv (e->id.c ());
	app_set_env (idx == g_store.activeEnv ? -1 : idx);
	bind_env (cur_tab ());
}
static void on_env_export (Widget &)
{
	Environment *e = tab_env (cur_tab ());
	if (!e) return;
	app_export_env (e == &g_store.globals ? -1 : g_store.findEnv (e->id.c ()));
}
static void on_coll_name (Widget &)
{
	Collection *c = tab_coll (cur_tab ());
	if (!c || ui.collName->text.empty ()) return;
	c->name = ui.collName->text; app_collection_changed (c);
	refresh_doctabs (); g_side->invalidate (true);
}
static void on_coll_desc (Widget &) { Collection *c = tab_coll (cur_tab ()); if (!c) return; c->desc.set (ui.collDesc->text (), ui.collDesc->length ()); app_collection_changed (c); }
static void on_coll_auth (Widget &) { app_collection_changed (tab_coll (cur_tab ())); }
static void on_coll_vars (Widget &) { app_collection_changed (tab_coll (cur_tab ())); }
static void on_coll_tab (Widget &) { coll_section (ui.collTabs->sel); }
// the tab bar
static void on_doc_select (int i) { select_tab (i); }
static void on_doc_close (int i) { close_tab (i); }
static void on_doc_new () { app_new_request (0); }
static void on_doc_context (int i, int mx, int my)
{
	int ax = mx + ui.docTabs->left, ay = my;
	for (Widget *w = ui.docTabs->parent; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
	ay += ui.docTabs->top;
	PopupMenu m (ax, ay);
	m.add ("Close", 1, true, "Ctrl+W"); m.add ("Close Other Tabs", 2, g_tabs.size () > 1); m.add ("Close All Tabs", 3);
	m.separator ();
	m.add ("Duplicate", 4, g_tabs[i]->kind == TAB_REQUEST);
	int id = m.run ();
	if (id == 1) close_tab (i);
	else if (id == 2) { Tab *keep = g_tabs[i]; for (int k = g_tabs.size () - 1; k >= 0; k--) if (g_tabs[k] != keep) close_tab (k); }
	else if (id == 3) { for (int k = g_tabs.size () - 1; k >= 0; k--) close_tab (k); }
	else if (id == 4)
	{
		Tab *t = new Tab; t->req = g_tabs[i]->req; t->req.name.add (" Copy"); t->dirty = true;
		select_tab (i); add_tab (t);
	}
}
static void on_env_pick (Widget &) { app_set_env (ui.envPick->sel - 1); }
static void on_gear (Widget &w)
{
	int ax = 0, ay = 0; for (Widget *p = &w; p && p->parent; p = p->parent) { ax += p->left; ay += p->top; }
	PopupMenu m (ax - 150, ay + w.height + 4);
	m.add ("Cookies...", 1); m.add ("Manage Environments", 2); m.separator (); m.add ("Clear the History...", 3);
	int id = m.run ();
	if (id == 1) app_cookies ();
	else if (id == 2) g_side->setMode (SB_ENV);
	else if (id == 3) app_clear_history ();
}

// ================================================================================================================
// building the window
// ================================================================================================================
static const char *const BODY_MODES[] = { "none", "form-data", "x-www-form-urlencoded", "raw", "binary" };
static const char *const VIEWS[] = { "Pretty", "Raw", "Preview" };

static void build (Root &root)
{
	int W = root.width, H = root.height;
	TextFace *title = 0;
	{ FtTextFace *f = new FtTextFace; if (f->open ("DejaVu Sans", 17)) title = f; else delete f; }

	// the top bar
	ui.top = new Pane (C_BG, lay_top, paint_top);
	ui.top->left = 0; ui.top->top = 0; ui.top->resizeTo (W, 50);
	ui.top->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	ui.newBtn = new Btn (0, 0, 88, 32, "New", BTN_SECONDARY, [] (Widget &) { app_new_request (0); }); ui.newBtn->setGlyph (WKG_PLUS); ui.newBtn->tip = "A new request (Ctrl+N)";
	ui.importBtn = new Btn (0, 0, 96, 32, "Import", BTN_SECONDARY, [] (Widget &) { app_import (); }); ui.importBtn->setTool (WKT_OPEN); ui.importBtn->tip = "Postman collections, environments, cURL commands (Ctrl+O)";
	ui.gear = new Btn (0, 0, 44, 32, "", BTN_GHOST, on_gear); ui.gear->setTool (WKT_GEAR); ui.gear->tip = "Cookies, history";
	ui.envPick = new Choice (0, 0, 240, 32, ui.envNames, 1, 0); ui.envPick->onChange = on_env_pick; ui.envPick->tip = "The environment whose variables the requests use";
	ui.envNames[0] = "No Environment";
	ui.top->addChild (ui.newBtn); ui.top->addChild (ui.importBtn); ui.top->addChild (ui.envPick); ui.top->addChild (ui.gear);
	root.addChild (ui.top);

	// the status bar
	ui.status = new Pane (C_BG, 0, paint_status);
	ui.status->left = 0; ui.status->top = H - 26; ui.status->resizeTo (W, 26);
	ui.status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.addChild (ui.status);

	// the sidebar | the main area
	g_side = new Sidebar ();
	ui.main = new Pane (C_BG, lay_main);
	ui.hsplit = new HSplitter (0, 50, W, H - 50 - 26, 330, C_BG);
	ui.hsplit->anchor = ANCHOR_FILL;
	ui.hsplit->minA = 250; ui.hsplit->minB = 560;
	ui.hsplit->gripCol = c_bgline (); ui.hsplit->gripHi = C_ACCENT;
	root.addChild (ui.hsplit);

	// the tabs
	ui.docTabs = new DocTabs (0, 0, 100, 40);
	ui.docTabs->onSelect = on_doc_select; ui.docTabs->onClose = on_doc_close; ui.docTabs->onNew = on_doc_new; ui.docTabs->onContext = on_doc_context;
	ui.main->addChild (ui.docTabs);

	// the welcome
	ui.welcome = new Pane (C_FIELD, lay_welcome, paint_welcome);
	ui.welcome->addChild ((new Btn (0, 0, 150, 36, "New Request", BTN_PRIMARY, [] (Widget &) { app_new_request (0); }))->setGlyph (WKG_PLUS));
	ui.welcome->addChild ((new Btn (0, 0, 150, 36, "Import", BTN_SECONDARY, [] (Widget &) { app_import (); }))->setTool (WKT_OPEN));
	ui.welcome->addChild (new Btn (0, 0, 150, 36, "New Collection", BTN_SECONDARY, [] (Widget &) { app_new_collection (); }));
	ui.main->addChild (ui.welcome);

	// ---- the request ----
	ui.reqView = new Pane (C_FIELD, lay_reqview);
	ui.reqPane = new Pane (C_FIELD, lay_req_pane, paint_req_pane);
	ui.respPane = new Pane (C_FIELD, lay_resp_pane, paint_resp_pane);
	ui.vsplit = new VSplitter (0, 0, 100, 600, 330, C_FIELD);
	ui.vsplit->minA = 200; ui.vsplit->minB = 150;
	ui.vsplit->gripCol = c_line (); ui.vsplit->gripHi = C_ACCENT;
	ui.reqView->addChild (ui.vsplit);

	ui.name = new LineEdit (0, 0, 100, 30); ui.name->framed = false; ui.name->hoverFrame = true; ui.name->style = 2; ui.name->vars = false;
	ui.name->placeholder = "Untitled Request"; ui.name->onChange = on_name; ui.name->tip = "The request's name (click to edit it)";
	ui.save = new Btn (0, 0, 86, 30, "Save", BTN_SECONDARY, [] (Widget &) { app_save (); }); ui.save->setTool (WKT_SAVE); ui.save->tip = "Save in its collection (Ctrl+S)";
	ui.code = new Btn (0, 0, 40, 30, "</>", BTN_GHOST, [] (Widget &) { app_code (); }); ui.code->tip = "The request as code: cURL, HTTP, Python, JavaScript";
	ui.method = new Choice (0, 0, 116, 38, METHODS, M_COUNT, 0); ui.method->colorMethod = true; ui.method->onChange = on_method;
	ui.url = new LineEdit (0, 0, 100, 38); ui.url->placeholder = "Enter the URL, or paste a cURL command"; ui.url->onChange = on_url;
	ui.url->onEnter = [] (Widget &) { app_send (); };
	ui.url->onPaste = [] (LineEdit &, const char *s) { return app_paste_curl (s); };
	ui.send = new Btn (0, 0, 96, 38, "Send", BTN_PRIMARY, [] (Widget &) { app_send (); }); ui.send->tip = "Send the request (Ctrl+Enter)";
	ui.sections = new TabBar (0, 0, 100, 36, C_FIELD);
	ui.sections->add ("Params"); ui.sections->add ("Authorization"); ui.sections->add ("Headers"); ui.sections->add ("Body");
	ui.sections->add ("Tests"); ui.sections->add ("Settings");
	ui.sections->onChange = on_section;
	Widget *reqKids[] = { ui.name, ui.save, ui.code, ui.method, ui.url, ui.send, ui.sections };
	for (int i = 0; i < 7; i++) ui.reqPane->addChild (reqKids[i]);

	// the sections
	ui.secParams = new Pane (C_FIELD, lay_table_sec, paint_params);
	ui.params = new KVTable (0, 0, 100, 100); ui.params->onChange = on_params;
	ui.paramsBulk = new Btn (0, 0, 96, 26, "Bulk Edit", BTN_GHOST, [] (Widget &w) { ui.params->setBulk (!ui.params->bulkMode); ((Btn &) w).setText (ui.params->bulkMode ? "Key-Value Edit" : "Bulk Edit"); });
	ui.secParams->addChild (ui.params); ui.secParams->addChild (ui.paramsBulk);
	ui.secAuth = new Pane (C_FIELD, [] (Pane &p) { place (ui.auth, 0, 0, p.width, p.height); });
	ui.auth = new AuthPanel (false); ui.auth->onChange = on_auth;
	ui.secAuth->addChild (ui.auth);
	ui.secHeaders = new Pane (C_FIELD, lay_table_sec, paint_headers);
	ui.headers = new KVTable (0, 0, 100, 100); ui.headers->onChange = on_table;
	ui.headersBulk = new Btn (0, 0, 96, 26, "Bulk Edit", BTN_GHOST, [] (Widget &w) { ui.headers->setBulk (!ui.headers->bulkMode); ((Btn &) w).setText (ui.headers->bulkMode ? "Key-Value Edit" : "Bulk Edit"); });
	ui.secHeaders->addChild (ui.headers); ui.secHeaders->addChild (ui.headersBulk);
	ui.secBody = new Pane (C_FIELD, lay_body, paint_body);
	ui.bodyMode = new RadioRow (0, 0, 560, 28, BODY_MODES, 5, C_FIELD); ui.bodyMode->onChange = on_body_mode;
	ui.rawLang = new Choice (0, 0, 130, 28, RAW_NAMES, RAW_COUNT, RAW_JSON); ui.rawLang->flat = true; ui.rawLang->onChange = on_raw_lang;
	ui.beautify = new Btn (0, 0, 96, 26, "Beautify", BTN_GHOST, on_beautify); ui.beautify->tip = "Indent the JSON";
	ui.bodyBulk = new Btn (0, 0, 96, 26, "Bulk Edit", BTN_GHOST, [] (Widget &w)
	{
		Tab *t = cur_tab (); if (!t) return;
		KVTable *k = t->req.bodyMode == BODY_FORM ? ui.form : ui.urlenc;
		k->setBulk (!k->bulkMode); ((Btn &) w).setText (k->bulkMode ? "Key-Value Edit" : "Bulk Edit");
	});
	ui.form = new KVTable (0, 0, 100, 100); ui.form->formMode = true; ui.form->onChange = on_table;
	ui.urlenc = new KVTable (0, 0, 100, 100); ui.urlenc->onChange = on_table;
	ui.raw = new CodeEdit (0, 0, 100, 100); ui.raw->vars = true; ui.raw->framed_ = true; ui.raw->onChange = on_raw;
	ui.raw->placeholder = "{ \"name\": \"{{$randomFirstName}}\" }";
	ui.binaryRow = new Pane (C_FIELD, lay_binary, paint_binary);
	ui.binaryPick = new Btn (0, 0, 130, 32, "Select File", BTN_SECONDARY, on_binary); ui.binaryPick->setTool (WKT_OPEN);
	ui.binaryRow->addChild (ui.binaryPick);
	Widget *bodyKids[] = { ui.bodyMode, ui.rawLang, ui.beautify, ui.bodyBulk, ui.form, ui.urlenc, ui.raw, ui.binaryRow };
	for (int i = 0; i < 8; i++) ui.secBody->addChild (bodyKids[i]);
	ui.secTests = new Pane (C_FIELD, lay_tests, paint_tests);
	ui.tests = new KVTable (0, 0, 100, 100); ui.tests->showDesc = false; ui.tests->onChange = on_table;
	ui.tests->colName[0] = "Check"; ui.tests->colName[1] = "Expected"; ui.tests->ph[0] = "status"; ui.tests->ph[1] = "200";
	ui.captures = new KVTable (0, 0, 100, 100); ui.captures->showDesc = false; ui.captures->onChange = on_table;
	ui.captures->colName[0] = "Variable"; ui.captures->colName[1] = "From the response"; ui.captures->ph[0] = "token"; ui.captures->ph[1] = "json.access_token";
	ui.secTests->addChild (ui.tests); ui.secTests->addChild (ui.captures);
	ui.secSettings = new Pane (C_FIELD, lay_settings, paint_settings);
	ui.follow = new ToggleSwitch (0, 0, 420, 30, "Automatically follow redirects", true, on_follow, C_FIELD);
	ui.timeout = new LineEdit (0, 0, 120, 30); ui.timeout->vars = false; ui.timeout->placeholder = "30000"; ui.timeout->onChange = on_timeout;
	ui.secSettings->addChild (ui.follow); ui.secSettings->addChild (ui.timeout);
	Pane *secs[] = { ui.secParams, ui.secAuth, ui.secHeaders, ui.secBody, ui.secTests, ui.secSettings };
	for (int i = 0; i < 6; i++) ui.reqPane->addChild (secs[i]);

	// the response
	ui.respTabs = new TabBar (0, 0, 100, 38, C_FIELD);
	ui.respTabs->add ("Body"); ui.respTabs->add ("Cookies"); ui.respTabs->add ("Headers"); ui.respTabs->add ("Tests"); ui.respTabs->add ("Console");
	ui.respTabs->onChange = on_resp_tab;
	ui.respStatus = new RespStatus ();
	ui.respBody = new Pane (C_FIELD, lay_resp_body, paint_resp_body);
	ui.bodyView = new SegmentedControl (0, 0, 230, 28, VIEWS, 3, 0, on_body_view);
	ui.find = new LineEdit (0, 0, 190, 28); ui.find->vars = false; ui.find->placeholder = "Find in the body"; ui.find->onChange = on_find; ui.find->onEnter = on_find_next;
	ui.copyBody = new Btn (0, 0, 34, 28, "", BTN_GHOST, on_copy_body); ui.copyBody->setTool (WKT_COPY); ui.copyBody->tip = "Copy the body";
	ui.saveBody = new Btn (0, 0, 34, 28, "", BTN_GHOST, on_save_body); ui.saveBody->setTool (WKT_SAVE); ui.saveBody->tip = "Save the response to a file";
	ui.bodyText = new CodeEdit (0, 0, 100, 100); ui.bodyText->readonly = true; ui.bodyText->framed_ = true;
	ui.bodyImage = new ImageBox (0, 0, 100, 100, IMG_FIT, C_FIELD);
	Widget *rbKids[] = { ui.bodyView, ui.find, ui.copyBody, ui.saveBody, ui.bodyText, ui.bodyImage };
	for (int i = 0; i < 6; i++) ui.respBody->addChild (rbKids[i]);
	ui.respHeaders = new KVTable (0, 0, 100, 100); ui.respHeaders->readonly = true; ui.respHeaders->showDesc = false;
	ui.respCookies = new KVTable (0, 0, 100, 100); ui.respCookies->readonly = true;
	ui.respCookies->colName[0] = "Name"; ui.respCookies->colName[1] = "Value"; ui.respCookies->colName[2] = "Domain, path, expiry";
	ui.results = new ResultsView ();
	ui.console = new CodeEdit (0, 0, 100, 100); ui.console->readonly = true; ui.console->framed_ = true; ui.console->lineNumbers = false;
	ui.console->placeholder = "The exchange -- what was sent, what came back -- shows here once the request is sent.";
	ui.respEmpty = new Pane (C_FIELD, 0, paint_resp_empty);
	ui.sending = new SendingView ();
	Widget *rKids[] = { ui.respTabs, ui.respStatus, ui.respBody, ui.respCookies, ui.respHeaders, ui.results, ui.console, ui.respEmpty, ui.sending };
	for (int i = 0; i < 9; i++) ui.respPane->addChild (rKids[i]);
	ui.vsplit->setPanes (ui.reqPane, ui.respPane);
	ui.main->addChild (ui.reqView);

	// ---- an environment ----
	ui.envView = new Pane (C_FIELD, lay_env, paint_env);
	ui.envName = new LineEdit (0, 0, 100, 36); ui.envName->framed = false; ui.envName->hoverFrame = true; ui.envName->style = 2; ui.envName->vars = false;
	ui.envName->face = title; ui.envName->onChange = on_env_name;
	ui.envActive = new Btn (0, 0, 130, 32, "Set Active", BTN_SECONDARY, on_env_active);
	ui.envExport = new Btn (0, 0, 100, 32, "Export", BTN_GHOST, on_env_export); ui.envExport->setTool (WKT_SAVE);
	ui.envVars = new KVTable (0, 0, 100, 100); ui.envVars->showDesc = false; ui.envVars->onChange = on_env_vars;
	ui.envVars->colName[0] = "Variable"; ui.envVars->colName[1] = "Value"; ui.envVars->ph[0] = "Add a new variable"; ui.envVars->ph[1] = "";
	ui.envView->addChild (ui.envName); ui.envView->addChild (ui.envActive); ui.envView->addChild (ui.envExport); ui.envView->addChild (ui.envVars);
	ui.main->addChild (ui.envView);

	// ---- a collection ----
	ui.collView = new Pane (C_FIELD, lay_coll, paint_coll);
	ui.collName = new LineEdit (0, 0, 100, 36); ui.collName->framed = false; ui.collName->hoverFrame = true; ui.collName->style = 2; ui.collName->vars = false;
	ui.collName->face = title; ui.collName->onChange = on_coll_name;
	ui.collNewReq = new Btn (0, 0, 140, 32, "Add Request", BTN_SECONDARY, [] (Widget &) { Collection *c = tab_coll (cur_tab ()); if (c) app_new_request (&c->root); });
	ui.collNewReq->setGlyph (WKG_PLUS);
	ui.collExport = new Btn (0, 0, 100, 32, "Export", BTN_GHOST, [] (Widget &) { Collection *c = tab_coll (cur_tab ()); if (c) app_export_coll (c); });
	ui.collExport->setTool (WKT_SAVE);
	ui.collTabs = new TabBar (0, 0, 100, 36, C_FIELD);
	ui.collTabs->add ("Overview"); ui.collTabs->add ("Authorization"); ui.collTabs->add ("Variables");
	ui.collTabs->onChange = on_coll_tab;
	ui.collDesc = new CodeEdit (0, 0, 100, 100); ui.collDesc->lineNumbers = false; ui.collDesc->framed_ = true; ui.collDesc->onChange = on_coll_desc;
	ui.collDesc->placeholder = "Describe the collection: what it is for, how to use its requests...";
	ui.collAuth = new AuthPanel (true); ui.collAuth->onChange = on_coll_auth;
	ui.collVars = new KVTable (0, 0, 100, 100); ui.collVars->onChange = on_coll_vars;
	ui.collVars->colName[0] = "Variable"; ui.collVars->colName[1] = "Value"; ui.collVars->ph[0] = "Add a new variable"; ui.collVars->ph[1] = "";
	Widget *cKids[] = { ui.collName, ui.collNewReq, ui.collExport, ui.collTabs, ui.collDesc, ui.collAuth, ui.collVars };
	for (int i = 0; i < 7; i++) ui.collView->addChild (cKids[i]);
	ui.main->addChild (ui.collView);

	ui.hsplit->setPanes (g_side, ui.main);
	ui.reqView->hidden = ui.envView->hidden = ui.collView->hidden = true;
}

// ================================================================================================================
// the window
// ================================================================================================================
class CourierRoot : public Root
{
public:
	CourierRoot (int w, int h) : Root (w, h, "Courier"), m_tick (0) {}
	void onTick () override
	{
		unsigned t = kapi_get_ticks ();
		Tab *ct = cur_tab ();
		if (ct && ct->job && t - (unsigned) g_spinT >= 6) { g_spinT = (int) t; ui.sending->phase++; ui.sending->invalidate (true); }
		if (t - m_tick >= 25)
		{
			m_tick = t;
			flush (false);
			if (!ui.statusMsg.empty () && !ui.statusErr && t - ui.statusT > 800) { ui.statusMsg.clear (); ui.status->invalidate (true); }
		}
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER && ctrl_held ()) { app_send (); return true; }
		if (k == 27) { Tab *t = cur_tab (); if (t && t->job) { app_cancel (); return true; } }
		return false;
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		char path[256];
		if (type == DND_FILES && doc_first_path (data, path, sizeof path)) import_file (path);
		else if (type == DND_TEXT) import_text (data, (int) strlen (data), "The text dropped");
	}
private:
	unsigned m_tick;
};

static void cmd_new () { app_new_request (0); }
static void cmd_new_coll () { app_new_collection (); }
static void cmd_new_env () { app_new_env (); }
static void cmd_import () { app_import (); }
static void cmd_save () { app_save (); }
static void cmd_save_as () { app_save_as (); }
static void cmd_close () { if (g_cur >= 0) close_tab (g_cur); }
static void cmd_send () { app_send (); }
static void cmd_code () { app_code (); }
static void cmd_cookies () { app_cookies (); }
static void cmd_history () { g_side->setMode (SB_HIST); }
static void cmd_colls () { g_side->setMode (SB_COLL); }
static void cmd_envs () { g_side->setMode (SB_ENV); }
static void cmd_clear_history () { app_clear_history (); }
static void cmd_next_tab () { if (g_tabs.size ()) select_tab ((g_cur + 1) % g_tabs.size ()); }
static void cmd_prev_tab () { if (g_tabs.size ()) select_tab ((g_cur + g_tabs.size () - 1) % g_tabs.size ()); }
static void cmd_beautify () { Widget w (0, 0, 1, 1); on_beautify (w); }
static void cmd_export_coll ()
{
	Collection *c = tab_coll (cur_tab ());
	if (!c && g_store.colls.size () == 1) c = g_store.colls[0];
	if (!c) { app_status ("Open a request of the collection (or the collection) to export it; or use its menu in the sidebar.", true); return; }
	app_export_coll (c);
}

} // namespace cr

using namespace cr;

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	g_sans = uk_textface ();
	{ FtTextFace *m = new FtTextFace; if (m->open ("DejaVu Sans Mono", 13)) g_mono = m; else delete m; }
	{ FtTextFace *s = new FtTextFace; if (s->open ("DejaVu Sans", 11)) Sidebar::g_small = s; else delete s; }
	int sw = 1280, sh = 800;
	kapi_screen_size (&sw, &sh);
	int W = imin (1360, sw - 60), H = imin (860, sh - 120);
	if (W < 900) W = imax (640, sw);
	if (H < 560) H = imax (480, sh - 40);
	// (a kernel before v66 makes no window over 1024 x 768)
	CourierRoot *rp = new CourierRoot (W, H);
	if (rp->canvas.px == 0 && (W > 1024 || H > 768)) { delete rp; rp = new CourierRoot (imin (W, 1000), imin (H, 700)); }
	if (rp->canvas.px == 0) return 1;
	CourierRoot &root = *rp;
	root.setBg (C_BG);
	root.attach ();
	ui.root = &root;
	build (root);
	root.setResizable (true);
	root.fitWorkArea ();

	static Menu menu;
	menu.menu ("File");
	menu.item ("New Request", "^N", UK_CTRL ('N'), cmd_new);
	menu.item ("New Collection", "", 0, cmd_new_coll);
	menu.item ("New Environment", "", 0, cmd_new_env);
	menu.separator ();
	menu.item ("Import...", "^O", UK_CTRL ('O'), cmd_import);
	menu.item ("Export Collection...", "", 0, cmd_export_coll);
	menu.separator ();
	menu.item ("Save", "^S", UK_CTRL ('S'), cmd_save);
	menu.item ("Save As...", "", 0, cmd_save_as);
	menu.item ("Close Tab", "^W", UK_CTRL ('W'), cmd_close);
	menu.menu ("Request");
	menu.item ("Send", "^Enter", 0, cmd_send);
	menu.item ("Code...", "", 0, cmd_code);
	menu.item ("Beautify the Body", "^B", UK_CTRL ('B'), cmd_beautify);
	menu.separator ();
	menu.item ("Next Tab", "^PgDn", 0, cmd_next_tab);
	menu.item ("Previous Tab", "^PgUp", 0, cmd_prev_tab);
	menu.menu ("View");
	menu.item ("Collections", "", 0, cmd_colls);
	menu.item ("Environments", "", 0, cmd_envs);
	menu.item ("History", "^H", UK_CTRL ('H'), cmd_history);
	menu.menu ("Tools");
	menu.item ("Cookies...", "", 0, cmd_cookies);
	menu.item ("Clear the History...", "", 0, cmd_clear_history);
	menu.publish ();

	load_store (g_store);
	load_cookies ();
	g_side->setMode (SB_COLL);
	load_state ();
	app_refresh ();
	show_views ();
	send_button ();

	// a file named on the command line: imported
	char args[256];
	if (kapi_get_args (args, sizeof args) > 0 && args[0]) { Str a; s_trim (a, args); if (!a.empty ()) import_file (a.c ()); }
	if (!g_tabs.size ()) app_status ("Welcome. New (Ctrl+N) makes a request; Import (Ctrl+O) reads Postman's collections.");

	root.run ();
	flush (true);
	save_state ();
	return 0;
}
