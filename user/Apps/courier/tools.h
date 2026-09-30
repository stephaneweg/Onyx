//
// tools.h -- what Courier computes from a response and a request:
//   * the Tests and Captures' expressions: status, time, size, body, header.<Name>, cookie.<name>,
//     json (the whole body), json.a.b[0].c (or $.a.b[0].c) -- and the expectations: "200", "= x",
//     "!= x", "< 500", ">= 1", "contains x", "matches x" (a substring, no regular expressions),
//     "exists" (or nothing);
//   * the code snippets (cURL, raw HTTP, Python requests, JavaScript fetch);
//   * a cURL command read into a request (Import).
//
#ifndef _courier_tools_h
#define _courier_tools_h

#include "net.h"

namespace cr {

// ---- expressions ---------------------------------------------------------------------------------------------------
static inline void json_text (Str &out, const json::Value &v)
{
	switch (v.type)
	{
	case json::STR: out.set (v.s, (int) v.slen_); break;
	case json::NUM: out.set (v.s ? v.s : "0"); break;
	case json::BOOL: out.set (v.b ? "true" : "false"); break;
	case json::NUL: out.set ("null"); break;
	default: { json::Writer w (false); w.value (v); out.set (w.data (), (int) w.size ()); }
	}
}
// the value at a path (".a.b[0]", "a.b", "[2].name") of a JSON value; 0: none
static inline const json::Value *json_path (const json::Value &root, const char *path)
{
	const json::Value *v = &root;
	const char *p = path;
	while (*p && v)
	{
		if (*p == '.') { p++; continue; }
		if (*p == '[')
		{
			p++;
			if (*p == '"' || *p == '\'')
			{
				char q = *p++; const char *e = p; while (*e && *e != q) e++;
				Str k; k.set (p, (int) (e - p));
				v = v->find (k.c ());
				p = *e ? e + 1 : e;
				while (*p && *p != ']') p++;
				if (*p) p++;
				continue;
			}
			int i = atoi (p);
			while (*p && *p != ']') p++;
			if (*p) p++;
			if (!v->isArr ()) return 0;
			if (i < 0) i += (int) v->size ();
			const json::Value &c = (*v)[i];
			v = &c == &json::Value::null () ? 0 : &c;
			continue;
		}
		const char *e = p; while (*e && *e != '.' && *e != '[') e++;
		Str k; k.set (p, (int) (e - p));
		if (v->isArr () && k.len () && k.c ()[0] >= '0' && k.c ()[0] <= '9')
		{ const json::Value &c = (*v)[atoi (k.c ())]; v = &c == &json::Value::null () ? 0 : &c; }
		else v = v->find (k.c ());
		p = e;
	}
	return v;
}

// A response seen by the expressions (its JSON parsed once)
struct Eval
{
	const Response &r;
	json::Doc doc; int parsed;		// 0 not yet, 1 ok, -1 not JSON
	Eval (const Response &resp) : r (resp), parsed (0) {}
	const json::Value *json ()
	{
		if (!parsed) parsed = r.body && doc.parse (r.body, (unsigned long) r.bodyLen, json::TOLERANT) ? 1 : -1;
		return parsed > 0 ? &doc.root () : 0;
	}
	// the expression's value into out; false: it has none (a missing field, a header not sent)
	bool get (const char *expr, Str &out)
	{
		Str e; s_trim (e, expr);
		const char *t = e.c ();
		out.clear ();
		if (s_eqi (t, "status") || s_eqi (t, "code")) { out.addInt (r.status); return true; }
		if (s_eqi (t, "time")) { out.addInt (r.msTotal); return true; }
		if (s_eqi (t, "size")) { out.addInt (r.bodyLen); return true; }
		if (s_eqi (t, "body")) { out.set (r.body ? r.body : "", r.bodyLen); return true; }
		if (s_eqi (t, "reason")) { out = r.reason; return true; }
		if (s_startsi (t, "header.") || s_startsi (t, "headers."))
		{
			const char *h = r.header (strchr (t, '.') + 1);
			if (!h) return false;
			out = h; return true;
		}
		if (s_startsi (t, "cookie."))
		{
			for (int i = 0; i < r.setCookies.size (); i++) if (r.setCookies[i].name.eq (t + 7)) { out = r.setCookies[i].value; return true; }
			return false;
		}
		if (s_startsi (t, "json") || t[0] == '$')
		{
			const json::Value *root = json ();
			if (!root) return false;
			const json::Value *v = json_path (*root, t[0] == '$' ? t + 1 : t + 4);
			if (!v) return false;
			json_text (out, *v);
			return true;
		}
		out = t;				// (a constant)
		return true;
	}
};

static inline bool as_number (const char *s, double *v)
{
	char *end = 0;
	while (*s == ' ') s++;
	if (!*s) return false;
	*v = strtod (s, &end);
	while (end && *end == ' ') end++;
	return end && *end == 0;
}
// one test: check against the expectation -> passed; detail: what was found
static inline bool run_test (Eval &ev, const char *check, const char *expect, Str &detail)
{
	Str got, exp; s_trim (exp, expect);
	bool has = ev.get (check, got);
	const char *x = exp.c ();
	const char *op = "=";
	if (s_starts (x, "!=")) { op = "!="; x += 2; }
	else if (s_starts (x, "<=")) { op = "<="; x += 2; }
	else if (s_starts (x, ">=")) { op = ">="; x += 2; }
	else if (s_starts (x, "==")) { op = "="; x += 2; }
	else if (s_starts (x, "<")) { op = "<"; x += 1; }
	else if (s_starts (x, ">")) { op = ">"; x += 1; }
	else if (s_starts (x, "=")) { op = "="; x += 1; }
	else if (s_startsi (x, "contains ")) { op = "contains"; x += 9; }
	else if (s_startsi (x, "matches ")) { op = "contains"; x += 8; }
	else if (s_startsi (x, "not exists") || s_startsi (x, "missing")) { detail = has ? "found" : "missing"; return !has; }
	while (*x == ' ') x++;
	Str want; want = x;
	// (a quoted expectation: its text)
	if (want.len () >= 2 && want.c ()[0] == '"' && want.c ()[want.len () - 1] == '"') { Str t; t.set (want.c () + 1, want.len () - 2); want = t; }
	if (!has) { detail = "missing"; return false; }
	detail.clear ();
	Str shown; shown.set (got.c (), imin (got.len (), 80)); if (got.len () > 80) shown.add ("...");
	detail.addf ("got %s", shown.c ());
	if (exp.empty () || s_eqi (x, "exists")) return true;
	double a, b;
	bool num = as_number (got.c (), &a) && as_number (want.c (), &b);
	if (s_eq (op, "contains")) return s_findi (got.c (), got.len (), want.c ()) >= 0;
	if (s_eq (op, "=")) return num ? a == b : got.eq (want.c ());
	if (s_eq (op, "!=")) return num ? a != b : !got.eq (want.c ());
	if (!num) { detail.add (" (not a number)"); return false; }
	if (s_eq (op, "<")) return a < b;
	if (s_eq (op, "<=")) return a <= b;
	if (s_eq (op, ">")) return a > b;
	if (s_eq (op, ">=")) return a >= b;
	return false;
}

// ---- code snippets --------------------------------------------------------------------------------------------------
enum { SN_CURL, SN_HTTP, SN_PYTHON, SN_JS, SN_COUNT };
static const char *const SNIPPET_NAMES[SN_COUNT] = { "cURL", "HTTP", "Python - requests", "JavaScript - fetch" };

static inline void sh_quote (Str &out, const char *s)
{
	out.add ('\'');
	for (; *s; s++) { if (*s == '\'') out.add ("'\\''"); else out.add (*s); }
	out.add ('\'');
}
static inline void c_quote (Str &out, const char *s, int n = -1)	// "..." with escapes (Python, JS)
{
	if (n < 0) n = (int) strlen (s);
	out.add ('"');
	for (int i = 0; i < n; i++)
	{
		char c = s[i];
		if (c == '"' || c == '\\') { out.add ('\\'); out.add (c); }
		else if (c == '\n') out.add ("\\n");
		else if (c == '\r') out.add ("\\r");
		else if (c == '\t') out.add ("\\t");
		else out.add (c);
	}
	out.add ('"');
}
// is this header one of the defaults Courier adds (left out of the snippets)?
static inline bool default_header (const KV &h)
{
	return (s_eqi (h.key.c (), "User-Agent") && h.value.eq (USER_AGENT)) || (s_eqi (h.key.c (), "Accept") && h.value.eq ("*/*"))
	    || (s_eqi (h.key.c (), "Accept-Encoding") && h.value.eq ("gzip, deflate"));
}
// The snippet of the request (its variables resolved) in the language chosen.
static inline void make_snippet (Str &out, int lang, const Request &r, const Scope &sc, const Auth &auth)
{
	CookieJar none;
	Prepared p; prepare (p, r, sc, auth, none);
	bool form = r.bodyMode == BODY_FORM;
	KVList hs;
	for (int i = 0; i < p.headers.size (); i++)
	{
		if (default_header (p.headers[i])) continue;
		if (form && s_eqi (p.headers[i].key.c (), "Content-Type") && s_startsi (p.headers[i].value.c (), "multipart/")) continue;
		hs.push (p.headers[i]);
	}
	// the form's fields, resolved
	KVList fields;
	if (form)
		for (int i = 0; i < r.form.size (); i++)
		{
			if (!r.form[i].on || r.form[i].key.empty ()) continue;
			KV kv; resolve (kv.key, sc, r.form[i].key.c ()); resolve (kv.value, sc, r.form[i].value.c ()); kv.file = r.form[i].file;
			fields.push (kv);
		}
	bool binBody = r.bodyMode == BODY_BINARY;
	Str binPath; if (binBody) resolve (binPath, sc, r.binary.c ());
	out.clear ();
	switch (lang)
	{
	case SN_CURL:
		out.add ("curl --location");
		if (!p.method.eq ("GET")) { out.add (" --request "); out.add (p.method.c ()); }
		out.add (" "); sh_quote (out, p.url.c ());
		for (int i = 0; i < hs.size (); i++) { Str h; h.addf ("%s: %s", hs[i].key.c (), hs[i].value.c ()); out.add (" \\\n  --header "); sh_quote (out, h.c ()); }
		if (form)
			for (int i = 0; i < fields.size (); i++)
			{
				Str f; f.add (fields[i].key.c ()); f.add (fields[i].file ? "=@\"" : "=\""); f.add (fields[i].value.c ()); f.add ('"');
				out.add (" \\\n  --form "); sh_quote (out, f.c ());
			}
		else if (binBody) { Str f ("@"); f.add (binPath.c ()); out.add (" \\\n  --data-binary "); sh_quote (out, f.c ()); }
		else if (p.bodyLen > 0) { Str b; b.set (p.body, p.bodyLen); out.add (" \\\n  --data-raw "); sh_quote (out, b.c ()); }
		out.add ("\n");
		break;
	case SN_HTTP:
	{
		Url u; u.parse (p.url.c ());
		out.addf ("%s %s HTTP/1.1\nHost: %s", p.method.c (), u.path.c (), u.host.c ());
		if ((u.https && u.port != 443) || (!u.https && u.port != 80)) out.addf (":%u", u.port);
		out.add ('\n');
		for (int i = 0; i < hs.size (); i++) out.addf ("%s: %s\n", hs[i].key.c (), hs[i].value.c ());
		if (form)
		{
			// (the boundary made again: readable)
			out.add ("Content-Type: multipart/form-data; boundary=----CourierBoundary\n\n");
			for (int i = 0; i < fields.size (); i++)
			{
				out.add ("------CourierBoundary\n");
				if (fields[i].file) out.addf ("Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\nContent-Type: %s\n\n(the file's bytes: %s)\n",
							      fields[i].key.c (), base_name (fields[i].value.c ()), mime_of (fields[i].value.c ()), fields[i].value.c ());
				else out.addf ("Content-Disposition: form-data; name=\"%s\"\n\n%s\n", fields[i].key.c (), fields[i].value.c ());
			}
			out.add ("------CourierBoundary--\n");
		}
		else if (binBody) out.addf ("Content-Length: %d\n\n(the file's bytes: %s)\n", p.bodyLen, binPath.c ());
		else if (p.bodyLen > 0) { out.addf ("Content-Length: %d\n\n", p.bodyLen); out.add (p.body, p.bodyLen); out.add ('\n'); }
		break;
	}
	case SN_PYTHON:
		out.add ("import requests\n\n");
		out.add ("url = "); c_quote (out, p.url.c ()); out.add ("\n\n");
		if (form)
		{
			out.add ("payload = {");
			bool first = true;
			for (int i = 0; i < fields.size (); i++) if (!fields[i].file) { if (!first) out.add (", "); first = false; c_quote (out, fields[i].key.c ()); out.add (": "); c_quote (out, fields[i].value.c ()); }
			out.add ("}\nfiles = [");
			first = true;
			for (int i = 0; i < fields.size (); i++) if (fields[i].file)
			{
				if (!first) out.add (",");
				first = false;
				out.add ("\n  ("); c_quote (out, fields[i].key.c ()); out.add (", ("); c_quote (out, base_name (fields[i].value.c ()));
				out.add (", open("); c_quote (out, fields[i].value.c ()); out.add (", \"rb\"), "); c_quote (out, mime_of (fields[i].value.c ())); out.add ("))");
			}
			out.add (first ? "]\n" : "\n]\n");
		}
		else if (binBody) { out.add ("payload = open("); c_quote (out, binPath.c ()); out.add (", \"rb\").read()\n"); }
		else { out.add ("payload = "); c_quote (out, p.body ? p.body : "", p.bodyLen); out.add ("\n"); }
		out.add ("headers = {");
		for (int i = 0; i < hs.size (); i++) { out.add ("\n  "); c_quote (out, hs[i].key.c ()); out.add (": "); c_quote (out, hs[i].value.c ()); out.add (i + 1 < hs.size () ? "," : "\n"); }
		out.add ("}\n\n");
		out.addf ("response = requests.request(\"%s\", url, headers=headers, data=payload%s)\n\n", p.method.c (), form ? ", files=files" : "");
		out.add ("print(response.text)\n");
		break;
	case SN_JS:
		out.add ("const myHeaders = new Headers();\n");
		for (int i = 0; i < hs.size (); i++) { out.add ("myHeaders.append("); c_quote (out, hs[i].key.c ()); out.add (", "); c_quote (out, hs[i].value.c ()); out.add (");\n"); }
		out.add ("\n");
		if (form)
		{
			out.add ("const body = new FormData();\n");
			for (int i = 0; i < fields.size (); i++)
			{
				out.add ("body.append("); c_quote (out, fields[i].key.c ()); out.add (", ");
				if (fields[i].file) { out.add ("fileInput.files[0], "); c_quote (out, base_name (fields[i].value.c ())); }
				else c_quote (out, fields[i].value.c ());
				out.add (");\n");
			}
			out.add ("\n");
		}
		else if (binBody) out.add ("const body = fileInput.files[0];\n\n");
		else if (p.bodyLen > 0) { out.add ("const body = "); c_quote (out, p.body, p.bodyLen); out.add (";\n\n"); }
		out.add ("const requestOptions = {\n  method: "); c_quote (out, p.method.c ()); out.add (",\n  headers: myHeaders,\n");
		if (form || binBody || p.bodyLen > 0) out.add ("  body: body,\n");
		out.add ("  redirect: \"follow\"\n};\n\n");
		out.add ("fetch("); c_quote (out, p.url.c ()); out.add (", requestOptions)\n  .then((response) => response.text())\n");
		out.add ("  .then((result) => console.log(result))\n  .catch((error) => console.error(error));\n");
		break;
	}
}

// ---- a cURL command read ---------------------------------------------------------------------------------------------
static inline void shell_words (Vec<Str> &out, const char *s)
{
	Str w; bool in = false;
	while (*s)
	{
		char c = *s;
		if (c == '\\' && (s[1] == '\n' || (s[1] == '\r' && s[2] == '\n'))) { s += s[1] == '\n' ? 2 : 3; continue; }
		if (c == '^' && (s[1] == '\n' || (s[1] == '\r' && s[2] == '\n'))) { s += s[1] == '\n' ? 2 : 3; continue; }	// (Windows' cmd)
		if (is_space (c)) { if (in) { out.push (w); w.clear (); in = false; } s++; continue; }
		in = true;
		if (c == '\'') { s++; while (*s && *s != '\'') w.add (*s++); if (*s) s++; continue; }
		if (c == '"')
		{
			s++;
			while (*s && *s != '"')
			{
				if (*s == '\\' && (s[1] == '"' || s[1] == '\\' || s[1] == '$' || s[1] == '`')) s++;
				w.add (*s++);
			}
			if (*s) s++;
			continue;
		}
		if (c == '$' && s[1] == '\'')				// $'...': C escapes
		{
			s += 2;
			while (*s && *s != '\'')
			{
				if (*s == '\\' && s[1]) { s++; char e = *s++; w.add (e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e); continue; }
				w.add (*s++);
			}
			if (*s) s++;
			continue;
		}
		if (c == '\\' && s[1]) { w.add (s[1]); s += 2; continue; }
		w.add (c); s++;
	}
	if (in) out.push (w);
}
// A cURL command into r (its name: the method and the URL's path). false: not one (err).
static inline bool parse_curl (Request &r, const char *cmd, Str &err)
{
	Vec<Str> a; shell_words (a, cmd);
	if (!a.size () || !(s_eq (a[0].c (), "curl") || ends_with (a[0].c (), "/curl") || s_eqi (a[0].c (), "curl.exe")))
	{ err = "It does not start with curl."; return false; }
	Str method, data; bool get = false, dataSeen = false, urlencode = false;
	KVList formRows;
	for (int i = 1; i < a.size (); i++)
	{
		const char *w = a[i].c ();
		const char *next = i + 1 < a.size () ? a[i + 1].c () : 0;
		#define ARG(x) (s_eq (w, x))
		if (ARG ("-X") || ARG ("--request")) { if (next) method = next; i++; }
		else if (s_starts (w, "-X") && w[2]) method = w + 2;
		else if (ARG ("-H") || ARG ("--header"))
		{
			if (!next) break;
			const char *c = strchr (next, ':');
			if (c) { KV kv; s_trim (kv.key, next, (int) (c - next)); s_trim (kv.value, c + 1); r.headers.push (kv); }
			i++;
		}
		else if (ARG ("-d") || ARG ("--data") || ARG ("--data-raw") || ARG ("--data-binary") || ARG ("--data-ascii") || ARG ("--data-urlencode"))
		{
			if (!next) break;
			if (dataSeen) data.add ('&');
			if (ARG ("--data-urlencode"))
			{
				const char *eq = strchr (next, '=');
				if (eq) { data.add (next, (int) (eq - next) + 1); url_encode (data, eq + 1, true); } else url_encode (data, next, true);
				urlencode = true;
			}
			else data.add (next);
			dataSeen = true; i++;
		}
		else if (ARG ("-F") || ARG ("--form") || ARG ("--form-string"))
		{
			if (!next) break;
			const char *eq = strchr (next, '=');
			if (eq)
			{
				KV kv; kv.key.set (next, (int) (eq - next));
				const char *v = eq + 1;
				if (*v == '@' && !ARG ("--form-string")) { kv.file = true; v++; }
				Str val (v);
				const char *semi = strchr (val.c (), ';'); if (semi && kv.file) val.truncate ((int) (semi - val.c ()));
				if (val.len () >= 2 && val.c ()[0] == '"' && val.c ()[val.len () - 1] == '"') { Str t; t.set (val.c () + 1, val.len () - 2); val = t; }
				kv.value = val;
				formRows.push (kv);
			}
			i++;
		}
		else if (ARG ("-u") || ARG ("--user"))
		{
			if (!next) break;
			const char *c = strchr (next, ':');
			r.auth.type = AUTH_BASIC;
			if (c) { r.auth.user.set (next, (int) (c - next)); r.auth.pass = c + 1; } else r.auth.user = next;
			i++;
		}
		else if (ARG ("-A") || ARG ("--user-agent")) { if (next) r.headers.push (KV ("User-Agent", next)); i++; }
		else if (ARG ("-b") || ARG ("--cookie")) { if (next) r.headers.push (KV ("Cookie", next)); i++; }
		else if (ARG ("-e") || ARG ("--referer")) { if (next) r.headers.push (KV ("Referer", next)); i++; }
		else if (ARG ("--url")) { if (next) r.url = next; i++; }
		else if (ARG ("-G") || ARG ("--get")) get = true;
		else if (ARG ("-I") || ARG ("--head")) method = "HEAD";
		else if (ARG ("-o") || ARG ("--output") || ARG ("-m") || ARG ("--max-time") || ARG ("--connect-timeout") || ARG ("-x") || ARG ("--proxy")
			 || ARG ("-w") || ARG ("--write-out") || ARG ("--retry") || ARG ("-c") || ARG ("--cookie-jar") || ARG ("-E") || ARG ("--cert")) i++;
		else if (w[0] == '-') {}				// (-L, -k, -s, --compressed...: nothing to keep)
		else if (r.url.empty ()) r.url = w;
		#undef ARG
	}
	if (r.url.empty ()) { err = "The command has no URL."; return false; }
	if (get && dataSeen) { r.url.add (strchr (r.url.c (), '?') ? '&' : '?'); r.url.add (data.c ()); dataSeen = false; }
	if (formRows.size ()) { r.bodyMode = BODY_FORM; r.form = formRows; if (method.empty ()) method = "POST"; }
	else if (dataSeen)
	{
		if (method.empty ()) method = "POST";
		const char *ct = 0;
		for (int i = 0; i < r.headers.size (); i++) if (s_eqi (r.headers[i].key.c (), "Content-Type")) ct = r.headers[i].value.c ();
		if (!ct || s_startsi (ct, "application/x-www-form-urlencoded"))
		{
			// the fields, decoded, as Postman shows them
			r.bodyMode = BODY_URLENC;
			const char *p = data.c ();
			while (*p)
			{
				const char *e = p; while (*e && *e != '&') e++;
				const char *eq = p; while (eq < e && *eq != '=') eq++;
				KV kv; url_decode (kv.key, p, (int) (eq - p)); if (eq < e) url_decode (kv.value, eq + 1, (int) (e - eq - 1));
				r.urlenc.push (kv);
				if (!*e) break;
				p = e + 1;
			}
			for (int i = r.headers.size () - 1; i >= 0; i--) if (s_eqi (r.headers[i].key.c (), "Content-Type")) r.headers.remove (i);
			(void) urlencode;
		}
		else
		{
			r.bodyMode = BODY_RAW; r.raw = data;
			r.rawLang = s_findi (ct, (int) strlen (ct), "json") >= 0 ? RAW_JSON : s_findi (ct, (int) strlen (ct), "xml") >= 0 ? RAW_XML
				  : s_findi (ct, (int) strlen (ct), "html") >= 0 ? RAW_HTML : RAW_TEXT;
			// (Courier sets the type of its raw language itself: the header kept only when it says more)
			for (int i = r.headers.size () - 1; i >= 0; i--)
				if (s_eqi (r.headers[i].key.c (), "Content-Type") && r.headers[i].value.eq (RAW_CTYPE[r.rawLang])) r.headers.remove (i);
		}
	}
	r.method = method.empty () ? "GET" : method.c ();
	// the params from the URL
	const char *qm = strchr (r.url.c (), '?');
	if (qm)
		for (const char *p = qm + 1; *p; )
		{
			const char *e = p; while (*e && *e != '&' && *e != '#') e++;
			const char *eq = p; while (eq < e && *eq != '=') eq++;
			KV kv; kv.key.set (p, (int) (eq - p)); if (eq < e) kv.value.set (eq + 1, (int) (e - eq - 1));
			if (!kv.key.empty ()) r.params.push (kv);
			if (*e != '&') break;
			p = e + 1;
		}
	// a name: the URL's path
	Url u;
	r.name.clear ();
	if (u.parse (r.url.c ()))
	{
		const char *path = u.path.c (); const char *q = strchr (path, '?');
		Str pp; pp.set (path, q ? (int) (q - path) : (int) strlen (path));
		r.name.add (u.host.c ()); if (!pp.eq ("/")) r.name.add (pp.c ());
	}
	else r.name = r.url;
	return true;
}

// ---- formatting ------------------------------------------------------------------------------------------------------
// JSON re-indented (2 spaces), its tokens as they were (the numbers' text, the keys' order). false: not JSON.
static inline bool pretty_json (Str &out, const char *s, int n)
{
	int i = 0;
	while (i < n && is_space (s[i])) i++;
	if (i >= n || (s[i] != '{' && s[i] != '[')) return false;
	int depth = 0;
	out.clear ();
	struct L { static void nl (Str &o, int d) { o.add ('\n'); for (int k = 0; k < d; k++) o.add ("  "); } };
	for (; i < n; i++)
	{
		char c = s[i];
		if (c == '"')
		{
			int e = i + 1;
			while (e < n && s[e] != '"') { if (s[e] == '\\') e++; e++; }
			if (e >= n) return false;
			out.add (s + i, e - i + 1);
			i = e;
		}
		else if (c == '{' || c == '[')
		{
			// an empty one stays {} / []
			int j = i + 1; while (j < n && is_space (s[j])) j++;
			if (j < n && ((c == '{' && s[j] == '}') || (c == '[' && s[j] == ']'))) { out.add (c); out.add (s[j]); i = j; continue; }
			out.add (c); depth++; L::nl (out, depth);
		}
		else if (c == '}' || c == ']') { depth--; if (depth < 0) return false; L::nl (out, depth); out.add (c); }
		else if (c == ',') { out.add (','); L::nl (out, depth); }
		else if (c == ':') out.add (": ");
		else if (is_space (c)) continue;
		else out.add (c);
	}
	if (depth != 0) return false;
	out.add ('\n');
	return true;
}
// XML re-indented: a tag a line (text kept inline between an opening and its closing tag). false: not XML.
static inline bool pretty_xml (Str &out, const char *s, int n)
{
	int i = 0;
	while (i < n && is_space (s[i])) i++;
	if (i >= n || s[i] != '<') return false;
	int depth = 0;
	out.clear ();
	bool lineStart = true;
	while (i < n)
	{
		if (s[i] == '<')
		{
			int e = i + 1;
			bool comment = i + 3 < n && s[i + 1] == '!' && s[i + 2] == '-' && s[i + 3] == '-';
			if (comment) { while (e + 2 < n && !(s[e] == '-' && s[e + 1] == '-' && s[e + 2] == '>')) e++; e += 3; }
			else { char q = 0; while (e < n && (q || s[e] != '>')) { if (!q && (s[e] == '"' || s[e] == '\'')) q = s[e]; else if (q == s[e]) q = 0; e++; } e++; }
			if (e > n) e = n;
			bool closing = s[i + 1] == '/', selfClose = e >= 2 && s[e - 2] == '/', decl = s[i + 1] == '?' || s[i + 1] == '!';
			if (closing) depth = imax (0, depth - 1);
			if (!lineStart) out.add ('\n');
			for (int k = 0; k < depth; k++) out.add ("  ");
			out.add (s + i, e - i);
			lineStart = false;
			if (!closing && !selfClose && !decl)
			{
				depth++;
				// <a>text</a> on one line
				int t = e; while (t < n && s[t] != '<') t++;
				if (t + 1 < n && s[t + 1] == '/')
				{
					Str txt; s_trim (txt, s + e, t - e);
					int ce = t; while (ce < n && s[ce] != '>') ce++;
					out.add (txt.c ()); out.add (s + t, imin (n, ce + 1) - t);
					depth--;
					i = imin (n, ce + 1);
					continue;
				}
			}
			i = e;
		}
		else
		{
			int e = i; while (e < n && s[e] != '<') e++;
			Str txt; s_trim (txt, s + i, e - i);
			if (!txt.empty ()) { out.add ('\n'); for (int k = 0; k < depth; k++) out.add ("  "); out.add (txt.c ()); }
			i = e;
		}
	}
	out.add ('\n');
	return true;
}
// An HTML page as text (a preview): the tags dropped, blocks as lines, the entities decoded.
static inline void html_text (Str &out, const char *s, int n)
{
	out.clear ();
	bool skip = false; int blank = 0;
	for (int i = 0; i < n; )
	{
		if (s[i] == '<')
		{
			int e = i + 1; while (e < n && s[e] != '>') e++;
			Str tag; int a = i + 1; if (a < n && s[a] == '/') a++;
			int b = a; while (b < e && ((s[b] >= 'a' && s[b] <= 'z') || (s[b] >= 'A' && s[b] <= 'Z') || (s[b] >= '0' && s[b] <= '9'))) b++;
			tag.set (s + a, b - a);
			for (int k = 0; k < tag.len (); k++) tag.data ()[k] = lc (tag.c ()[k]);
			if (tag.eq ("script") || tag.eq ("style") || tag.eq ("head")) skip = s[i + 1] != '/';
			static const char *const BLOCK[] = { "p", "div", "br", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6", "title", "section", "article", "header", "footer", "table", "ul", "ol", "pre", "hr", 0 };
			for (int k = 0; BLOCK[k]; k++) if (tag.eq (BLOCK[k])) { if (out.len () && out.c ()[out.len () - 1] != '\n') out.add ('\n'); else if (blank < 1 && out.len ()) { out.add ('\n'); blank++; } }
			if (tag.eq ("li") && s[i + 1] != '/') out.add ("  - ");
			i = imin (n, e + 1);
			continue;
		}
		if (skip) { i++; continue; }
		if (s[i] == '&')
		{
			static const struct { const char *e; const char *t; } ENT[] = { { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" },
				{ "&#39;", "'" }, { "&apos;", "'" }, { "&nbsp;", " " }, { "&copy;", "\xC2\xA9" }, { "&eacute;", "\xC3\xA9" }, { "&egrave;", "\xC3\xA8" }, { 0, 0 } };
			bool done = false;
			for (int k = 0; ENT[k].e; k++) { int l = (int) strlen (ENT[k].e); if (i + l <= n && s_eqin (s + i, ENT[k].e, l)) { out.add (ENT[k].t); i += l; done = true; break; } }
			if (done) continue;
		}
		char c = s[i];
		if (is_space (c))
		{
			if (out.len () && !is_space (out.c ()[out.len () - 1])) out.add (' ');
			i++; continue;
		}
		out.add (c); blank = 0; i++;
	}
}

} // namespace cr

#endif
