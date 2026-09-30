//
// vars.h -- {{variables}}: found in the environment chosen, then the request's collection, then the
// globals (Postman's order: the narrower scope wins), or made on the spot ({{$guid}}, {{$timestamp}}...).
// A value may itself hold {{variables}}: resolved again (a few levels deep).
//
#ifndef _courier_vars_h
#define _courier_vars_h

#include "model.h"

namespace cr {

enum { VS_NONE, VS_DYNAMIC, VS_ENV, VS_COLL, VS_GLOBAL };
static const char *const VS_NAMES[] = { "unresolved", "dynamic", "environment", "collection", "global" };

// the dynamic variables Courier makes
static const char *const DYN_VARS[] = { "$guid", "$randomUUID", "$timestamp", "$isoTimestamp", "$randomInt",
	"$randomAlphaNumeric", "$randomBoolean", "$randomFirstName", "$randomLastName", "$randomEmail",
	"$randomCity", "$randomColor", "$randomWord", 0 };

struct Scope
{
	Environment *env;			// the environment chosen (0: none)
	Collection *coll;			// the request's collection (0: none)
	Environment *globals;
	Scope () : env (0), coll (0), globals (0) {}
};

static inline const KV *find_var (const KVList &l, const char *name, int n)
{
	for (int i = l.size () - 1; i >= 0; i--)
		if (l[i].on && l[i].key.len () == n && !strncmp (l[i].key.c (), name, n)) return &l[i];
	return 0;
}

static inline bool dynamic_var (Str &out, const char *name, int n)
{
	static const char *const FIRST[] = { "Ada", "Alan", "Grace", "Linus", "Margaret", "Dennis", "Barbara", "Ken", "Frances", "Tim" };
	static const char *const LAST[] = { "Lovelace", "Turing", "Hopper", "Torvalds", "Hamilton", "Ritchie", "Liskov", "Thompson", "Allen", "Berners-Lee" };
	static const char *const CITY[] = { "Paris", "Lyon", "Brussels", "Geneva", "Montreal", "Lille", "Nantes", "Bordeaux", "Liege", "Quebec" };
	static const char *const WORD[] = { "onyx", "courier", "zircon", "quartz", "agate", "garnet", "topaz", "beryl", "jade", "opal" };
	static const char *const COLOR[] = { "red", "green", "blue", "orange", "purple", "teal", "yellow", "black", "white", "grey" };
	Str nm; nm.set (name, n);
	const char *s = nm.c ();
	if (s_eq (s, "$guid") || s_eq (s, "$randomUUID")) { uuid (out); return true; }
	if (s_eq (s, "$timestamp")) { out.clear (); out.addInt (now_unix ()); return true; }
	if (s_eq (s, "$isoTimestamp")) { out.clear (); iso_time (out, now_unix ()); return true; }
	if (s_eq (s, "$randomInt")) { out.clear (); out.addInt (rnd32 () % 1001); return true; }
	if (s_eq (s, "$randomBoolean")) { out.set (rnd32 () & 1 ? "true" : "false"); return true; }
	if (s_eq (s, "$randomAlphaNumeric")) { out.clear (); out.add ("abcdefghijklmnopqrstuvwxyz0123456789"[rnd32 () % 36]); return true; }
	if (s_eq (s, "$randomFirstName")) { out.set (FIRST[rnd32 () % 10]); return true; }
	if (s_eq (s, "$randomLastName")) { out.set (LAST[rnd32 () % 10]); return true; }
	if (s_eq (s, "$randomCity")) { out.set (CITY[rnd32 () % 10]); return true; }
	if (s_eq (s, "$randomWord")) { out.set (WORD[rnd32 () % 10]); return true; }
	if (s_eq (s, "$randomColor")) { out.set (COLOR[rnd32 () % 10]); return true; }
	if (s_eq (s, "$randomEmail"))
	{
		out.set (FIRST[rnd32 () % 10]); out.add ('.'); out.add (LAST[rnd32 () % 10]); out.addInt (rnd32 () % 100); out.add ("@example.com");
		for (int i = 0; i < out.len (); i++) out.data ()[i] = lc (out.c ()[i]);
		return true;
	}
	return false;
}

// Where {{name}} (name[0..n)) comes from, its value in *val (if given). VS_NONE: nowhere.
static inline int lookup_var (const Scope &sc, const char *name, int n, Str *val)
{
	if (n > 0 && name[0] == '$')
	{
		for (int i = 0; DYN_VARS[i]; i++)
			if ((int) strlen (DYN_VARS[i]) == n && !strncmp (DYN_VARS[i], name, n))
			{ if (val) dynamic_var (*val, name, n); return VS_DYNAMIC; }
		return VS_NONE;
	}
	const KV *kv = 0;
	if (sc.env && (kv = find_var (sc.env->vars, name, n))) { if (val) *val = kv->value; return VS_ENV; }
	if (sc.coll && (kv = find_var (sc.coll->vars, name, n))) { if (val) *val = kv->value; return VS_COLL; }
	if (sc.globals && (kv = find_var (sc.globals->vars, name, n))) { if (val) *val = kv->value; return VS_GLOBAL; }
	return VS_NONE;
}

// The {{...}} in s[0..n) from byte i: its name's start and length, and the end (after "}}"). false: none there.
static inline bool var_at (const char *s, int n, int i, int *ns, int *nl, int *end)
{
	if (i + 1 >= n || s[i] != '{' || s[i + 1] != '{') return false;
	int j = i + 2;
	while (j + 1 < n && !(s[j] == '}' && s[j + 1] == '}'))
	{
		if (s[j] == '{' || s[j] == '\n') return false;
		j++;
	}
	if (j + 1 >= n) return false;
	int a = i + 2, b = j;
	while (a < b && s[a] == ' ') a++;
	while (b > a && s[b - 1] == ' ') b--;
	if (b <= a) return false;
	*ns = a; *nl = b - a; *end = j + 2;
	return true;
}

// s with its {{variables}} replaced (an unknown one stays as written, as Postman does)
static inline void resolve (Str &out, const Scope &sc, const char *s, int depth = 0)
{
	int n = (int) strlen (s);
	out.clear ();
	for (int i = 0; i < n; )
	{
		int ns, nl, end;
		if (var_at (s, n, i, &ns, &nl, &end))
		{
			Str v;
			if (lookup_var (sc, s + ns, nl, &v))
			{
				if (depth < 4 && strstr (v.c (), "{{")) { Str r; resolve (r, sc, v.c (), depth + 1); out.add (r.c ()); }
				else out.add (v.c ());
				i = end;
				continue;
			}
			out.add (s + i, end - i);
			i = end;
			continue;
		}
		out.add (s[i]); i++;
	}
}

// Set (or add) a variable: in the environment chosen, else the globals (the Captures)
static inline void set_var (KVList &l, const char *name, const char *value)
{
	for (int i = 0; i < l.size (); i++)
		if (l[i].key.eq (name)) { l[i].value = value; l[i].on = true; return; }
	// before the list's blank last row (the editors keep one)
	KV kv (name, value);
	int at = l.size ();
	while (at > 0 && l[at - 1].blank ()) at--;
	l.insert (at, kv);
}

} // namespace cr

#endif
