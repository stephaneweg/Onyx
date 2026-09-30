//
// model.h -- Courier's data: requests, collections (folders inside), environments, the globals, the
// history -- and their files, in Postman's own formats (collection v2.1, environment), so that a
// collection made here opens in Postman and one exported from Postman opens here.
//
//   SD:/courier/collections/<id>.postman_collection.json
//   SD:/courier/environments/<id>.postman_environment.json
//   SD:/courier/globals.json, history.json, state.json (the open tabs, the environment chosen...)
//
// What Courier does with Postman's scripts: its own Tests and Captures (declarative rows: check /
// expectation, variable / source) are kept in an "onyx" member of the item AND written as the
// equivalent JavaScript in the item's "test" event, so Postman runs them too. Scripts it cannot run
// (a pre-request script, a test written by hand in Postman) are kept as they are and written back.
//
#ifndef _courier_model_h
#define _courier_model_h

#include "util.h"
#include "json.hpp"

namespace cr {

static const char *const DIR_ROOT = "SD:/courier";
static const char *const DIR_COLL = "SD:/courier/collections";
static const char *const DIR_ENV = "SD:/courier/environments";
static const char *const F_GLOBALS = "SD:/courier/globals.json";
static const char *const F_HISTORY = "SD:/courier/history.json";
static const char *const F_STATE = "SD:/courier/state.json";
static const char *const F_COOKIES = "SD:/courier/cookies.json";
static const char *const SCHEMA_21 = "https://schema.getpostman.com/json/collection/v2.1.0/collection.json";

// ---- the pieces of a request --------------------------------------------------------------------------
struct KV
{
	Str key, value, desc;
	bool on;
	bool file;				// (form-data) value is a file's path on the card
	KV () : on (true), file (false) {}
	KV (const char *k, const char *v, bool enabled = true) : key (k), value (v), on (enabled), file (false) {}
	bool blank () const { return key.empty () && value.empty () && desc.empty (); }
};
typedef Vec<KV> KVList;

enum { M_GET, M_POST, M_PUT, M_PATCH, M_DELETE, M_HEAD, M_OPTIONS, M_COUNT };
static const char *const METHODS[M_COUNT] = { "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS" };
static inline int method_index (const char *m)
{
	for (int i = 0; i < M_COUNT; i++) if (s_eqi (m, METHODS[i])) return i;
	return M_GET;
}

enum { AUTH_INHERIT, AUTH_NONE, AUTH_BEARER, AUTH_BASIC, AUTH_APIKEY, AUTH_COUNT };
static const char *const AUTH_NAMES[AUTH_COUNT] = { "Inherit auth from parent", "No Auth", "Bearer Token", "Basic Auth", "API Key" };
struct Auth
{
	int type;
	Str token, user, pass, key, value;
	bool inQuery;				// API Key: added to the query rather than the headers
	Auth () : type (AUTH_INHERIT), inQuery (false) {}
};

enum { BODY_NONE, BODY_FORM, BODY_URLENC, BODY_RAW, BODY_BINARY, BODY_COUNT };
static const char *const BODY_NAMES[BODY_COUNT] = { "none", "form-data", "x-www-form-urlencoded", "raw", "binary" };
enum { RAW_TEXT, RAW_JSON, RAW_XML, RAW_HTML, RAW_JS, RAW_COUNT };
static const char *const RAW_NAMES[RAW_COUNT] = { "Text", "JSON", "XML", "HTML", "JavaScript" };
static const char *const RAW_LANG[RAW_COUNT] = { "text", "json", "xml", "html", "javascript" };
static const char *const RAW_CTYPE[RAW_COUNT] = { "text/plain", "application/json", "application/xml", "text/html", "application/javascript" };

struct Request
{
	Str name, method, url, desc;
	KVList params, headers;			// params: the URL's query (the disabled ones too)
	Auth auth;
	int bodyMode, rawLang;
	Str raw, binary;			// raw text; binary: a file's path
	KVList form, urlenc;
	KVList tests, captures;			// tests: key = the check, value = the expectation; captures: key = the
						// variable, value = where from (the same expressions)
	bool followRedirects;
	int timeoutMs;				// 0: the app's default
	Str events;				// Postman's events Courier does not run (their JSON), kept
	Request () : method ("GET"), bodyMode (BODY_NONE), rawLang (RAW_JSON), followRedirects (true), timeoutMs (0) {}
};

// ---- collections -------------------------------------------------------------------------------------------
struct Item
{
	Str id, name, desc;
	bool folder, open;
	Request req;				// (a request)
	Vec<Item *> kids;			// (a folder)
	Item *parent;
	Str events;				// (a folder's scripts, kept)
	Auth auth;				// (a folder's auth: Postman lets a folder set one)
	Item () : folder (false), open (false), parent (0) {}
	~Item () { for (int i = 0; i < kids.size (); i++) delete kids[i]; }
	void add (Item *k, int at = -1)
	{
		k->parent = this;
		if (at < 0 || at >= kids.size ()) kids.push (k); else kids.insert (at, k);
	}
	int indexOf (Item *k) const { for (int i = 0; i < kids.size (); i++) if (kids[i] == k) return i; return -1; }
	void detach (Item *k) { int i = indexOf (k); if (i >= 0) kids.remove (i); k->parent = 0; }
	// a deep copy (new ids)
	Item *clone () const
	{
		Item *c = new Item;
		uuid (c->id); c->name = name; c->desc = desc; c->folder = folder; c->open = false;
		c->req = req; c->events = events; c->auth = auth;
		for (int i = 0; i < kids.size (); i++) c->add (kids[i]->clone ());
		return c;
	}
};

struct Collection
{
	Str id, name, desc, file;
	KVList vars;
	Auth auth;				// (the root: AUTH_INHERIT reads as none)
	Item root;				// its items (root.folder = true)
	Str events;
	bool dirty;
	Collection () : dirty (false) { root.folder = true; root.open = true; auth.type = AUTH_NONE; }
};

struct Environment
{
	Str id, name, file;
	KVList vars;
	bool dirty;
	Environment () : dirty (false) {}
};

struct HistoryEntry
{
	long long when;
	int status;
	int ms;
	Request req;
	HistoryEntry () : when (0), status (0), ms (0) {}
};

// ---- the store: every collection, environment, the globals, the history -------------------------------
struct Store
{
	Vec<Collection *> colls;
	Vec<Environment *> envs;
	Environment globals;
	Vec<HistoryEntry> history;		// the most recent first
	int activeEnv;				// index in envs, -1: none
	bool historyDirty;
	Store () : activeEnv (-1), historyDirty (false) { globals.name = "Globals"; }
	Environment *env () { return activeEnv >= 0 && activeEnv < envs.size () ? envs[activeEnv] : 0; }
	Collection *collectionOf (const Item *it)
	{
		while (it && it->parent) it = it->parent;
		for (int i = 0; i < colls.size (); i++) if (&colls[i]->root == it) return colls[i];
		return 0;
	}
	Item *findItem (Item *in, const char *id)
	{
		if (in->id.eq (id)) return in;
		for (int i = 0; i < in->kids.size (); i++) { Item *r = findItem (in->kids[i], id); if (r) return r; }
		return 0;
	}
	Item *findItem (const char *id, Collection **c = 0)
	{
		if (!id || !id[0]) return 0;
		for (int i = 0; i < colls.size (); i++)
		{
			Item *r = findItem (&colls[i]->root, id);
			if (r) { if (c) *c = colls[i]; return r; }
		}
		return 0;
	}
	Collection *findColl (const char *id) { for (int i = 0; i < colls.size (); i++) if (colls[i]->id.eq (id)) return colls[i]; return 0; }
	int findEnv (const char *id) { for (int i = 0; i < envs.size (); i++) if (envs[i]->id.eq (id)) return i; return -1; }
};

// ================================================================================================================
// JSON: reading
// ================================================================================================================
static inline void jstr (Str &out, const json::Value &v)
{
	if (v.isStr ()) out.set (v.s, (int) v.slen_);
	else if (v.isNum () && v.s) out.set (v.s);
	else if (v.isBool ()) out.set (v.b ? "true" : "false");
	else if (v.isObj ()) jstr (out, v["content"]);		// a description as { content, type }
	else out.clear ();
}
// a value kept as JSON text (Postman's events: written back as they were)
static inline void jkeep (Str &out, const json::Value &v)
{
	json::Writer w (false);
	w.value (v);
	out.set (w.data (), (int) w.size ());
}
static inline void read_kvs (KVList &out, const json::Value &arr)
{
	out.clear ();
	for (const json::Value *e = arr.first (); e; e = e->next)
	{
		KV kv;
		jstr (kv.key, (*e)["key"]);
		jstr (kv.value, (*e)["value"]);
		jstr (kv.desc, (*e)["description"]);
		kv.on = !(*e)["disabled"].asBool (false);
		if (e->has ("enabled")) kv.on = (*e)["enabled"].asBool (true);	// (an environment's values)
		if (s_eq ((*e)["type"].asStr (""), "file"))
		{
			kv.file = true;
			const json::Value &src = (*e)["src"];
			if (src.isArr ()) jstr (kv.value, src[0]); else jstr (kv.value, src);
		}
		out.push (kv);
	}
}
static inline void read_auth (Auth &a, const json::Value &v)
{
	if (!v.isObj ()) { a.type = AUTH_INHERIT; return; }
	const char *t = v["type"].asStr ("");
	// Postman: "bearer": [ { key, value } ... ]; v2.0: "bearer": { token: ... }
	struct P { static void get (Str &out, const json::Value &list, const char *k)
	{
		if (list.isObj ()) { jstr (out, list[k]); return; }
		for (const json::Value *e = list.first (); e; e = e->next)
			if (s_eq ((*e)["key"].asStr (""), k)) { jstr (out, (*e)["value"]); return; }
	} };
	if (s_eq (t, "noauth")) a.type = AUTH_NONE;
	else if (s_eq (t, "bearer")) { a.type = AUTH_BEARER; P::get (a.token, v["bearer"], "token"); }
	else if (s_eq (t, "basic")) { a.type = AUTH_BASIC; P::get (a.user, v["basic"], "username"); P::get (a.pass, v["basic"], "password"); }
	else if (s_eq (t, "apikey"))
	{
		a.type = AUTH_APIKEY; Str in;
		P::get (a.key, v["apikey"], "key"); P::get (a.value, v["apikey"], "value"); P::get (in, v["apikey"], "in");
		a.inQuery = in.eq ("query");
	}
	else a.type = AUTH_NONE;			// (a kind Courier does not do: none)
}
// events: the ones Courier makes ("onyx-tests") dropped (made again from its rows), the others kept
static inline void read_events (Str &out, const json::Value &ev)
{
	out.clear ();
	if (!ev.isArr () || !ev.size ()) return;
	json::Writer w (false);
	w.beginArr ();
	int n = 0;
	for (const json::Value *e = ev.first (); e; e = e->next)
	{
		if (s_eq ((*e)["script"]["id"].asStr (""), "onyx-tests")) continue;
		w.value (*e); n++;
	}
	w.endArr ();
	if (n) out.set (w.data (), (int) w.size ());
}
static inline void read_request (Request &r, const json::Value &item)
{
	const json::Value &q = item["request"];
	jstr (r.name, item["name"]);
	if (q.isStr ()) { r.url.set (q.s); r.method = "GET"; return; }	// (v2: a request may be its URL)
	r.method.set (q["method"].asStr ("GET"));
	const json::Value &u = q["url"];
	if (u.isStr ()) r.url.set (u.s);
	else
	{
		jstr (r.url, u["raw"]);
		read_kvs (r.params, u["query"]);
	}
	// the params: the URL's query (the enabled ones) + the disabled ones read above
	{
		KVList all; KVList off;
		for (int i = 0; i < r.params.size (); i++) if (!r.params[i].on) off.push (r.params[i]);
		const char *qm = strchr (r.url.c (), '?');
		if (qm)
		{
			const char *p = qm + 1;
			while (*p)
			{
				const char *e = p; while (*e && *e != '&' && *e != '#') e++;
				if (e > p)
				{
					const char *eq = p; while (eq < e && *eq != '=') eq++;
					KV kv; kv.key.set (p, (int) (eq - p));
					if (eq < e) kv.value.set (eq + 1, (int) (e - eq - 1));
					// (the description Postman keeps for the same key)
					for (int i = 0; i < r.params.size (); i++) if (r.params[i].on && r.params[i].key.eq (kv.key.c ())) { kv.desc = r.params[i].desc; break; }
					all.push (kv);
				}
				if (*e != '&') break;
				p = e + 1;
			}
		}
		for (int i = 0; i < off.size (); i++) all.push (off[i]);
		r.params = all;
	}
	jstr (r.desc, q["description"]);
	read_kvs (r.headers, q["header"]);
	read_auth (r.auth, q["auth"]);
	const json::Value &b = q["body"];
	r.bodyMode = BODY_NONE;
	if (b.isObj () && !b["disabled"].asBool (false))
	{
		const char *m = b["mode"].asStr ("");
		if (s_eq (m, "raw"))
		{
			r.bodyMode = BODY_RAW; jstr (r.raw, b["raw"]);
			const char *lang = b["options"]["raw"]["language"].asStr ("text");
			r.rawLang = RAW_TEXT;
			for (int i = 0; i < RAW_COUNT; i++) if (s_eq (lang, RAW_LANG[i])) r.rawLang = i;
		}
		else if (s_eq (m, "urlencoded")) { r.bodyMode = BODY_URLENC; read_kvs (r.urlenc, b["urlencoded"]); }
		else if (s_eq (m, "formdata")) { r.bodyMode = BODY_FORM; read_kvs (r.form, b["formdata"]); }
		else if (s_eq (m, "file")) { r.bodyMode = BODY_BINARY; jstr (r.binary, b["file"]["src"]); }
		else if (s_eq (m, "graphql"))			// (as raw JSON: { query, variables })
		{
			r.bodyMode = BODY_RAW; r.rawLang = RAW_JSON;
			json::Writer w (true); w.beginObj ();
			w.key ("query"); w.str (b["graphql"]["query"].asStr (""));
			w.key ("variables"); w.str (b["graphql"]["variables"].asStr (""));
			w.endObj (); r.raw.set (w.data ());
		}
	}
	const json::Value &ppb = item["protocolProfileBehavior"];
	r.followRedirects = ppb["followRedirects"].asBool (true);
	r.timeoutMs = item["onyx"]["timeout"].asInt (0);
	read_kvs (r.tests, item["onyx"]["tests"]);
	read_kvs (r.captures, item["onyx"]["captures"]);
	read_events (r.events, item["event"]);
}
static inline void read_items (Item *parent, const json::Value &arr)
{
	for (const json::Value *e = arr.first (); e; e = e->next)
	{
		Item *it = new Item;
		jstr (it->id, (*e)["id"]);
		if (it->id.empty ()) uuid (it->id);
		jstr (it->name, (*e)["name"]);
		if (e->has ("item"))
		{
			it->folder = true;
			jstr (it->desc, (*e)["description"]);
			read_auth (it->auth, (*e)["auth"]);
			read_events (it->events, (*e)["event"]);
			read_items (it, (*e)["item"]);
		}
		else read_request (it->req, *e);
		if (it->name.empty ()) it->name = it->folder ? "New Folder" : "New Request";
		parent->add (it);
	}
}
// A Postman collection (v2.0 / v2.1) from its text. false: not one (err says why).
static inline bool parse_collection (Collection &c, const char *text, int len, char *err, int ecap)
{
	json::Doc d;
	if (!d.parse (text, (unsigned long) len, json::TOLERANT)) { snprintf (err, ecap, "not JSON: %s (line %d)", d.error (), d.errLine ()); return false; }
	const json::Value &r = d.root ();
	if (!r["info"].isObj () || !r["item"].isArr ()) { snprintf (err, ecap, "not a Postman collection (no info / item)"); return false; }
	jstr (c.id, r["info"]["_postman_id"]);
	if (c.id.empty ()) uuid (c.id);
	jstr (c.name, r["info"]["name"]);
	if (c.name.empty ()) c.name = "Collection";
	jstr (c.desc, r["info"]["description"]);
	read_kvs (c.vars, r["variable"]);
	read_auth (c.auth, r["auth"]);
	if (c.auth.type == AUTH_INHERIT) c.auth.type = AUTH_NONE;
	read_events (c.events, r["event"]);
	read_items (&c.root, r["item"]);
	return true;
}
static inline bool parse_environment (Environment &e, const char *text, int len, char *err, int ecap)
{
	json::Doc d;
	if (!d.parse (text, (unsigned long) len, json::TOLERANT)) { snprintf (err, ecap, "not JSON: %s (line %d)", d.error (), d.errLine ()); return false; }
	const json::Value &r = d.root ();
	if (!r["values"].isArr ()) { snprintf (err, ecap, "not a Postman environment (no values)"); return false; }
	jstr (e.id, r["id"]);
	if (e.id.empty ()) uuid (e.id);
	jstr (e.name, r["name"]);
	if (e.name.empty ()) e.name = "Environment";
	read_kvs (e.vars, r["values"]);
	return true;
}

// ================================================================================================================
// JSON: writing
// ================================================================================================================
static inline void write_desc (json::Writer &w, const Str &d) { if (!d.empty ()) { w.key ("description"); w.str (d.c ()); } }
static inline void write_kvs (json::Writer &w, const KVList &l, bool env = false, bool form = false)
{
	w.beginArr ();
	for (int i = 0; i < l.size (); i++)
	{
		const KV &kv = l[i];
		if (kv.blank ()) continue;
		w.beginObj ();
		w.key ("key"); w.str (kv.key.c ());
		if (form && kv.file) { w.key ("type"); w.str ("file"); w.key ("src"); w.str (kv.value.c ()); }
		else
		{
			w.key ("value"); w.str (kv.value.c ());
			if (env) { w.key ("type"); w.str ("default"); w.key ("enabled"); w.boolean (kv.on); }
			else if (form) { w.key ("type"); w.str ("text"); }
		}
		if (!env && !kv.on) { w.key ("disabled"); w.boolean (true); }
		write_desc (w, kv.desc);
		w.endObj ();
	}
	w.endArr ();
}
static inline void write_pair (json::Writer &w, const char *k, const char *v)
{
	w.beginObj (true); w.key ("key"); w.str (k); w.key ("value"); w.str (v); w.key ("type"); w.str ("string"); w.endObj ();
}
static inline void write_auth (json::Writer &w, const Auth &a)
{
	if (a.type == AUTH_INHERIT) return;
	w.key ("auth"); w.beginObj ();
	switch (a.type)
	{
	case AUTH_NONE: w.key ("type"); w.str ("noauth"); break;
	case AUTH_BEARER: w.key ("type"); w.str ("bearer"); w.key ("bearer"); w.beginArr (); write_pair (w, "token", a.token.c ()); w.endArr (); break;
	case AUTH_BASIC:
		w.key ("type"); w.str ("basic"); w.key ("basic"); w.beginArr ();
		write_pair (w, "username", a.user.c ()); write_pair (w, "password", a.pass.c ()); w.endArr ();
		break;
	case AUTH_APIKEY:
		w.key ("type"); w.str ("apikey"); w.key ("apikey"); w.beginArr ();
		write_pair (w, "key", a.key.c ()); write_pair (w, "value", a.value.c ()); write_pair (w, "in", a.inQuery ? "query" : "header");
		w.endArr ();
		break;
	}
	w.endObj ();
}
// the URL as Postman keeps it: raw + its parts
static inline void write_url (json::Writer &w, const Request &r)
{
	w.key ("url"); w.beginObj ();
	w.key ("raw"); w.str (r.url.c ());
	const char *s = r.url.c ();
	const char *qm = strchr (s, '?');
	int end = qm ? (int) (qm - s) : r.url.len ();
	const char *hash = strchr (s, '#'); if (hash && (hash - s) < end) end = (int) (hash - s);
	int p = 0;
	const char *sep = strstr (s, "://");
	if (sep && (sep - s) < end) { w.key ("protocol"); w.str (s, (unsigned) (sep - s)); p = (int) (sep - s) + 3; }
	int hs = p; while (p < end && s[p] != '/') p++;
	int he = p;
	int colon = -1; for (int i = hs; i < he; i++) if (s[i] == ':' && !(i > hs && s[i - 1] == '{')) { colon = i; }
	if (colon >= 0 && strstr (s + hs, "}}") && (strstr (s + hs, "}}") - s) > colon) colon = -1;	// (a {{var}})
	w.key ("host"); w.beginArr (true);
	{
		int a = hs, stop = colon >= 0 ? colon : he;
		bool var = stop - hs > 3 && s[hs] == '{' && s[hs + 1] == '{';
		if (var) w.str (s + hs, (unsigned) (stop - hs));
		else for (int i = hs; i <= stop; i++) if (i == stop || s[i] == '.') { if (i > a) w.str (s + a, (unsigned) (i - a)); a = i + 1; }
	}
	w.endArr ();
	if (colon >= 0) { w.key ("port"); w.str (s + colon + 1, (unsigned) (he - colon - 1)); }
	w.key ("path"); w.beginArr (true);
	{
		int a = he + 1;
		for (int i = he + 1; i <= end; i++) if (i == end || s[i] == '/') { if (i > a) w.str (s + a, (unsigned) (i - a)); a = i + 1; }
	}
	w.endArr ();
	bool anyQ = false;
	for (int i = 0; i < r.params.size (); i++) if (!r.params[i].blank ()) anyQ = true;
	if (anyQ) { w.key ("query"); write_kvs (w, r.params); }
	w.endObj ();
}
// the JavaScript of a check / a source, for Postman ("json.a.b[0]" -> pm.response.json().a.b[0])
static inline void js_expr (Str &out, const char *src)
{
	Str s; s_trim (s, src);
	const char *t = s.c ();
	if (s_eqi (t, "status") || s_eqi (t, "code")) out.add ("pm.response.code");
	else if (s_eqi (t, "time")) out.add ("pm.response.responseTime");
	else if (s_eqi (t, "size")) out.add ("pm.response.responseSize");
	else if (s_eqi (t, "body")) out.add ("pm.response.text()");
	else if (s_startsi (t, "header.") || s_startsi (t, "headers."))
	{ out.add ("pm.response.headers.get(\""); out.add (strchr (t, '.') + 1); out.add ("\")"); }
	else if (s_startsi (t, "cookie.")) { out.add ("pm.cookies.get(\""); out.add (t + 7); out.add ("\")"); }
	else if (s_startsi (t, "json") || t[0] == '$')
	{
		out.add ("pm.response.json()");
		const char *p = t[0] == '$' ? t + 1 : t + 4;
		for (; *p; p++)
		{
			if (*p == '.')
			{
				const char *e = p + 1; while (*e && *e != '.' && *e != '[') e++;
				Str k; k.set (p + 1, (int) (e - p - 1));
				bool ident = k.len () > 0;
				for (int i = 0; i < k.len (); i++) { char c = k.c ()[i]; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || (i && c >= '0' && c <= '9'))) ident = false; }
				if (ident) { out.add ('.'); out.add (k.c ()); } else { out.add ("[\""); out.add (k.c ()); out.add ("\"]"); }
				p = e - 1;
			}
			else out.add (*p);
		}
	}
	else { out.add ("\""); out.add (t); out.add ("\""); }
}
static inline void js_quote (Str &out, const char *s)
{
	out.add ('"');
	for (; *s; s++) { if (*s == '"' || *s == '\\') out.add ('\\'); if (*s == '\n') { out.add ("\\n"); continue; } out.add (*s); }
	out.add ('"');
}
static inline void write_test_event (json::Writer &w, const Request &r)
{
	bool any = false;
	for (int i = 0; i < r.tests.size (); i++) if (r.tests[i].on && !r.tests[i].key.empty ()) any = true;
	for (int i = 0; i < r.captures.size (); i++) if (r.captures[i].on && !r.captures[i].key.empty ()) any = true;
	if (!any) return;
	w.beginObj ();
	w.key ("listen"); w.str ("test");
	w.key ("script"); w.beginObj ();
	w.key ("id"); w.str ("onyx-tests");
	w.key ("type"); w.str ("text/javascript");
	w.key ("exec"); w.beginArr ();
	w.str ("// Made by Onyx's Courier from its Tests and Captures tabs (they are what it runs).");
	for (int i = 0; i < r.tests.size (); i++)
	{
		const KV &t = r.tests[i];
		if (!t.on || t.key.empty ()) continue;
		Str e, name, line, exp; js_expr (e, t.key.c ());
		s_trim (exp, t.value.c ());
		name.add (t.key.c ()); name.add (' '); name.add (exp.empty () ? "exists" : exp.c ());
		line.add ("pm.test("); js_quote (line, name.c ()); line.add (", function () { var v = "); line.add (e.c ()); line.add ("; ");
		const char *x = exp.c ();
		const char *op = "==";
		if (s_starts (x, "!=")) { op = "!="; x += 2; }
		else if (s_starts (x, "<=")) { op = "<="; x += 2; }
		else if (s_starts (x, ">=")) { op = ">="; x += 2; }
		else if (s_starts (x, "<")) { op = "<"; x += 1; }
		else if (s_starts (x, ">")) { op = ">"; x += 1; }
		else if (s_starts (x, "=")) { op = "=="; x += 1; }
		else if (s_startsi (x, "contains ")) { op = "contains"; x += 9; }
		else if (s_startsi (x, "matches ")) { op = "matches"; x += 8; }
		while (*x == ' ') x++;
		if (exp.empty () || s_eqi (x, "exists")) line.add ("pm.expect(v).to.not.be.oneOf([undefined, null]);");
		else if (s_eq (op, "contains")) { line.add ("pm.expect(String(v)).to.include("); js_quote (line, x); line.add (");"); }
		else if (s_eq (op, "matches")) { line.add ("pm.expect(String(v)).to.match(new RegExp("); js_quote (line, x); line.add ("));"); }
		else if (op[0] == '<' || op[0] == '>') { line.add ("pm.expect(Number(v) "); line.add (op); line.add (" Number("); js_quote (line, x); line.add (")).to.be.true;"); }
		else { line.add ("pm.expect(String(v) "); line.add (op[0] == '!' ? "!==" : "==="); line.add (" "); js_quote (line, x); line.add (").to.be.true;"); }
		line.add (" });");
		w.str (line.c ());
	}
	for (int i = 0; i < r.captures.size (); i++)
	{
		const KV &c = r.captures[i];
		if (!c.on || c.key.empty ()) continue;
		Str line, e; js_expr (e, c.value.c ());
		line.add ("pm.environment.set("); js_quote (line, c.key.c ()); line.add (", "); line.add (e.c ()); line.add (");");
		w.str (line.c ());
	}
	w.endArr ();
	w.endObj ();
	w.endObj ();
}
// the kept events (their JSON), plus Courier's own test event
static inline void write_events (json::Writer &w, const Str &kept, const Request *r)
{
	json::Doc d;
	bool hasKept = !kept.empty () && d.parse (kept.c (), (unsigned long) kept.len (), json::TOLERANT) && d.root ().size ();
	bool hasOwn = false;
	if (r)
	{
		for (int i = 0; i < r->tests.size (); i++) if (r->tests[i].on && !r->tests[i].key.empty ()) hasOwn = true;
		for (int i = 0; i < r->captures.size (); i++) if (r->captures[i].on && !r->captures[i].key.empty ()) hasOwn = true;
	}
	if (!hasKept && !hasOwn) return;
	w.key ("event"); w.beginArr ();
	if (hasKept) for (const json::Value *e = d.root ().first (); e; e = e->next) w.value (*e);
	if (hasOwn) write_test_event (w, *r);
	w.endArr ();
}
static inline void write_request_body (json::Writer &w, const Request &r)
{
	if (r.bodyMode == BODY_NONE) return;
	w.key ("body"); w.beginObj ();
	switch (r.bodyMode)
	{
	case BODY_RAW:
		w.key ("mode"); w.str ("raw");
		w.key ("raw"); w.str (r.raw.c ());
		w.key ("options"); w.beginObj (); w.key ("raw"); w.beginObj (true); w.key ("language"); w.str (RAW_LANG[r.rawLang]); w.endObj (); w.endObj ();
		break;
	case BODY_URLENC: w.key ("mode"); w.str ("urlencoded"); w.key ("urlencoded"); write_kvs (w, r.urlenc, false, true); break;
	case BODY_FORM: w.key ("mode"); w.str ("formdata"); w.key ("formdata"); write_kvs (w, r.form, false, true); break;
	case BODY_BINARY: w.key ("mode"); w.str ("file"); w.key ("file"); w.beginObj (true); w.key ("src"); w.str (r.binary.c ()); w.endObj (); break;
	}
	w.endObj ();
}
// one item of a collection (a request: as Postman's item)
static inline void write_request_item (json::Writer &w, const Request &r, const char *id)
{
	w.key ("name"); w.str (r.name.c ());
	if (id && id[0]) { w.key ("id"); w.str (id); }
	write_events (w, r.events, &r);
	if (!r.followRedirects) { w.key ("protocolProfileBehavior"); w.beginObj (true); w.key ("followRedirects"); w.boolean (false); w.endObj (); }
	w.key ("request"); w.beginObj ();
	write_auth (w, r.auth);
	w.key ("method"); w.str (r.method.c ());
	w.key ("header"); write_kvs (w, r.headers);
	write_request_body (w, r);
	write_url (w, r);
	write_desc (w, r.desc);
	w.endObj ();
	w.key ("response"); w.beginArr (true); w.endArr ();
	// Courier's own: its tests and captures (rows), the timeout
	bool t = false;
	for (int i = 0; i < r.tests.size (); i++) if (!r.tests[i].blank ()) t = true;
	for (int i = 0; i < r.captures.size (); i++) if (!r.captures[i].blank ()) t = true;
	if (t || r.timeoutMs)
	{
		w.key ("onyx"); w.beginObj ();
		if (r.timeoutMs) { w.key ("timeout"); w.num (r.timeoutMs); }
		w.key ("tests"); write_kvs (w, r.tests);
		w.key ("captures"); write_kvs (w, r.captures);
		w.endObj ();
	}
}
static inline void write_items (json::Writer &w, const Item *parent)
{
	w.beginArr ();
	for (int i = 0; i < parent->kids.size (); i++)
	{
		const Item *it = parent->kids[i];
		w.beginObj ();
		if (it->folder)
		{
			w.key ("name"); w.str (it->name.c ());
			w.key ("id"); w.str (it->id.c ());
			write_desc (w, it->desc);
			write_auth (w, it->auth);
			write_events (w, it->events, 0);
			w.key ("item"); write_items (w, it);
		}
		else write_request_item (w, it->req, it->id.c ());
		w.endObj ();
	}
	w.endArr ();
}
static inline void write_collection (json::Writer &w, const Collection &c)
{
	w.beginObj ();
	w.key ("info"); w.beginObj ();
	w.key ("_postman_id"); w.str (c.id.c ());
	w.key ("name"); w.str (c.name.c ());
	write_desc (w, c.desc);
	w.key ("schema"); w.str (SCHEMA_21);
	w.endObj ();
	w.key ("item"); write_items (w, &c.root);
	if (c.auth.type != AUTH_NONE) write_auth (w, c.auth);
	write_events (w, c.events, 0);
	bool anyVar = false;
	for (int i = 0; i < c.vars.size (); i++) if (!c.vars[i].blank ()) anyVar = true;
	if (anyVar) { w.key ("variable"); write_kvs (w, c.vars); }
	w.endObj ();
}
static inline void write_environment (json::Writer &w, const Environment &e, const char *scope)
{
	w.beginObj ();
	w.key ("id"); w.str (e.id.c ());
	w.key ("name"); w.str (e.name.c ());
	w.key ("values"); write_kvs (w, e.vars, true);
	w.key ("_postman_variable_scope"); w.str (scope);
	w.endObj ();
}

// ---- a request alone (the open tabs, the history) ------------------------------------------------------
static inline void write_request_alone (json::Writer &w, const Request &r)
{
	w.beginObj ();
	write_request_item (w, r, 0);
	w.endObj ();
}
static inline void read_request_alone (Request &r, const json::Value &v) { read_request (r, v); }

// ================================================================================================================
// the files
// ================================================================================================================
static inline bool save_json (const char *path, json::Writer &w)
{
	if (!w.ok ()) return false;
	return kapi_save_file (path, w.data (), (unsigned) w.size ()) >= 0;
}
static inline void store_dirs ()
{
	kapi_mkdir (DIR_ROOT); kapi_mkdir (DIR_COLL); kapi_mkdir (DIR_ENV);
}
static inline bool save_collection (Collection &c)
{
	store_dirs ();
	if (c.file.empty ()) { c.file.set (DIR_COLL); c.file.add ("/"); c.file.add (c.id.c ()); c.file.add (".postman_collection.json"); }
	json::Writer w (true);
	write_collection (w, c);
	bool ok = save_json (c.file.c (), w);
	if (ok) c.dirty = false;
	return ok;
}
static inline bool save_environment (Environment &e)
{
	store_dirs ();
	if (e.file.empty ()) { e.file.set (DIR_ENV); e.file.add ("/"); e.file.add (e.id.c ()); e.file.add (".postman_environment.json"); }
	json::Writer w (true);
	write_environment (w, e, "environment");
	bool ok = save_json (e.file.c (), w);
	if (ok) e.dirty = false;
	return ok;
}
static inline bool save_globals (Environment &g)
{
	store_dirs ();
	json::Writer w (true);
	write_environment (w, g, "globals");
	bool ok = save_json (F_GLOBALS, w);
	if (ok) g.dirty = false;
	return ok;
}
enum { HISTORY_MAX = 100 };
static inline bool save_history (Store &s)
{
	store_dirs ();
	json::Writer w (false);
	w.beginArr ();
	for (int i = 0; i < s.history.size (); i++)
	{
		const HistoryEntry &h = s.history[i];
		w.beginObj ();
		w.key ("when"); w.num ((long long) h.when);
		w.key ("status"); w.num (h.status);
		w.key ("ms"); w.num (h.ms);
		w.key ("item"); write_request_alone (w, h.req);
		w.endObj ();
	}
	w.endArr ();
	bool ok = save_json (F_HISTORY, w);
	if (ok) s.historyDirty = false;
	return ok;
}
static inline void load_history (Store &s)
{
	int n = 0; char *b = read_file (F_HISTORY, &n);
	if (!b) return;
	json::Doc d;
	if (d.parse (b, (unsigned long) n, json::TOLERANT))
		for (const json::Value *e = d.root ().first (); e && s.history.size () < HISTORY_MAX; e = e->next)
		{
			HistoryEntry &h = s.history.push ();
			h.when = (*e)["when"].asLong (0); h.status = (*e)["status"].asInt (0); h.ms = (*e)["ms"].asInt (0);
			read_request_alone (h.req, (*e)["item"]);
		}
	free (b);
}
static inline bool ends_with (const char *s, const char *e)
{
	int a = (int) strlen (s), b = (int) strlen (e);
	return a >= b && s_eqi (s + a - b, e);
}
static inline void load_store (Store &s)
{
	store_dirs ();
	char err[160];
	void *d = kapi_opendir (DIR_COLL);
	if (d)
	{
		struct kapi_dirent e;
		while (kapi_readdir (d, &e))
		{
			if (e.is_dir || !ends_with (e.name, ".json")) continue;
			Str path; path.addf ("%s/%s", DIR_COLL, e.name);
			int n = 0; char *b = read_file (path.c (), &n);
			if (!b) continue;
			Collection *c = new Collection;
			if (parse_collection (*c, b, n, err, sizeof err)) { c->file = path; c->root.open = false; s.colls.push (c); }
			else delete c;
			free (b);
		}
		kapi_closedir (d);
	}
	// (the collections by name)
	for (int i = 1; i < s.colls.size (); i++)
		for (int j = i; j > 0 && strcmp (s.colls[j - 1]->name.c (), s.colls[j]->name.c ()) > 0; j--)
		{ Collection *t = s.colls[j]; s.colls[j] = s.colls[j - 1]; s.colls[j - 1] = t; }
	d = kapi_opendir (DIR_ENV);
	if (d)
	{
		struct kapi_dirent e;
		while (kapi_readdir (d, &e))
		{
			if (e.is_dir || !ends_with (e.name, ".json")) continue;
			Str path; path.addf ("%s/%s", DIR_ENV, e.name);
			int n = 0; char *b = read_file (path.c (), &n);
			if (!b) continue;
			Environment *en = new Environment;
			if (parse_environment (*en, b, n, err, sizeof err)) { en->file = path; s.envs.push (en); }
			else delete en;
			free (b);
		}
		kapi_closedir (d);
	}
	for (int i = 1; i < s.envs.size (); i++)
		for (int j = i; j > 0 && strcmp (s.envs[j - 1]->name.c (), s.envs[j]->name.c ()) > 0; j--)
		{ Environment *t = s.envs[j]; s.envs[j] = s.envs[j - 1]; s.envs[j - 1] = t; }
	{
		int n = 0; char *b = read_file (F_GLOBALS, &n);
		if (b) { Str keepName = s.globals.name; parse_environment (s.globals, b, n, err, sizeof err); s.globals.name = keepName; free (b); }
		if (s.globals.id.empty ()) uuid (s.globals.id);
	}
	load_history (s);
}

} // namespace cr

#endif
