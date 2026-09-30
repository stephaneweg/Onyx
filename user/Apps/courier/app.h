//
// app.h -- what Courier's views share: the store, the open tabs, the cookie jar, and the app's
// commands they call (main.cpp).
//
#ifndef _courier_app_h
#define _courier_app_h

#include "tools.h"
#include "widgets.h"

namespace cr {

enum { TAB_REQUEST, TAB_ENV, TAB_COLL };

struct TestResult { Str name, detail; bool pass; bool capture; };

// An open tab: a request (a working copy of a collection's, or a new one), an environment or a
// collection (edited in place: saved as they change).
struct Tab
{
	int kind;
	Request req;				// (a request) the working copy
	Str itemId;				// ... the collection item it is ("": not saved in a collection)
	bool dirty;
	Response *resp;				// its last response (0: none yet): last's
	Job *last;				// the job that brought it (owned)
	Job *job;				// on its way (0: none)
	Vec<TestResult> results;
	Str envId;				// (an environment) its id, "globals": the globals
	Str collId;				// (a collection) its id
	int reqSection, respSection, bodyView;
	int split;				// the request / response divider (-1: the default)
	Tab () : kind (TAB_REQUEST), dirty (false), resp (0), last (0), job (0), reqSection (0), respSection (0), bodyView (0), split (-1) {}
	~Tab () { delete last; }
private:
	Tab (const Tab &); Tab &operator= (const Tab &);
};

static Store g_store;
static CookieJar g_jar;
static Vec<Tab *> g_tabs;
static int g_cur = -1;				// the tab shown

static inline Tab *cur_tab () { return g_cur >= 0 && g_cur < g_tabs.size () ? g_tabs[g_cur] : 0; }
static inline Item *tab_item (Tab *t, Collection **c = 0) { return t && t->kind == TAB_REQUEST ? g_store.findItem (t->itemId.c (), c) : 0; }
static inline Environment *tab_env (Tab *t)
{
	if (!t || t->kind != TAB_ENV) return 0;
	if (t->envId.eq ("globals")) return &g_store.globals;
	int i = g_store.findEnv (t->envId.c ());
	return i >= 0 ? g_store.envs[i] : 0;
}
static inline Collection *tab_coll (Tab *t)
{
	if (!t) return 0;
	if (t->kind == TAB_COLL) return g_store.findColl (t->collId.c ());
	Collection *c = 0; tab_item (t, &c); return c;
}
// the variables a tab sees
static inline void update_scope ()
{
	g_scope.env = g_store.env ();
	g_scope.globals = &g_store.globals;
	g_scope.coll = tab_coll (cur_tab ());
}

// ---- the app's commands (main.cpp) --------------------------------------------------------------------------------
static void app_status (const char *msg, bool error = false);
static void app_refresh ();				// the model changed: the sidebar, the tabs, the pickers follow
static void app_tab_changed ();				// the tab's request edited: its dirty mark, its title
static void app_open_item (Item *it);
static void app_open_coll (Collection *c);
static void app_open_env (int idx);			// -1: the globals
static void app_open_history (int idx);
static void app_new_request (Item *folder = 0);	// folder: into it (saved at once), else a new tab
static void app_new_folder (Item *parent);
static void app_new_collection ();
static void app_new_env ();
static void app_rename_item (Item *it);
static void app_duplicate_item (Item *it);
static void app_delete_item (Item *it);
static void app_rename_coll (Collection *c);
static void app_duplicate_coll (Collection *c);
static void app_delete_coll (Collection *c);
static void app_export_coll (Collection *c);
static void app_rename_env (int idx);
static void app_duplicate_env (int idx);
static void app_delete_env (int idx);
static void app_export_env (int idx);
static void app_set_env (int idx);
static void app_clear_history ();
static void app_delete_history (int idx);
static void app_send ();
static void app_cancel ();
static void app_save ();
static void app_save_as ();
static void app_code ();
static void app_import ();
static void app_cookies ();
static bool app_paste_curl (const char *text);	// a cURL command pasted in the URL: the request made from it
static void app_move_item (Item *it, int dir);		// up / down among its siblings
static void app_collection_changed (Collection *c);	// (saved soon)
static void app_env_changed (Environment *e);

} // namespace cr

#endif
