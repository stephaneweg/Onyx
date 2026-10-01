//
// pkg/pkglib.h -- Onyx's packages (docs/pkg/README.md): what the `pkg` command, the package manager
// (pkgman) and the update daemon (pkgd) share. Header-only, one translation unit; newlib + mbedTLS
// (SHA-256, the index's ECDSA signature) + the Archiver's ZIP engine (Apps/archiver/ops.h) + zlib.
// With PKG_NET (and ONYX_HTTP_TLS) a repository may be http:// or https:// (http.hpp); without, a
// folder only (SD:/..., RAM:/...: the tests, a card with no network).
//
// The repository: index.txt (a section per package: version, size, sha256, file, needs...) and
// index.sig (its ECDSA P-256 / SHA-256 signature, DER in hex) checked with SD:/etc/pkg/onyx.pub;
// the packages pkgs/<name>-<version>.opk: ZIPs of the card's tree plus PKG/manifest.ini.
// The card: SD:/etc/pkg/pkg.ini (repo =, key =); SD:/var/pkg/db/<name>.ini a package installed
// ([package]: name, version, mode manual|auto|never...; [files]: each file and its SHA-256);
// SD:/var/pkg/index.txt the last index whose signature was good; SD:/var/pkg/stage/ the packages
// waiting for the next boot (restart = 1: the system, the firmware), moved in by `pkg commit`
// (the first line of etc/autostart).
//
#ifndef _pkg_pkglib_h
#define _pkg_pkglib_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kapi.h"
#include "Apps/archiver/ops.h"
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#ifdef PKG_NET
#include "http.hpp"
#else
#include "tls/onyx_tls.hpp"		// (PSA's random bytes: kapi_random)
#endif

namespace pkg {

using arc::u64; using arc::u32; using arc::u8;

#define PKG_CONF	"SD:/etc/pkg/pkg.ini"
#define PKG_KEY		"SD:/etc/pkg/onyx.pub"
#define PKG_REPO	"https://stephaneweg.github.io/onyx-packages"
#define PKG_VAR		"SD:/var/pkg"
#define PKG_DB		"SD:/var/pkg/db"
#define PKG_STAGE	"SD:/var/pkg/stage"
#define PKG_DL		"RAM:/pkg"			// the downloads (checked, then extracted)

// ---- strings --------------------------------------------------------------------------------------
static inline char *sdup (const char *s) { size_t n = strlen (s); char *d = (char *) malloc (n + 1); memcpy (d, s, n + 1); return d; }
static inline void cpy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline bool eq (const char *a, const char *b) { return a && b && !strcmp (a, b); }
static inline bool starts (const char *s, const char *p) { return !strncmp (s, p, strlen (p)); }

// "1.10.2" against "1.9": numbers compared one by one (a missing one is 0) -> <0, 0, >0
static inline int vcmp (const char *a, const char *b)
{
	while (*a || *b)
	{
		long x = 0, y = 0;
		while (*a >= '0' && *a <= '9') x = x * 10 + (*a++ - '0');
		while (*b >= '0' && *b <= '9') y = y * 10 + (*b++ - '0');
		if (x != y) return x < y ? -1 : 1;
		while (*a && (*a < '0' || *a > '9')) a++;
		while (*b && (*b < '0' || *b > '9')) b++;
	}
	return 0;
}

// ---- files ------------------------------------------------------------------------------------------
// A whole file -> malloc'd, NUL-terminated (*len its bytes); 0: none.
static inline char *read_file (const char *path, u64 *len = 0)
{
	void *h = kapi_open (path);
	if (!h) return 0;
	u64 n = kapi_fsize64 (h);
	char *b = (char *) malloc ((size_t) n + 1);
	u64 got = 0;
	while (b && got < n)
	{
		u32 want = n - got > (1u << 20) ? (1u << 20) : (u32) (n - got);
		int r = kapi_read (h, b + got, want);
		if (r <= 0) break;
		got += (u64) r;
	}
	kapi_close (h);
	if (!b || got != n) { free (b); return 0; }
	b[n] = 0;
	if (len) *len = n;
	return b;
}
static inline void mkparent (const char *path)
{
	char d[300]; cpy (d, path, sizeof d);
	char *s = strrchr (d, '/');
	if (s && s > d && s[-1] != ':') { *s = 0; arc::mkdirs (d); }
}
static inline bool write_file (const char *path, const void *b, u64 n)
{
	mkparent (path);
	arc::FileSink f;
	if (!f.open (path)) return false;
	bool ok = f.write (b, (u32) n);
	f.close ();
	return ok;
}
static inline bool exists (const char *p) { return arc::path_exists (p); }
// from -> to, replacing to (FAT's rename refuses an existing target); a copy when rename cannot
static inline bool move (const char *from, const char *to)
{
	mkparent (to);
	kapi_remove (to);
	if (kapi_rename (from, to) == 0) return true;
	u64 n; char *b = read_file (from, &n);
	if (!b) return false;
	bool ok = write_file (to, b, n);
	free (b);
	if (ok) kapi_remove (from);
	return ok;
}
// a folder and everything in it
static inline void remove_tree (const char *dir)
{
	void *d = kapi_opendir (dir);
	if (d)
	{
		struct kapi_dirent e;
		char names[64][120]; bool isdir[64]; int n = 0;
		for (;;)
		{
			n = 0;
			while (n < 64 && kapi_readdir (d, &e)) { if (e.name[0] == '.' && (!e.name[1] || (e.name[1] == '.' && !e.name[2]))) continue; cpy (names[n], e.name, 120); isdir[n] = e.is_dir; n++; }
			for (int i = 0; i < n; i++)
			{
				char p[300]; arc::join (p, sizeof p, dir, names[i]);
				if (isdir[i]) remove_tree (p); else kapi_remove (p);
			}
			if (n < 64) break;
			kapi_closedir (d); d = kapi_opendir (dir); if (!d) break;
		}
		if (d) kapi_closedir (d);
	}
	kapi_remove (dir);
}

// ---- SHA-256 ------------------------------------------------------------------------------------------
struct Sha
{
	mbedtls_sha256_context c;
	Sha () { mbedtls_sha256_init (&c); mbedtls_sha256_starts (&c, 0); }
	~Sha () { mbedtls_sha256_free (&c); }
	void add (const void *b, size_t n) { mbedtls_sha256_update (&c, (const unsigned char *) b, n); }
	void bin (u8 out[32]) { mbedtls_sha256_finish (&c, out); }
	void hex (char out[65]) { u8 h[32]; bin (h); for (int i = 0; i < 32; i++) { static const char x[] = "0123456789abcdef"; out[i * 2] = x[h[i] >> 4]; out[i * 2 + 1] = x[h[i] & 15]; } out[64] = 0; }
};
static inline void sha_hex (const void *b, size_t n, char out[65]) { Sha s; s.add (b, n); s.hex (out); }
static inline bool sha_file (const char *path, char out[65])
{
	void *h = kapi_open (path);
	if (!h) return false;
	Sha s; static u8 buf[65536];
	for (;;) { int r = kapi_read (h, buf, sizeof buf); if (r <= 0) break; s.add (buf, (size_t) r); }
	kapi_close (h);
	s.hex (out);
	return true;
}

// The index's signature (DER, in hex) checked with the public key (PEM) -> true: good
static inline bool sig_ok (const char *data, size_t n, const char *sighex, const char *pem)
{
	u8 sig[160]; size_t sl = 0;
	for (const char *p = sighex; p[0] && p[1] && sl < sizeof sig; p += 2)
	{
		auto hv = [] (char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
		int a = hv (p[0]), b = hv (p[1]);
		if (a < 0 || b < 0) break;
		sig[sl++] = (u8) (a * 16 + b);
	}
	if (sl < 8) return false;
	u8 h[32]; { Sha s; s.add (data, n); s.bin (h); }
	mbedtls_pk_context pk; mbedtls_pk_init (&pk);
	bool ok = mbedtls_pk_parse_public_key (&pk, (const unsigned char *) pem, strlen (pem) + 1) == 0 &&
		  mbedtls_pk_verify (&pk, MBEDTLS_MD_SHA256, h, 32, sig, sl) == 0;
	mbedtls_pk_free (&pk);
	return ok;
}

// ---- ini text: [section] and key = value --------------------------------------------------------------
struct Ini
{
	struct Kv { const char *sec, *key, *val; };
	char *buf; Kv *kv; int n, cap;
	Ini () : buf (0), kv (0), n (0), cap (0) {}
	~Ini () { clear (); }
	void clear () { free (buf); free (kv); buf = 0; kv = 0; n = cap = 0; }
	// takes text (malloc'd) as its own
	void parse (char *text)
	{
		clear (); buf = text;
		const char *sec = "";
		for (char *p = buf; p && *p; )
		{
			char *e = p; while (*e && *e != '\n') e++;
			char *next = *e ? e + 1 : e; *e = 0;
			if (e > p && e[-1] == '\r') e[-1] = 0;
			while (*p == ' ' || *p == '\t') p++;
			if (*p == '[') { char *c = strchr (p, ']'); if (c) { *c = 0; sec = p + 1; } }
			else if (*p && *p != '#' && *p != ';')
			{
				char *q = strchr (p, '=');
				if (q)
				{
					char *k = q; *q = 0; while (k > p && (k[-1] == ' ' || k[-1] == '\t')) *--k = 0;
					char *v = q + 1; while (*v == ' ' || *v == '\t') v++;
					char *ve = v + strlen (v); while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) *--ve = 0;
					if (n == cap) { cap = cap ? cap * 2 : 64; kv = (Kv *) realloc (kv, sizeof (Kv) * cap); }
					kv[n].sec = sec; kv[n].key = p; kv[n].val = v; n++;
				}
			}
			p = next;
		}
	}
	bool load (const char *path) { char *t = read_file (path); if (!t) { clear (); return false; } parse (t); return true; }
	const char *get (const char *sec, const char *key, const char *def = "") const
	{
		for (int i = 0; i < n; i++) if (eq (kv[i].sec, sec) && eq (kv[i].key, key)) return kv[i].val;
		return def;
	}
};

// ---- the needs: "onyx >= 2026.10, kapi >= 71" ------------------------------------------------------------
struct Need { char name[40]; char ver[24]; };
static inline int parse_needs (const char *s, Need *out, int max)
{
	int n = 0;
	while (s && *s && n < max)
	{
		while (*s == ' ' || *s == ',') s++;
		if (!*s) break;
		Need &d = out[n]; int k = 0;
		while (*s && *s != ' ' && *s != ',' && *s != '>' && *s != '=') { if (k < 39) d.name[k++] = *s; s++; }
		d.name[k] = 0; d.ver[0] = 0;
		while (*s == ' ') s++;
		if (*s == '>' || *s == '=') { while (*s == '>' || *s == '=' || *s == ' ') s++; k = 0; while (*s && *s != ',' && *s != ' ') { if (k < 23) d.ver[k++] = *s; s++; } d.ver[k] = 0; }
		while (*s && *s != ',') s++;
		if (d.name[0]) n++;
	}
	return n;
}
static inline int kapi_level () { return (int) KT->version; }

// ---- the repository's index ---------------------------------------------------------------------------------
struct Pkg
{
	const char *name, *title, *version, *category, *author, *summary, *file, *sha256, *icon, *needs;
	u64 size, installed; bool required, restart;
};
struct Index
{
	Ini ini; Pkg *p; int n; char date[24];
	Index () : p (0), n (0) { date[0] = 0; }
	~Index () { free (p); }
	void build ()
	{
		free (p); p = 0; n = 0;
		for (int i = 0; i < ini.n; i++)
		{
			const char *s = ini.kv[i].sec;
			if (!*s || eq (s, "repo") || !eq (ini.kv[i].key, "version")) continue;
			p = (Pkg *) realloc (p, sizeof (Pkg) * (n + 1));
			Pkg &k = p[n++];
			k.name = s; k.title = ini.get (s, "title", s); k.version = ini.get (s, "version");
			k.category = ini.get (s, "category", "Other"); k.author = ini.get (s, "author", "");
			k.summary = ini.get (s, "summary"); k.file = ini.get (s, "file"); k.sha256 = ini.get (s, "sha256");
			k.icon = ini.get (s, "icon"); k.needs = ini.get (s, "needs");
			k.size = strtoull (ini.get (s, "size", "0"), 0, 10); k.installed = strtoull (ini.get (s, "installed", "0"), 0, 10);
			k.required = eq (ini.get (s, "required", "0"), "1"); k.restart = eq (ini.get (s, "restart", "0"), "1");
		}
		cpy (date, ini.get ("repo", "date"), sizeof date);
	}
	const Pkg *find (const char *name) const { for (int i = 0; i < n; i++) if (eq (p[i].name, name)) return &p[i]; return 0; }
};

// ---- the packages installed (SD:/var/pkg/db) ------------------------------------------------------------------
struct Inst
{
	Ini ini; char name[40];
	const char *version () const { return ini.get ("package", "version", "0"); }
	const char *mode () const { return ini.get ("package", "mode", "manual"); }
	bool required () const { return eq (ini.get ("package", "required", "0"), "1"); }
	const char *needs () const { return ini.get ("package", "needs"); }
	const char *hash (const char *file) const { return ini.get ("files", file, 0); }
	bool is_config (const char *file) const
	{
		const char *c = ini.get ("package", "config"); size_t l = strlen (file);
		for (const char *s = c; (s = strstr (s, file)) != 0; s += l)
			if ((s == c || s[-1] == ' ') && (s[l] == 0 || s[l] == ' ')) return true;
		return false;
	}
};
struct Db
{
	Inst **v; int n;
	Db () : v (0), n (0) {}
	~Db () { clear (); }
	void clear () { for (int i = 0; i < n; i++) delete v[i]; free (v); v = 0; n = 0; }
	void load ()
	{
		clear ();
		void *d = kapi_opendir (PKG_DB);
		if (!d) return;
		struct kapi_dirent e;
		while (kapi_readdir (d, &e))
		{
			int l = (int) strlen (e.name);
			if (e.is_dir || l < 5 || strcmp (e.name + l - 4, ".ini")) continue;
			Inst *x = new Inst;
			char p[200]; arc::join (p, sizeof p, PKG_DB, e.name);
			if (!x->ini.load (p)) { delete x; continue; }
			cpy (x->name, x->ini.get ("package", "name", ""), sizeof x->name);
			if (!x->name[0]) { e.name[l - 4] = 0; cpy (x->name, e.name, sizeof x->name); }
			v = (Inst **) realloc (v, sizeof (Inst *) * (n + 1)); v[n++] = x;
		}
		kapi_closedir (d);
		for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (v[j - 1]->name, v[j]->name) > 0; j--) { Inst *t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
	}
	Inst *find (const char *name) const { for (int i = 0; i < n; i++) if (eq (v[i]->name, name)) return v[i]; return 0; }
	// a file another installed package has (one moved from a package into another: kept when the old one drops it)
	bool owned_elsewhere (const char *file, const char *self) const
	{ for (int i = 0; i < n; i++) if (!eq (v[i]->name, self) && v[i]->ini.get ("files", file, 0)) return true; return false; }
};

// ---- what a job tells its caller ---------------------------------------------------------------------------------
struct Report
{
	virtual void say (const char *s) { (void) s; }			// a line (pkg prints it)
	virtual void step (const char *what, u64 done, u64 total) { (void) what; (void) done; (void) total; }
	virtual bool cancelled () { return false; }
	virtual ~Report () {}
	void sayf (const char *fmt, const char *a = "", const char *b = "", const char *c = "")
	{ char t[400]; snprintf (t, sizeof t, fmt, a, b, c); say (t); }
};

// The results of a job
enum { OK = 0, E_NET = 1, E_SIG = 2, E_NOTFOUND = 3, E_BADPKG = 4, E_NEEDS = 5, E_RUNNING = 6, E_REQUIRED = 7,
       E_IO = 8, E_USED = 9, E_ALREADY = 10, E_NOTINST = 11, E_CANCEL = 12 };

// A sink that writes a file and hashes what it writes
struct HashSink : arc::Sink
{
	arc::FileSink f; Sha sha; u64 n;
	HashSink () : n (0) {}
	bool write (const void *b, u32 k) override { sha.add (b, k); n += k; return f.write (b, k); }
};
struct MemSink : arc::Sink
{
	char *b; u32 n, cap;
	MemSink () : b (0), n (0), cap (0) {}
	~MemSink () { free (b); }
	bool write (const void *d, u32 k) override
	{
		if (n + k + 1 > cap) { cap = (n + k + 1) * 2; b = (char *) realloc (b, cap); }
		memcpy (b + n, d, k); n += k; b[n] = 0; return true;
	}
};

// ---- the manager ---------------------------------------------------------------------------------------------------
class Manager
{
public:
	char repo[300], key[200];
	Index index; bool haveIndex, verified;
	Db db;
	bool kernelChanged;				// (a commit replaced the kernel or the firmware: reboot)

	Manager () : haveIndex (false), verified (false), kernelChanged (false)
	{
		Ini c; c.load (PKG_CONF);
		cpy (repo, c.get ("", "repo", PKG_REPO), sizeof repo);
		cpy (key, c.get ("", "key", PKG_KEY), sizeof key);
		db.load ();
	}

	// rel of the repository -> malloc'd bytes (*len); want: the size expected (0: unknown)
	char *fetch (const char *rel, u64 *len, u64 want, Report &r)
	{
		char url[600]; cpy (url, repo, sizeof url);
		int l = (int) strlen (url); if (l && url[l - 1] != '/') { url[l++] = '/'; url[l] = 0; }
		strncat (url, rel, sizeof url - strlen (url) - 1);
		if (starts (url, "http://") || starts (url, "https://"))
		{
#ifdef PKG_NET
			int cap = (int) (want ? want + 65536 : 4u << 20);
			char *buf = (char *) malloc ((size_t) cap);
			if (!buf) return 0;
			HttpClient http; http.user_agent ("Onyx-pkg/1.0").timeout_ms (30000);
			struct P { Report *r; const char *rel; u64 want; } pc = { &r, rel, want };
			http.progress ([] (void *c, long got) -> bool
				{ P *p = (P *) c; p->r->step (p->rel, (u64) got, p->want); return !p->r->cancelled (); }, &pc);
			r.step (rel, 0, want);
			HttpResponse res = http.get (url, buf, cap);
			if (!res.ok () || res.truncated)
			{
				char t[80]; snprintf (t, sizeof t, res.status < 0 ? "network error %d" : "HTTP %d", res.status);
				r.sayf ("%s: %s", rel, t); free (buf); return 0;
			}
			memmove (buf, res.body, (size_t) res.body_len); buf[res.body_len] = 0;
			*len = (u64) res.body_len;
			r.step (rel, *len, want);
			return buf;
#else
			r.sayf ("%s: this build has no network (a folder only)", url); return 0;
#endif
		}
		char *b = read_file (url, len);
		if (!b) r.sayf ("%s: not found", url);
		return b;
	}

	// The index from the repository, its signature checked (then kept as SD:/var/pkg/index.txt)
	int refresh (Report &r)
	{
		u64 n = 0, sn = 0;
		char *t = fetch ("index.txt", &n, 0, r);
		if (!t) return load_cached (r) ? OK : E_NET;
		char *s = fetch ("index.sig", &sn, 0, r);
		char *pem = read_file (key);
		bool good = s && pem && sig_ok (t, (size_t) n, s, pem);
		free (s); free (pem);
		if (!good)
		{
			r.sayf ("the index's signature is %s: refused", pem ? "wrong" : "unchecked (no key: " PKG_KEY ")");
			free (t); return E_SIG;
		}
		write_file (PKG_VAR "/index.txt", t, n);
		index.ini.parse (t); index.build (); haveIndex = verified = true;
		return OK;
	}
	bool load_cached (Report &r)
	{
		if (!index.ini.load (PKG_VAR "/index.txt")) return false;
		index.build (); haveIndex = true; verified = false;
		r.sayf ("(the repository could not be read: its last index, of %s)", index.date);
		return true;
	}
	bool load_cached_quiet () { if (!index.ini.load (PKG_VAR "/index.txt")) return false; index.build (); haveIndex = true; return true; }

	// Is one of the package's apps running? (its name -> out)
	bool running (const Ini &files, const char *section, char *out, int cap)
	{
		static char tasks[4096];
		kapi_list_tasks (tasks, sizeof tasks);
		for (int i = 0; i < files.n; i++)
		{
			if (!eq (files.kv[i].sec, section) || !starts (files.kv[i].key, "apps/")) continue;
			const char *a = files.kv[i].key + 5; const char *dot = strstr (a, ".app/");
			if (!dot) continue;
			char app[40]; int k = (int) (dot - a); if (k > 39) k = 39; memcpy (app, a, (size_t) k); app[k] = 0;
			for (char *t = tasks; *t; )
			{
				char *e = t; while (*e && *e != '\n') e++;
				if (e - t > 3 && t[1] == 'a' && (size_t) (e - t - 3) == strlen (app) && !strncmp (t + 3, app, strlen (app))) { cpy (out, app, cap); return true; }
				t = *e ? e + 1 : e;
			}
		}
		return false;
	}

	// The packages to install for `name` (its needs first, those already there left out) -> count; 0 and *err
	int resolve (const char *name, const Pkg **out, int max, int *err, Report &r, int depth = 0)
	{
		const Pkg *p = index.find (name);
		if (!p) { r.sayf ("%s: no such package in the repository", name); *err = E_NOTFOUND; return 0; }
		if (depth > 8) { *err = E_NEEDS; return 0; }
		Need nd[16]; int nn = parse_needs (p->needs, nd, 16), k = 0;
		for (int i = 0; i < nn; i++)
		{
			if (eq (nd[i].name, "kapi"))
			{
				if (kapi_level () < atoi (nd[i].ver))
				{ char t[200]; snprintf (t, sizeof t, "%s needs a newer system (kapi %s, this one %d): update onyx first", name, nd[i].ver, kapi_level ()); r.say (t); *err = E_NEEDS; return 0; }
				continue;
			}
			Inst *in = db.find (nd[i].name);
			if (in && vcmp (in->version (), nd[i].ver) >= 0) continue;
			bool planned = false;
			for (int j = 0; j < k; j++) if (eq (out[j]->name, nd[i].name)) planned = true;
			if (planned) continue;
			const Pkg *q = index.find (nd[i].name);
			if (!q || vcmp (q->version, nd[i].ver) < 0) { char t[200]; snprintf (t, sizeof t, "%s needs %s %s: not in the repository", name, nd[i].name, nd[i].ver); r.say (t); *err = E_NEEDS; return 0; }
			int got = resolve (nd[i].name, out + k, max - k, err, r, depth + 1);
			if (!got && *err) return 0;
			k += got;
		}
		if (k < max) out[k++] = p;
		return k;
	}

	// Download, check, install (or stage) one package
	int install (const Pkg &p, Report &r, bool force = false)
	{
		Inst *old = db.find (p.name);
		// (the package manager updating itself: its programs are loaded whole into memory -- pkgman, pkgd
		//  running go on with the old ones, the new ones start the next time)
		if (!p.restart && old && !force && !eq (p.name, "pkgman"))
		{
			char app[40];
			if (running (old->ini, "files", app, sizeof app))
			{ r.sayf ("%s is running: close it, then try again", app); return E_RUNNING; }
		}
		// the archive, its size and SHA-256 those of the signed index
		u64 n = 0;
		char *b = fetch (p.file, &n, p.size, r);
		if (!b) return E_NET;
		char h[65]; sha_hex (b, (size_t) n, h);
		if (n != p.size || !eq (h, p.sha256)) { free (b); r.sayf ("%s: the download is not the one of the index (refused)", p.name); return E_BADPKG; }
		char opk[200]; snprintf (opk, sizeof opk, PKG_DL "/%s.opk", p.name);
		kapi_mkdir (PKG_DL);
		bool w = write_file (opk, b, n);
		free (b);
		if (!w) { r.sayf ("%s: cannot write %s", p.name, opk); return E_IO; }
		int rc = install_file (opk, &p, old, r);
		kapi_remove (opk);
		return rc;
	}

	// Install the .opk at path (p: its index entry, checked against its manifest; 0: a local package)
	int install_file (const char *path, const Pkg *p, Inst *old, Report &r)
	{
		char why[200];
		arc::Archive *a = arc::archive_open (path, why, sizeof why);
		if (!a) { r.sayf ("%s: %s", path, why); return E_BADPKG; }
		int mi = a->find ("PKG/manifest.ini");
		MemSink ms;
		if (mi < 0 || !a->extract (mi, ms, 0)) { delete a; r.sayf ("%s: not a package (no PKG/manifest.ini)", path); return E_BADPKG; }
		Ini man; man.parse (sdup (ms.b ? ms.b : ""));
		const char *name = man.get ("", "name"), *ver = man.get ("", "version");
		if (!*name || (p && (!eq (name, p->name) || !eq (ver, p->version))))
		{ delete a; r.sayf ("%s: its manifest does not match the index", path); return E_BADPKG; }
		bool stage = eq (man.get ("", "restart", "0"), "1");
		if (!old) old = db.find (name);
		char root[200];
		if (stage) { snprintf (root, sizeof root, PKG_STAGE "/%s", name); remove_tree (root); arc::mkdirs (root); }
		else cpy (root, "SD:", sizeof root);
		// the new database entry
		size_t cap = 4096 + (size_t) a->n * 120; char *dbt = (char *) malloc (cap); size_t dl = 0;
		auto put = [&] (const char *s) { size_t l = strlen (s); if (dl + l + 1 >= cap) { cap = (dl + l + 1) * 2; dbt = (char *) realloc (dbt, cap); } memcpy (dbt + dl, s, l + 1); dl += l; };
		char line[512];
		put ("# installed by pkg\n[package]\n");
		static const char *const keys[] = { "name", "title", "version", "category", "author", "summary", "needs", "required", "restart", "config", 0 };
		for (int i = 0; keys[i]; i++) { snprintf (line, sizeof line, "%s = %s\n", keys[i], man.get ("", keys[i])); put (line); }
		snprintf (line, sizeof line, "mode = %s\n\n[files]\n", old ? old->mode () : "manual"); put (line);
		const char *cfg = man.get ("", "config");
		size_t mcap = 1024; char *mv = (char *) malloc (mcap); size_t ml = 0; mv[0] = 0;	// stage: [moves], [remove]
		auto putm = [&] (const char *s) { size_t l = strlen (s); if (ml + l + 1 >= mcap) { mcap = (ml + l + 1) * 2; mv = (char *) realloc (mv, mcap); } memcpy (mv + ml, s, l + 1); ml += l; };
		int files = 0, kept = 0; u64 done = 0, total = 0;
		for (int i = 0; i < a->n; i++) total += a->e[i].size;
		int rc = OK;
		for (int i = 0; i < a->n && rc == OK; i++)
		{
			const arc::Entry &e = a->e[i];
			if (e.dir || starts (e.name, "PKG/")) continue;
			char rel[260]; arc::safe_rel (e.name, rel, sizeof rel);
			if (!eq (rel, e.name)) { rc = E_BADPKG; r.sayf ("%s: a bad name in the package: %s", name, e.name); break; }
			if (r.cancelled ()) { rc = E_CANCEL; break; }
			// the user's file (config): replaced only if absent or as the package left it
			char target[300]; cpy (target, rel, sizeof target);
			char card[300]; snprintf (card, sizeof card, "SD:/%s", rel);
			size_t l = strlen (rel);
			bool isCfg = false;
			for (const char *s = cfg; (s = strstr (s, rel)) != 0; s += l) if ((s == cfg || s[-1] == ' ') && (s[l] == 0 || s[l] == ' ')) isCfg = true;
			if (isCfg && exists (card))
			{
				char now[65]; const char *was = old ? old->hash (rel) : 0;
				if (!sha_file (card, now) || !was || !eq (now, was)) { strncat (target, ".new", sizeof target - strlen (target) - 1); kept++; }
			}
			char dest[300], tmp[310];
			snprintf (dest, sizeof dest, "%s/%s", root, target);
			snprintf (tmp, sizeof tmp, stage ? "%s" : "%s.pkgtmp", dest);
			mkparent (tmp);
			HashSink hs;
			if (!hs.f.open (tmp)) { rc = E_IO; r.sayf ("cannot write %s", tmp); break; }
			if (!a->extract (i, hs, 0)) { hs.f.close (); kapi_remove (tmp); rc = E_BADPKG; r.sayf ("%s: %s", name, a->error); break; }
			hs.f.close ();
			if (!hs.f.ok) { kapi_remove (tmp); rc = E_IO; r.sayf ("cannot write %s", tmp); break; }
			if (!stage && !move (tmp, dest)) { rc = E_IO; r.sayf ("cannot replace %s", dest); break; }
			char hh[65]; hs.sha.hex (hh);
			snprintf (line, sizeof line, "%s = %s\n", rel, hh); put (line);
			if (stage) { snprintf (line, sizeof line, "%s = 1\n", target); if (!ml) putm ("[moves]\n"); putm (line); }
			files++; done += e.size;
			r.step (name, done, total);
		}
		delete a;
		if (rc != OK) { free (dbt); free (mv); if (stage) { char st[200]; snprintf (st, sizeof st, PKG_STAGE "/%s", name); remove_tree (st); } return rc; }
		// the old version's files the new one has not: gone (the user's changed ones kept)
		Ini now; now.parse (sdup (dbt));
		int gone = 0;
		if (old)
			for (int i = 0; i < old->ini.n; i++)
			{
				if (!eq (old->ini.kv[i].sec, "files") || now.get ("files", old->ini.kv[i].key, 0)) continue;
				if (db.owned_elsewhere (old->ini.kv[i].key, name)) continue;
				char card[300]; snprintf (card, sizeof card, "SD:/%s", old->ini.kv[i].key);
				char hh[65];
				if (old->is_config (old->ini.kv[i].key) && sha_file (card, hh) && !eq (hh, old->ini.kv[i].val)) continue;
				if (stage) { snprintf (line, sizeof line, "%s = 1\n", old->ini.kv[i].key); if (!strstr (mv, "[remove]")) putm ("\n[remove]\n"); putm (line); }
				else { kapi_remove (card); prune (card); }
				gone++;
			}
		char dbp[200];
		if (stage)
		{
			put ("\n"); put (mv);
			snprintf (dbp, sizeof dbp, PKG_STAGE "/%s.ini", name);
		}
		else snprintf (dbp, sizeof dbp, PKG_DB "/%s.ini", name);
		bool ok = write_file (dbp, dbt, dl);
		free (dbt); free (mv);
		if (!ok) { r.sayf ("cannot write %s", dbp); return E_IO; }
		char t[300];
		snprintf (t, sizeof t, "%s %s: %d files %s%s", name, ver, files, stage ? "staged: restart to finish" : "installed",
			  kept ? " (your settings kept: the new ones beside them as .new)" : "");
		r.say (t);
		db.load ();
		return OK;
	}

	// the empty folders above a file removed
	static void prune (const char *file)
	{
		char d[300]; cpy (d, file, sizeof d);
		for (;;)
		{
			char *s = strrchr (d, '/');
			if (!s || s[-1] == ':') break;
			*s = 0;
			void *h = kapi_opendir (d); if (!h) break;
			struct kapi_dirent e; bool empty = true;
			while (kapi_readdir (h, &e)) if (!(e.name[0] == '.' && (!e.name[1] || (e.name[1] == '.' && !e.name[2])))) { empty = false; break; }
			kapi_closedir (h);
			if (!empty || kapi_remove (d) != 0) break;
		}
	}

	// Remove a package: its files (a setting the user changed kept, unless purge)
	int remove (const char *name, bool purge, Report &r, bool force = false)
	{
		Inst *in = db.find (name);
		if (!in) { r.sayf ("%s is not installed", name); return E_NOTINST; }
		if (in->required () && !force) { r.sayf ("%s is part of the system: it cannot be removed", name); return E_REQUIRED; }
		for (int i = 0; i < db.n; i++)
		{
			Need nd[16]; int nn = parse_needs (db.v[i]->needs (), nd, 16);
			for (int k = 0; k < nn; k++) if (eq (nd[k].name, name)) { r.sayf ("%s needs it: remove %s first", db.v[i]->name, db.v[i]->name); return E_USED; }
		}
		char app[40];
		if (!force && running (in->ini, "files", app, sizeof app)) { r.sayf ("%s is running: close it, then try again", app); return E_RUNNING; }
		int gone = 0, kept = 0;
		for (int i = 0; i < in->ini.n; i++)
		{
			if (!eq (in->ini.kv[i].sec, "files")) continue;
			char card[300]; snprintf (card, sizeof card, "SD:/%s", in->ini.kv[i].key);
			char hh[65];
			if (!purge && in->is_config (in->ini.kv[i].key) && sha_file (card, hh) && !eq (hh, in->ini.kv[i].val)) { kept++; continue; }
			if (kapi_remove (card) == 0) gone++;
			prune (card);
		}
		char dbp[200]; snprintf (dbp, sizeof dbp, PKG_DB "/%s.ini", name);
		kapi_remove (dbp);
		char t[200]; snprintf (t, sizeof t, "%s removed: %d files%s", name, gone, kept ? " (your changed settings kept)" : "");
		r.say (t);
		db.load ();
		return OK;
	}

	// The staged packages moved in (at boot, before the desktop: etc/autostart's first line)
	int commit (Report &r)
	{
		void *d = kapi_opendir (PKG_STAGE);
		if (!d) return OK;
		char names[32][64]; int n = 0;
		struct kapi_dirent e;
		while (n < 32 && kapi_readdir (d, &e))
		{
			int l = (int) strlen (e.name);
			if (!e.is_dir && l > 4 && !strcmp (e.name + l - 4, ".ini")) { cpy (names[n], e.name, 64); names[n][l - 4] = 0; n++; }
		}
		kapi_closedir (d);
		for (int k = 0; k < n; k++)
		{
			char ip[200], dir[200]; snprintf (ip, sizeof ip, PKG_STAGE "/%s.ini", names[k]); snprintf (dir, sizeof dir, PKG_STAGE "/%s", names[k]);
			Ini st; if (!st.load (ip)) continue;
			int moved = 0;
			for (int i = 0; i < st.n; i++)
			{
				const char *f = st.kv[i].key;
				if (eq (st.kv[i].sec, "moves"))
				{
					char from[300], to[300]; snprintf (from, sizeof from, "%s/%s", dir, f); snprintf (to, sizeof to, "SD:/%s", f);
					if (!exists (from)) continue;
					if (eq (f, "kernel8-rpi4.img") || eq (f, "start4.elf") || eq (f, "fixup4.dat") || eq (f, "armstub8-rpi4.bin") || strstr (f, ".dtb"))
					{
						kernelChanged = true;
						if (eq (f, "kernel8-rpi4.img")) move (to, "SD:/kernel8-rpi4.img.old");	// (the one to come back to)
					}
					if (move (from, to)) moved++;
				}
				else if (eq (st.kv[i].sec, "remove") && !db.owned_elsewhere (f, names[k])) { char c[300]; snprintf (c, sizeof c, "SD:/%s", f); kapi_remove (c); prune (c); }
			}
			char dbp[200]; snprintf (dbp, sizeof dbp, PKG_DB "/%s.ini", names[k]);
			move (ip, dbp);
			remove_tree (dir);
			char t[200]; snprintf (t, sizeof t, "%s %s: %d files moved in", names[k], st.get ("package", "version"), moved);
			r.say (t);
		}
		remove_tree (PKG_STAGE);
		db.load ();
		return OK;
	}
	bool staged (const char *name) { char p[200]; snprintf (p, sizeof p, PKG_STAGE "/%s.ini", name); return exists (p); }

	// manual / auto / never
	bool set_mode (const char *name, const char *mode)
	{
		Inst *in = db.find (name);
		if (!in) return false;
		char p[200]; snprintf (p, sizeof p, PKG_DB "/%s.ini", name);
		u64 n; char *t = read_file (p, &n);
		if (!t) return false;
		size_t cap = (size_t) n + 64; char *o = (char *) malloc (cap); size_t ol = 0; bool done = false;
		for (char *s = t; *s; )
		{
			char *e = s; while (*e && *e != '\n') e++;
			size_t l = (size_t) (e - s);
			if (!done && l >= 4 && !strncmp (s, "mode", 4) && (s[4] == ' ' || s[4] == '='))
			{ ol += (size_t) snprintf (o + ol, cap - ol, "mode = %s\n", mode); done = true; }
			else { memcpy (o + ol, s, l); ol += l; o[ol++] = '\n'; }
			s = *e ? e + 1 : e;
		}
		free (t);
		bool ok = write_file (p, o, ol);
		free (o);
		db.load ();
		return ok;
	}
	// an update for this one? -> the index's entry
	const Pkg *update_for (const Inst &in) const
	{
		const Pkg *p = index.find (in.name);
		return p && vcmp (p->version, in.version ()) > 0 ? p : 0;
	}
};

} // namespace pkg

#endif
