//
// json.hpp -- a small JSON library for Onyx apps: a parser into a document whose every node and
// string lives in ONE arena (freeing the document is one call: no per-node free, no leak
// possible), typed accessors that tolerate missing fields, and a streaming writer.
// Header-only; include it in one translation unit or several (everything is inline).
//
//   json::Doc d;
//   if (!d.parse (text, len)) printf ("%s at %d:%d", d.error (), d.errLine (), d.errCol ());
//   const json::Value &p = d.root ()["Project"];
//   int bpm = p["Tempo"][0]["Bpm"].asInt (120);
//   for (const json::Value *t = p["Tracks"].first (); t; t = t->next) ... (*t)["Name"].asStr ()
//
//   json::Writer w (true);                        (true: pretty, 2 spaces; false: minified)
//   w.beginObj (); w.key ("bpm"); w.num (120); w.key ("notes"); w.beginArr (true) ... w.endObj ();
//   kapi_save_file (path, w.data (), w.size ());
//
// Allocation: JSON_MALLOC / JSON_FREE (default malloc / free: a newlib app; a freestanding app
// defines them as umm_malloc / umm_free before including). Numbers are doubles, unless
// JSON_NO_FLOAT (an integer-only app, -mgeneral-regs-only): then they are long long, a fraction
// dropped. Strings are UTF-8 (\uXXXX decoded, surrogate pairs too); json::latin1 () converts
// for the bitmap fonts.
//
// Parse flags: json::TOLERANT accepts what an LLM or a hand-edited file may give: a ``` fence
// around the document, text before the first { / [, trailing commas, // and /* */ comments,
// and an unescaped control character inside a string.
//
#ifndef _user_json_hpp
#define _user_json_hpp

#ifndef JSON_MALLOC
#include <stdlib.h>
#define JSON_MALLOC(n) malloc (n)
#define JSON_FREE(p)   free (p)
#endif

namespace json {

#ifdef JSON_NO_FLOAT
typedef long long num_t;
#else
typedef double num_t;
#endif

enum Type { NUL = 0, BOOL, NUM, STR, ARR, OBJ };
enum { STRICT = 0, TOLERANT = 1 };

inline unsigned slen (const char *s) { unsigned n = 0; while (s && s[n]) n++; return n; }
inline bool seq (const char *a, const char *b)
{
	if (!a || !b) return a == b;
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}
inline bool seqi (const char *a, const char *b)			// ASCII case-insensitive
{
	if (!a || !b) return a == b;
	for (;; a++, b++)
	{
		char x = *a, y = *b;
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return false;
		if (!x) return true;
	}
}

// ---- the arena -----------------------------------------------------------------------------------------
class Arena
{
	struct Chunk { Chunk *next; unsigned size, used; };
	Chunk *m_head;
	unsigned long m_total;
public:
	Arena () : m_head (0), m_total (0) {}
	~Arena () { clear (); }
	void clear ()
	{
		while (m_head) { Chunk *n = m_head->next; JSON_FREE (m_head); m_head = n; }
		m_total = 0;
	}
	unsigned long bytes () const { return m_total; }
	void *alloc (unsigned n)
	{
		n = (n + 7u) & ~7u;
		if (!m_head || m_head->used + n > m_head->size)
		{
			unsigned sz = n > 16384u ? n : 16384u;
			Chunk *c = (Chunk *) JSON_MALLOC (sizeof (Chunk) + sz);
			if (!c) return 0;
			c->next = m_head; c->size = sz; c->used = 0; m_head = c;
			m_total += sizeof (Chunk) + sz;
		}
		void *p = (char *) (m_head + 1) + m_head->used;
		m_head->used += n;
		return p;
	}
	char *strdup (const char *s, unsigned n)
	{
		char *d = (char *) alloc (n + 1);
		if (!d) return 0;
		for (unsigned i = 0; i < n; i++) d[i] = s[i];
		d[n] = 0;
		return d;
	}
private:
	Arena (const Arena &);
	Arena &operator= (const Arena &);
};

// ---- the values ----------------------------------------------------------------------------------------
struct Value
{
	unsigned char type;
	bool b;
	num_t n;
	const char *s; unsigned slen_;		// STR: the text (NUL-terminated), its length in bytes; NUM: its token
	const char *key;			// a member of an object: its name (else 0)
	Value *first_, *last_, *next;		// ARR / OBJ: the children; next: the following sibling
	unsigned count;				// ARR / OBJ: how many children

	static const Value &null () { static Value v; return v; }
	Value () : type (NUL), b (false), n (0), s (0), slen_ (0), key (0), first_ (0), last_ (0), next (0), count (0) {}

	bool isNull () const { return type == NUL; }
	bool isBool () const { return type == BOOL; }
	bool isNum () const { return type == NUM; }
	bool isStr () const { return type == STR; }
	bool isArr () const { return type == ARR; }
	bool isObj () const { return type == OBJ; }
	bool has (const char *k) const { return find (k) != 0; }
	unsigned size () const { return (type == ARR || type == OBJ) ? count : 0; }
	const Value *first () const { return (type == ARR || type == OBJ) ? first_ : 0; }

	const Value *find (const char *k) const
	{
		if (type != OBJ) return 0;
		for (const Value *c = first_; c; c = c->next) if (seq (c->key, k)) return c;
		for (const Value *c = first_; c; c = c->next) if (seqi (c->key, k)) return c;	// "bpm" == "Bpm"
		return 0;
	}
	const Value &operator[] (const char *k) const { const Value *v = find (k); return v ? *v : null (); }
	const Value &operator[] (unsigned i) const
	{
		if (type != ARR && type != OBJ) return null ();
		for (const Value *c = first_; c; c = c->next) if (i-- == 0) return *c;
		return null ();
	}
	const Value &operator[] (int i) const { return i < 0 ? null () : (*this)[(unsigned) i]; }

	// the typed reads: a missing field or another type gives the default; a number in a
	// string ("120") and a bool as 0 / 1 are accepted (hand-made files, LLM replies)
	num_t asNum (num_t def = 0) const
	{
		if (type == NUM) return n;
		if (type == BOOL) return b ? 1 : 0;
		if (type == STR) { num_t v; if (parseNumber (s, &v)) return v; }
		return def;
	}
	long long asLong (long long def = 0) const
	{
		if (type == NUM) return (long long) n;
		if (type == BOOL || type == STR) { num_t v = asNum ((num_t) def); return (long long) v; }
		return def;
	}
	int asInt (int def = 0) const { return (int) asLong (def); }
#ifndef JSON_NO_FLOAT
	double asDouble (double def = 0) const { return (double) asNum ((num_t) def); }
	float asFloat (float def = 0) const { return (float) asNum ((num_t) def); }
#endif
	bool asBool (bool def = false) const
	{
		if (type == BOOL) return b;
		if (type == NUM) return n != 0;
		if (type == STR) { if (seqi (s, "true")) return true; if (seqi (s, "false")) return false; }
		return def;
	}
	const char *asStr (const char *def = "") const { return type == STR ? s : def; }
	// an exact unsigned 64-bit integer (a bit mask written by .NET's UInt64): from the number's own
	// text, not through the double (which keeps only 53 bits)
	unsigned long long asU64 (unsigned long long def = 0) const
	{
		if ((type != NUM && type != STR) || !s) return type == NUM ? (unsigned long long) n : def;
		const char *p = s; unsigned long long v = 0; bool any = false;
		while (*p == ' ') p++;
		if (*p == '-') return def;
		while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned long long) (*p - '0'); p++; any = true; }
		return any ? v : def;
	}
	// copy the string into buf (cap bytes, always terminated); the length copied
	unsigned copyStr (char *buf, unsigned cap, const char *def = "") const
	{
		const char *src = asStr (def); unsigned i = 0;
		if (!cap) return 0;
		while (src[i] && i + 1 < cap) { buf[i] = src[i]; i++; }
		buf[i] = 0;
		return i;
	}

	// strtod-like, for asNum on strings and for the parser: [-]digits[.digits][e[+-]digits]
	static bool parseNumber (const char *p, num_t *out, const char **end = 0)
	{
		const char *q = p; bool neg = false;
		while (*q == ' ' || *q == '\t') q++;
		if (*q == '-') { neg = true; q++; } else if (*q == '+') q++;
		if (*q < '0' || *q > '9') { if (*q != '.' || q[1] < '0' || q[1] > '9') return false; }
#ifdef JSON_NO_FLOAT
		long long v = 0;
		while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; }
		if (*q == '.') { q++; while (*q >= '0' && *q <= '9') q++; }
		if (*q == 'e' || *q == 'E')
		{
			q++; bool en = false; int e = 0;
			if (*q == '-') { en = true; q++; } else if (*q == '+') q++;
			while (*q >= '0' && *q <= '9') { e = e * 10 + (*q - '0'); q++; }
			while (e-- > 0) { if (en) v /= 10; else v *= 10; }
		}
#else
		double v = 0;
		while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; }
		if (*q == '.')
		{
			q++; double f = 0.1;
			while (*q >= '0' && *q <= '9') { v += (*q - '0') * f; f *= 0.1; q++; }
		}
		if (*q == 'e' || *q == 'E')
		{
			const char *save = q;
			q++; bool en = false; int e = 0;
			if (*q == '-') { en = true; q++; } else if (*q == '+') q++;
			if (*q < '0' || *q > '9') q = save;
			else
			{
				while (*q >= '0' && *q <= '9') { if (e < 400) e = e * 10 + (*q - '0'); q++; }
				double m = 1, b10 = 10;
				while (e) { if (e & 1) m *= b10; b10 *= b10; e >>= 1; }
				v = en ? v / m : v * m;
			}
		}
#endif
		*out = neg ? -v : v;
		if (end) *end = q;
		return true;
	}
};

// ---- the document --------------------------------------------------------------------------------------
class Doc
{
	Arena m_arena;
	Value *m_root;
	char m_err[96];
	int m_line, m_col;
	// the parser's state
	const char *m_p, *m_end, *m_begin;
	int m_flags, m_depth;
public:
	Doc () : m_root (0), m_line (0), m_col (0), m_p (0), m_end (0), m_begin (0), m_flags (0), m_depth (0) { m_err[0] = 0; }
	void clear () { m_arena.clear (); m_root = 0; m_err[0] = 0; m_line = m_col = 0; }

	const Value &root () const { return m_root ? *m_root : Value::null (); }
	Value *rootPtr () { return m_root; }
	void setRoot (Value *v) { m_root = v; }
	const char *error () const { return m_err; }
	int errLine () const { return m_line; }
	int errCol () const { return m_col; }
	unsigned long bytes () const { return m_arena.bytes (); }

	bool parse (const char *text, unsigned long len, int flags = STRICT)
	{
		clear ();
		m_begin = m_p = text; m_end = text + len; m_flags = flags; m_depth = 0;
		if (len >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF) m_p += 3;	// a UTF-8 BOM
		if (flags & TOLERANT) skipPreamble ();
		ws ();
		Value *v = value ();
		if (!v) return false;
		ws ();
		if (m_p < m_end && !(m_flags & TOLERANT)) return fail ("text after the document");
		m_root = v;
		return true;
	}
	bool parse (const char *text) { return parse (text, slen (text), STRICT); }

	// ---- building (for the writer, or to edit a parsed document) ----
	Value *make (Type t)
	{
		Value *v = (Value *) m_arena.alloc (sizeof (Value));
		if (!v) return 0;
		*v = Value (); v->type = (unsigned char) t;
		return v;
	}
	Value *newObj () { return make (OBJ); }
	Value *newArr () { return make (ARR); }
	Value *newNull () { return make (NUL); }
	Value *newBool (bool b) { Value *v = make (BOOL); if (v) v->b = b; return v; }
	Value *newNum (num_t n) { Value *v = make (NUM); if (v) v->n = n; return v; }
	Value *newStr (const char *s) { return newStr (s, slen (s)); }
	Value *newStr (const char *s, unsigned n)
	{
		Value *v = make (STR); if (!v) return 0;
		v->s = m_arena.strdup (s, n); v->slen_ = n;
		return v->s ? v : 0;
	}
	void append (Value *parent, Value *child)
	{
		if (!parent || !child) return;
		child->next = 0;
		if (parent->last_) parent->last_->next = child; else parent->first_ = child;
		parent->last_ = child; parent->count++;
	}
	void set (Value *obj, const char *k, Value *child)
	{
		if (!obj || !child || obj->type != OBJ) return;
		child->key = m_arena.strdup (k, slen (k));
		append (obj, child);
	}
	Arena &arena () { return m_arena; }

private:
	Doc (const Doc &);
	Doc &operator= (const Doc &);

	bool fail (const char *msg)
	{
		unsigned i = 0;
		while (msg[i] && i + 1 < sizeof m_err) { m_err[i] = msg[i]; i++; }
		m_err[i] = 0;
		m_line = 1; m_col = 1;
		for (const char *q = m_begin; q < m_p && q < m_end; q++)
			if (*q == '\n') { m_line++; m_col = 1; } else m_col++;
		return false;
	}
	Value *failv (const char *msg) { fail (msg); return 0; }

	void skipPreamble ()
	{
		// a ```json fence, any text before the first { or [
		const char *q = m_p;
		while (q < m_end && *q != '{' && *q != '[') q++;
		if (q < m_end) m_p = q;
		// a closing fence: cut the end at the last } or ]
		const char *e = m_end;
		while (e > m_p && e[-1] != '}' && e[-1] != ']') e--;
		if (e > m_p) m_end = e;
	}
	void ws ()
	{
		for (;;)
		{
			while (m_p < m_end && (*m_p == ' ' || *m_p == '\t' || *m_p == '\n' || *m_p == '\r')) m_p++;
			if ((m_flags & TOLERANT) && m_p + 1 < m_end && m_p[0] == '/')
			{
				if (m_p[1] == '/') { while (m_p < m_end && *m_p != '\n') m_p++; continue; }
				if (m_p[1] == '*')
				{
					m_p += 2;
					while (m_p + 1 < m_end && !(m_p[0] == '*' && m_p[1] == '/')) m_p++;
					m_p = m_p + 2 <= m_end ? m_p + 2 : m_end;
					continue;
				}
			}
			return;
		}
	}
	bool lit (const char *w)
	{
		unsigned n = slen (w);
		if ((unsigned long) (m_end - m_p) < n) return false;
		for (unsigned i = 0; i < n; i++) if (m_p[i] != w[i]) return false;
		m_p += n;
		return true;
	}
	Value *value ()
	{
		if (m_p >= m_end) return failv ("unexpected end");
		char c = *m_p;
		if (c == '{') return object ();
		if (c == '[') return array ();
		if (c == '"') return string ();
		if (c == '-' || (c >= '0' && c <= '9')) return number ();
		if (lit ("true")) return newBool (true);
		if (lit ("false")) return newBool (false);
		if (lit ("null")) return newNull ();
		return failv ("unexpected character");
	}
	Value *number ()
	{
		num_t v; const char *e;
		// bound the scan to the buffer: copy the token (numbers are short)
		char tok[64]; unsigned n = 0;
		while (m_p + n < m_end && n < sizeof tok - 1)
		{
			char c = m_p[n];
			if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') { tok[n] = c; n++; }
			else break;
		}
		tok[n] = 0;
		if (!Value::parseNumber (tok, &v, &e) || e == tok) return failv ("bad number");
		m_p += e - tok;
		Value *nv = newNum (v);
		if (nv) { nv->s = m_arena.strdup (tok, (unsigned) (e - tok)); nv->slen_ = (unsigned) (e - tok); }	// the token: exact 64-bit reads
		return nv;
	}
	static int hex4 (const char *p)
	{
		int v = 0;
		for (int i = 0; i < 4; i++)
		{
			char c = p[i]; v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0';
			else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
			else return -1;
		}
		return v;
	}
	// a string token -> its unescaped UTF-8 in the arena (*len set); 0 on error
	char *stringText (unsigned *len)
	{
		m_p++;						// the opening quote
		// first pass: its end and an upper bound of its size
		const char *q = m_p;
		while (q < m_end && *q != '"') { if (*q == '\\') q++; q++; }
		if (q >= m_end) { fail ("unterminated string"); return 0; }
		char *out = (char *) m_arena.alloc ((unsigned) (q - m_p) + 1);
		if (!out) { fail ("out of memory"); return 0; }
		unsigned o = 0;
		while (m_p < q)
		{
			unsigned char c = (unsigned char) *m_p++;
			if (c == '\\')
			{
				char e = *m_p++;
				switch (e)
				{
				case '"': out[o++] = '"'; break;
				case '\\': out[o++] = '\\'; break;
				case '/': out[o++] = '/'; break;
				case 'b': out[o++] = '\b'; break;
				case 'f': out[o++] = '\f'; break;
				case 'n': out[o++] = '\n'; break;
				case 'r': out[o++] = '\r'; break;
				case 't': out[o++] = '\t'; break;
				case 'u':
				{
					if (q - m_p < 4) { fail ("bad \\u escape"); return 0; }
					long cp = hex4 (m_p); m_p += 4;
					if (cp < 0) { fail ("bad \\u escape"); return 0; }
					if (cp >= 0xD800 && cp < 0xDC00 && q - m_p >= 6 && m_p[0] == '\\' && m_p[1] == 'u')
					{
						long lo = hex4 (m_p + 2);
						if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); m_p += 6; }
					}
					// UTF-8 (a \uXXXX takes 6 source bytes; its UTF-8 at most 4: it fits)
					if (cp < 0x80) out[o++] = (char) cp;
					else if (cp < 0x800) { out[o++] = (char) (0xC0 | (cp >> 6)); out[o++] = (char) (0x80 | (cp & 63)); }
					else if (cp < 0x10000) { out[o++] = (char) (0xE0 | (cp >> 12)); out[o++] = (char) (0x80 | ((cp >> 6) & 63)); out[o++] = (char) (0x80 | (cp & 63)); }
					else { out[o++] = (char) (0xF0 | (cp >> 18)); out[o++] = (char) (0x80 | ((cp >> 12) & 63)); out[o++] = (char) (0x80 | ((cp >> 6) & 63)); out[o++] = (char) (0x80 | (cp & 63)); }
					break;
				}
				default:
					if (m_flags & TOLERANT) { out[o++] = e; break; }
					m_p--; fail ("bad escape"); return 0;
				}
			}
			else if (c < 0x20 && !(m_flags & TOLERANT)) { m_p--; fail ("control character in a string"); return 0; }
			else out[o++] = (char) c;
		}
		out[o] = 0;
		m_p = q + 1;
		*len = o;
		return out;
	}
	Value *string ()
	{
		unsigned n; char *s = stringText (&n);
		if (!s) return 0;
		Value *v = make (STR); if (!v) return failv ("out of memory");
		v->s = s; v->slen_ = n;
		return v;
	}
	Value *array ()
	{
		if (++m_depth > 200) return failv ("nested too deep");
		m_p++;
		Value *a = newArr (); if (!a) return failv ("out of memory");
		ws ();
		if (m_p < m_end && *m_p == ']') { m_p++; m_depth--; return a; }
		for (;;)
		{
			ws ();
			if ((m_flags & TOLERANT) && m_p < m_end && *m_p == ']') { m_p++; break; }	// a trailing comma
			Value *v = value (); if (!v) return 0;
			append (a, v);
			ws ();
			if (m_p >= m_end) return failv ("unterminated array");
			if (*m_p == ',') { m_p++; continue; }
			if (*m_p == ']') { m_p++; break; }
			return failv ("expected , or ]");
		}
		m_depth--;
		return a;
	}
	Value *object ()
	{
		if (++m_depth > 200) return failv ("nested too deep");
		m_p++;
		Value *o = newObj (); if (!o) return failv ("out of memory");
		ws ();
		if (m_p < m_end && *m_p == '}') { m_p++; m_depth--; return o; }
		for (;;)
		{
			ws ();
			if ((m_flags & TOLERANT) && m_p < m_end && *m_p == '}') { m_p++; break; }
			if (m_p >= m_end || *m_p != '"') return failv ("expected a member name");
			unsigned kl; char *k = stringText (&kl); if (!k) return 0;
			ws ();
			if (m_p >= m_end || *m_p != ':') return failv ("expected :");
			m_p++; ws ();
			Value *v = value (); if (!v) return 0;
			v->key = k;
			append (o, v);
			ws ();
			if (m_p >= m_end) return failv ("unterminated object");
			if (*m_p == ',') { m_p++; continue; }
			if (*m_p == '}') { m_p++; break; }
			return failv ("expected , or }");
		}
		m_depth--;
		return o;
	}
};

// ---- the writer ----------------------------------------------------------------------------------------
class Writer
{
	char *m_buf; unsigned long m_n, m_cap;
	bool m_pretty, m_oom;
	int m_depth;
	// per level: has a first element been written; is the container "inline" (on one line)
	unsigned char m_any[64], m_inl[64];
	bool m_afterKey;
public:
	explicit Writer (bool pretty = true) : m_buf (0), m_n (0), m_cap (0), m_pretty (pretty), m_oom (false), m_depth (0), m_afterKey (false) {}
	~Writer () { JSON_FREE (m_buf); }
	const char *data () const { return m_buf ? m_buf : ""; }
	unsigned long size () const { return m_n; }
	bool ok () const { return !m_oom; }
	void reset () { m_n = 0; m_depth = 0; m_oom = false; m_afterKey = false; if (m_buf) m_buf[0] = 0; }
	// hand the buffer over (the caller JSON_FREEs it)
	char *release () { char *b = m_buf; m_buf = 0; m_n = m_cap = 0; return b; }

	void raw (const char *s, unsigned long n)
	{
		if (m_oom) return;
		if (m_n + n + 1 > m_cap)
		{
			unsigned long nc = m_cap ? m_cap * 2 : 4096;
			while (nc < m_n + n + 1) nc *= 2;
			char *nb = (char *) JSON_MALLOC (nc);
			if (!nb) { m_oom = true; return; }
			for (unsigned long i = 0; i < m_n; i++) nb[i] = m_buf[i];
			JSON_FREE (m_buf); m_buf = nb; m_cap = nc;
		}
		for (unsigned long i = 0; i < n; i++) m_buf[m_n + i] = s[i];
		m_n += n; m_buf[m_n] = 0;
	}
	void raw (const char *s) { raw (s, slen (s)); }
	void ch (char c) { raw (&c, 1); }

	// inline: the container stays on one line (a note [48,0,24], a small point {"Beat":0,"Bpm":120})
	void beginObj (bool inl = false) { open ('{', inl); }
	void endObj () { close ('}'); }
	void beginArr (bool inl = false) { open ('[', inl); }
	void endArr () { close (']'); }
	void key (const char *k)
	{
		sep ();
		quoted (k);
		raw (m_pretty ? ": " : ":");
		m_afterKey = true;
	}
	void str (const char *s) { sep (); quoted (s ? s : ""); }
	void str (const char *s, unsigned n) { sep (); quoted (s, n); }
	void null () { sep (); raw ("null"); }
	void boolean (bool b) { sep (); raw (b ? "true" : "false"); }
	void integer (long long v)
	{
		sep ();
		char t[24]; int i = 0; bool neg = v < 0;
		unsigned long long u = neg ? (unsigned long long) (-(v + 1)) + 1 : (unsigned long long) v;
		do { t[i++] = (char) ('0' + u % 10); u /= 10; } while (u);
		if (neg) ch ('-');
		while (i) ch (t[--i]);
	}
#ifndef JSON_NO_FLOAT
	// a double: an integer when it is one, else up to `prec` decimals, trailing zeros dropped
	void num (double v, int prec = 6)
	{
		if (v != v) { null (); return; }		// NaN
		if (v > -1e15 && v < 1e15 && v == (double) (long long) v) { integer ((long long) v); return; }
		sep ();
		if (v < 0) { ch ('-'); v = -v; }
		if (v >= 1e15 || (v != 0 && v < 1e-6))
		{
			// scientific
			int e = 0;
			while (v >= 10) { v /= 10; e++; }
			while (v < 1) { v *= 10; e--; }
			digits (v, prec); ch ('e'); if (e < 0) { ch ('-'); e = -e; }
			char t[8]; int i = 0; do { t[i++] = (char) ('0' + e % 10); e /= 10; } while (e);
			while (i) ch (t[--i]);
			return;
		}
		digits (v, prec);
	}
#endif
	void u64 (unsigned long long u)
	{
		sep ();
		char t[24]; int i = 0;
		do { t[i++] = (char) ('0' + u % 10); u /= 10; } while (u);
		while (i) ch (t[--i]);
	}
	void num (long long v) { integer (v); }
	void num (int v) { integer (v); }
	void num (unsigned v) { integer (v); }
	void num (long v) { integer (v); }

	// write a whole parsed value (re-save what was read, keep unknown fields)
	void value (const Value &v)
	{
		switch (v.type)
		{
		case NUL: null (); break;
		case BOOL: boolean (v.b); break;
#ifdef JSON_NO_FLOAT
		case NUM: integer (v.n); break;
#else
		case NUM: num ((double) v.n); break;
#endif
		case STR: str (v.s, v.slen_); break;
		case ARR:
		{
			bool small = true;
			for (const Value *c = v.first_; c; c = c->next) if (c->type == ARR || c->type == OBJ) small = false;
			beginArr (small && v.count <= 16);
			for (const Value *c = v.first_; c; c = c->next) value (*c);
			endArr ();
			break;
		}
		case OBJ:
			beginObj ();
			for (const Value *c = v.first_; c; c = c->next) { key (c->key ? c->key : ""); value (*c); }
			endObj ();
			break;
		}
	}

private:
	Writer (const Writer &);
	Writer &operator= (const Writer &);
#ifndef JSON_NO_FLOAT
	void digits (double v, int prec)
	{
		double r = 0.5; for (int i = 0; i < prec; i++) r /= 10;
		v += r;
		unsigned long long ip = (unsigned long long) v;
		double fp = v - (double) ip;
		char t[24]; int i = 0;
		do { t[i++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
		while (i) ch (t[--i]);
		char f[24]; int n = 0;
		for (int k = 0; k < prec && k < 20; k++) { fp *= 10; int d = (int) fp; if (d > 9) d = 9; f[n++] = (char) ('0' + d); fp -= d; }
		while (n > 0 && f[n - 1] == '0') n--;
		if (n) { ch ('.'); raw (f, n); }
	}
#endif
	bool inl () const { return m_depth > 0 && m_depth <= 64 && m_inl[m_depth - 1]; }
	void newline ()
	{
		if (!m_pretty || inl ()) return;
		ch ('\n');
		for (int i = 0; i < m_depth; i++) raw ("  ", 2);
	}
	void sep ()
	{
		if (m_afterKey) { m_afterKey = false; return; }
		if (m_depth > 0 && m_depth <= 64)
		{
			if (m_any[m_depth - 1]) ch (',');
			m_any[m_depth - 1] = 1;
			newline ();
		}
	}
	void open (char c, bool inlineIt)
	{
		sep ();
		ch (c);
		if (m_depth < 64) { m_any[m_depth] = 0; m_inl[m_depth] = (unsigned char) (inlineIt || inl ()); }
		m_depth++;
	}
	void close (char c)
	{
		bool any = m_depth > 0 && m_depth <= 64 && m_any[m_depth - 1];
		bool wasInl = inl ();
		m_depth--;
		if (any && m_pretty && !wasInl) { ch ('\n'); for (int i = 0; i < m_depth; i++) raw ("  ", 2); }
		ch (c);
	}
	void quoted (const char *s) { quoted (s, slen (s)); }
	void quoted (const char *s, unsigned n)
	{
		ch ('"');
		unsigned start = 0;
		for (unsigned i = 0; i < n; i++)
		{
			unsigned char c = (unsigned char) s[i];
			const char *esc = 0; char u[8];
			if (c == '"') esc = "\\\"";
			else if (c == '\\') esc = "\\\\";
			else if (c == '\n') esc = "\\n";
			else if (c == '\r') esc = "\\r";
			else if (c == '\t') esc = "\\t";
			else if (c < 0x20)
			{
				u[0] = '\\'; u[1] = 'u'; u[2] = '0'; u[3] = '0';
				u[4] = "0123456789abcdef"[c >> 4]; u[5] = "0123456789abcdef"[c & 15]; u[6] = 0; esc = u;
			}
			if (esc) { raw (s + start, i - start); raw (esc); start = i + 1; }
		}
		raw (s + start, n - start);
		ch ('"');
	}
};

// UTF-8 -> Latin-1 (the bitmap fonts): a character above U+00FF becomes '?'; returns the length
inline unsigned latin1 (const char *in, char *out, unsigned cap)
{
	unsigned o = 0;
	if (!cap) return 0;
	const unsigned char *p = (const unsigned char *) in;
	while (*p && o + 1 < cap)
	{
		unsigned cp;
		if (*p < 0x80) cp = *p++;
		else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 31u) << 6) | (p[1] & 63u); p += 2; }
		else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { cp = 0x100; p += 3; }
		else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = 0x100; p += 4; }
		else { cp = '?'; p++; }
		out[o++] = cp <= 0xFF ? (char) cp : '?';
	}
	out[o] = 0;
	return o;
}

} // namespace json

#endif
