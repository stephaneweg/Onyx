//
// xml.h -- the XML the office files are made of: a pull reader (start tags with their attributes, end
// tags, text with its entities decoded; comments, declarations, CDATA handled; a name compared without
// its prefix, "x:row" = "row") and the escaping the writers need.
//
#ifndef _sheet_xml_h
#define _sheet_xml_h

#include "core.h"

namespace ss {

enum { X_EOF, X_START, X_END, X_TEXT };

struct XmlReader
{
	const char *p, *e;
	int ev;						// the current event
	const char *nm; int nml;			// X_START / X_END: the element's name (with its prefix)
	const char *at, *ate;				// X_START: its attributes' text
	bool empty;					// X_START of <a/>: an X_END follows by itself
	bool pendingEnd;
	Buf text;					// X_TEXT: decoded

	XmlReader (const char *d, int n) : p (d), e (d + n), ev (X_EOF), nm (0), nml (0), at (0), ate (0), empty (false), pendingEnd (false) {}

	static void decode (const char *s, const char *z, Buf &o)
	{
		o.clear ();
		while (s < z)
		{
			if (*s != '&') { o.put (*s++); continue; }
			const char *q = s + 1;
			while (q < z && *q != ';' && q - s < 12) q++;
			if (q >= z || *q != ';') { o.put (*s++); continue; }
			int l = (int) (q - s - 1);
			const char *en = s + 1;
			if (l == 2 && !memcmp (en, "lt", 2)) o.put ('<');
			else if (l == 2 && !memcmp (en, "gt", 2)) o.put ('>');
			else if (l == 3 && !memcmp (en, "amp", 3)) o.put ('&');
			else if (l == 4 && !memcmp (en, "quot", 4)) o.put ('"');
			else if (l == 4 && !memcmp (en, "apos", 4)) o.put ('\'');
			else if (l > 1 && en[0] == '#')
			{
				unsigned v = en[1] == 'x' || en[1] == 'X' ? (unsigned) strtoul (en + 2, 0, 16) : (unsigned) strtoul (en + 1, 0, 10);
				if (v) o.putu (v);
			}
			else o.putn (s, (int) (q - s + 1));
			s = q + 1;
		}
	}
	int next ()
	{
		if (pendingEnd) { pendingEnd = false; ev = X_END; at = ate = 0; return ev; }
		for (;;)
		{
			if (p >= e) return ev = X_EOF;
			if (*p != '<')
			{
				const char *s = p;
				while (p < e && *p != '<') p++;
				decode (s, p, text);
				return ev = X_TEXT;
			}
			if (p + 4 <= e && !memcmp (p, "<!--", 4)) { const char *q = p + 4; while (q + 3 <= e && memcmp (q, "-->", 3)) q++; p = q + 3 <= e ? q + 3 : e; continue; }
			if (p + 9 <= e && !memcmp (p, "<![CDATA[", 9))
			{
				const char *s = p + 9, *q = s;
				while (q + 3 <= e && memcmp (q, "]]>", 3)) q++;
				text.clear (); text.putn (s, (int) (q - s));
				p = q + 3 <= e ? q + 3 : e;
				return ev = X_TEXT;
			}
			if (p + 1 < e && (p[1] == '?' || p[1] == '!')) { while (p < e && *p != '>') p++; p++; continue; }
			if (p + 1 < e && p[1] == '/')
			{
				const char *s = p + 2, *q = s;
				while (q < e && *q != '>' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
				nm = s; nml = (int) (q - s);
				while (q < e && *q != '>') q++;
				p = q < e ? q + 1 : e;
				at = ate = 0;
				return ev = X_END;
			}
			const char *s = p + 1, *q = s;
			while (q < e && *q != '>' && *q != '/' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
			nm = s; nml = (int) (q - s);
			at = q;
			char quote = 0;
			while (q < e && (quote || (*q != '>')))
			{
				if (quote) { if (*q == quote) quote = 0; }
				else if (*q == '"' || *q == '\'') quote = *q;
				q++;
			}
			ate = q;
			empty = q > at && q[-1] == '/';
			if (empty) ate = q - 1;
			p = q < e ? q + 1 : e;
			if (empty) pendingEnd = true;
			return ev = X_START;
		}
	}
	// The element's name without its prefix is n?
	bool is (const char *n) const
	{
		const char *s = nm; int l = nml;
		for (int i = 0; i < l; i++) if (nm[i] == ':') { s = nm + i + 1; l = nml - i - 1; break; }
		int k = (int) strlen (n);
		return l == k && !memcmp (s, n, k);
	}
	bool isq (const char *qn) const { int k = (int) strlen (qn); return nml == k && !memcmp (nm, qn, k); }
	// An attribute's value (decoded) by its name -- with its prefix ("table:name"), or without one when
	// the name given has none and the attribute's is not "xmlns".
	bool attr (const char *name, Buf &o) const
	{
		if (ev != X_START || !at) return false;
		int k = (int) strlen (name);
		bool plain = !strchr (name, ':');
		const char *q = at;
		while (q < ate)
		{
			while (q < ate && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
			const char *an = q;
			while (q < ate && *q != '=' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
			int al = (int) (q - an);
			while (q < ate && *q != '=') q++;
			if (q >= ate) return false;
			q++;
			while (q < ate && *q != '"' && *q != '\'') q++;
			if (q >= ate) return false;
			char quote = *q++;
			const char *vs = q;
			while (q < ate && *q != quote) q++;
			const char *ve = q;
			if (q < ate) q++;
			bool match = al == k && !memcmp (an, name, k);
			if (!match && plain)
			{
				for (int i = 0; i < al; i++)
					if (an[i] == ':') { if (al - i - 1 == k && !memcmp (an + i + 1, name, k) && !(i == 5 && !memcmp (an, "xmlns", 5))) match = true; break; }
			}
			if (match) { decode (vs, ve, o); return true; }
		}
		return false;
	}
	int attr_int (const char *name, int def) const { Buf b; if (!attr (name, b)) return def; return atoi (b.str ()); }
	double attr_num (const char *name, double def) const { Buf b; if (!attr (name, b)) return def; return strtod (b.str (), 0); }
	bool attr_bool (const char *name, bool def) const { Buf b; if (!attr (name, b)) return def; return b.n && (b.b[0] == '1' || b.b[0] == 't' || b.b[0] == 'T'); }
	// Past the element just started (its whole content skipped).
	void skip ()
	{
		if (ev != X_START) return;
		int depth = 1;
		while (depth > 0) { int t = next (); if (t == X_EOF) return; if (t == X_START) depth++; else if (t == X_END) depth--; }
	}
};

// Text for XML (a node's text, or an attribute's value: quotes too); control characters dropped.
static void xml_esc (Buf &o, const char *s, int n = -1)
{
	if (n < 0) n = (int) strlen (s);
	for (int i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) s[i];
		switch (c)
		{
		case '&': o.puts ("&amp;"); break;
		case '<': o.puts ("&lt;"); break;
		case '>': o.puts ("&gt;"); break;
		case '"': o.puts ("&quot;"); break;
		default: if (c >= 32 || c == '\t' || c == '\n' || c == '\r') o.put ((char) c); break;
		}
	}
}

} // namespace ss

#endif
