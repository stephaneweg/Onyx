//
// mail/mime.h -- a message read and written (RFC 5322, MIME RFC 2045-2049, RFC 2231's parameters): the headers
// (unfolded, their encoded words decoded), the parts' tree (multipart/*, message/rfc822), a part's body decoded
// (base64, quoted-printable) and its text in UTF-8, the text to show (text/plain or text/html), the attachments, the
// HTML's pictures (cid:); a message made (text + HTML as multipart/alternative, the attachments base64, UTF-8 headers).
// Part of Onyx's mail (docs/mail/README.md).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_MIME_H
#define ONYX_MAIL_MIME_H

#include "mail/util.h"
#include <ctype.h>

namespace mail {

// ---- headers ------------------------------------------------------------------------------------------------------------
// the header block's end in s (the blank line), its length; the body starts after it
static int header_end (const char *s, int n, int *bodyAt)
{
	for (int i = 0; i < n; i++)
	{
		if (s[i] == '\n' && i + 1 < n && s[i + 1] == '\n') { *bodyAt = i + 2; return i + 1; }
		if (s[i] == '\n' && i + 2 < n && s[i + 1] == '\r' && s[i + 2] == '\n') { *bodyAt = i + 3; return i + 1; }
	}
	*bodyAt = n; return n;
}
// a field's raw value (unfolded: its CRLF + space joined), the first one named so -> true when there
static bool header_get (const char *h, int n, const char *name, Buf &out)
{
	out.clear ();
	int nl = (int) strlen (name);
	for (int i = 0; i < n; )
	{
		int e = i; while (e < n && h[e] != '\n') e++;
		if (e - i > nl && ieqn (h + i, name, nl) && h[i + nl] == ':')
		{
			int p = i + nl + 1;
			for (;;)
			{
				while (p < e && (h[p] == ' ' || h[p] == '\t') && out.n == 0) p++;
				int q = e; if (q > p && h[q - 1] == '\r') q--;
				out.add (h + p, q - p);
				if (e + 1 < n && (h[e + 1] == ' ' || h[e + 1] == '\t')) { p = e + 1; e = p; while (e < n && h[e] != '\n') e++; while (p < e && (h[p] == ' ' || h[p] == '\t')) p++; out.addc (' '); continue; }
				break;
			}
			return true;
		}
		i = e + 1;
	}
	return false;
}
// a parameter of a structured field (Content-Type: text/plain; charset="utf-8"), RFC 2231's name*= and name*0*=
// joined and decoded -> UTF-8
static bool header_param (const char *v, const char *name, Buf &out)
{
	out.clear ();
	int nl = (int) strlen (name);
	Buf ext; char cs[32] = ""; bool found = false, extended = false;
	const char *p = strchr (v, ';');
	while (p)
	{
		p++; while (*p == ' ' || *p == '\t') p++;
		const char *k = p; while (*p && *p != '=' && *p != ';') p++;
		int kn = (int) (p - k); while (kn && (k[kn - 1] == ' ' || k[kn - 1] == '\t')) kn--;
		Buf val;
		if (*p == '=')
		{
			p++; while (*p == ' ') p++;
			if (*p == '"') { p++; while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; val.addc (*p++); } if (*p) p++; }
			else { while (*p && *p != ';' && *p != ' ' && *p != '\t') val.addc (*p++); }
		}
		if (kn >= nl && ieqn (k, name, nl))
		{
			const char *rest = k + nl; int rn = kn - nl;
			if (rn == 0) { if (!found && !extended) { out.add (val.c (), val.n); found = true; } }
			else if (*rest == '*')
			{	// name* (one piece, encoded), name*0 / name*0* / name*1* ... (in order, as they almost always come)
				bool enc = rest[rn - 1] == '*';
				const char *s = val.c ();
				bool first = rn == 1 || (rn >= 2 && rest[1] == '0');
				if (enc && first)
				{	// charset'lang'value
					const char *q1 = strchr (s, '\''); const char *q2 = q1 ? strchr (q1 + 1, '\'') : 0;
					if (q1 && q2) { int cn = (int) (q1 - s); if (cn > 31) cn = 31; memcpy (cs, s, cn); cs[cn] = 0; s = q2 + 1; }
				}
				if (enc) { for (; *s; s++) { if (*s == '%' && hexv (s[1]) >= 0 && hexv (s[2]) >= 0) { ext.addc ((char) (hexv (s[1]) * 16 + hexv (s[2]))); s += 2; } else ext.addc (*s); } }
				else ext.add (s);
				extended = true;
			}
		}
		p = strchr (p, ';');
	}
	if (extended) { out.clear (); to_utf8 (out, ext.c (), ext.n, cs[0] ? cs : "utf-8"); return true; }
	if (found) { Buf d; decode_header (d, out.c ()); out.clear (); out.add (d.c (), d.n); }	// (some put encoded words there)
	return found;
}
// a structured field's first word, lower case ("Text/HTML; ..." -> "text/html")
static void header_word (const char *v, char *out, int cap)
{
	while (*v == ' ') v++;
	int i = 0; for (; *v && *v != ';' && *v != ' ' && *v != '\t' && i < cap - 1; v++) out[i++] = (char) lc (*v);
	out[i] = 0;
}

// ---- the parts' tree -------------------------------------------------------------------------------------------------------
struct Part
{
	char type[48], sub[48];		// "text", "plain"
	char charset[32], enc[24];	// "utf-8", "base64"
	char name[200];			// the file's name (UTF-8): filename or name
	char cid[160];			// Content-ID without its <>
	bool attachment, inlineDisp;
	const char *hdr; int hn;	// its headers in the source
	const char *body; int bn;	// its body (encoded) in the source
	int kid, nkid;			// its parts (Mime::parts[kid .. kid + nkid - 1])
	int depth;
};
struct Mime
{
	const char *src; int n;
	Part *parts; int np, cap;
	Mime () : src (0), n (0), parts (0), np (0), cap (0) {}
	~Mime () { free (parts); }
	Mime (const Mime &) = delete;
	Mime &operator= (const Mime &) = delete;

	// the message parsed (s stays the caller's while this is used)
	void parse (const char *s, int len)
	{
		src = s; n = len; np = 0;
		int root = add ();
		fill (root, s, len, 0, "text", "plain");
	}
	int add () { if (np == cap) { cap = cap ? cap * 2 : 16; parts = (Part *) realloc (parts, sizeof (Part) * cap); } memset (&parts[np], 0, sizeof (Part)); return np++; }
	const Part &root () const { return parts[0]; }

	void fill (int id, const char *s, int len, int depth, const char *defType, const char *defSub)
	{
		int bodyAt; int hn = header_end (s, len, &bodyAt);
		// (a part without headers: it starts with its blank line)
		if (len && (s[0] == '\n' || (s[0] == '\r' && len > 1 && s[1] == '\n'))) { hn = 0; bodyAt = s[0] == '\n' ? 1 : 2; }
		Part *P = &parts[id];
		P->hdr = s; P->hn = hn; P->body = s + bodyAt; P->bn = len - bodyAt; P->depth = depth;
		if (P->bn < 0) P->bn = 0;
		Buf v, prm;
		char ct[96] = "";
		if (header_get (s, hn, "Content-Type", v)) header_word (v.c (), ct, sizeof ct);
		char *sl = strchr (ct, '/');
		if (sl && sl[1]) { *sl = 0; scpy (P->type, ct, sizeof P->type); scpy (P->sub, sl + 1, sizeof P->sub); }
		else { scpy (P->type, defType, sizeof P->type); scpy (P->sub, defSub, sizeof P->sub); }
		if (header_param (v.c (), "charset", prm)) { char t[32]; header_word (prm.c (), t, sizeof t); scpy (P->charset, t, sizeof P->charset); }
		if (header_param (v.c (), "name", prm)) scpy (P->name, prm.c (), sizeof P->name);
		Buf boundary; bool multi = ieq (P->type, "multipart") && header_param (v.c (), "boundary", boundary) && boundary.n;
		if (header_get (s, hn, "Content-Transfer-Encoding", v)) header_word (v.c (), P->enc, sizeof P->enc);
		if (header_get (s, hn, "Content-Disposition", v))
		{
			char d[24]; header_word (v.c (), d, sizeof d);
			P->attachment = ieq (d, "attachment"); P->inlineDisp = ieq (d, "inline");
			if (header_param (v.c (), "filename", prm)) scpy (P->name, prm.c (), sizeof P->name);
		}
		if (header_get (s, hn, "Content-ID", v))
		{
			const char *c = v.c (); while (*c == ' ' || *c == '<') c++;
			scpy (P->cid, c, sizeof P->cid); char *e = strchr (P->cid, '>'); if (e) *e = 0;
		}
		if (multi && depth < 12)
		{
			// the parts between "--boundary" lines, up to "--boundary--"
			const char *b = P->body, *end = P->body + P->bn;
			int bl = boundary.n;
			const char *starts[256]; int ends[256]; int k = 0;
			const char *cur = 0;
			for (const char *q = b; q < end; )
			{
				const char *le = (const char *) memchr (q, '\n', end - q); if (!le) le = end;
				if (le - q >= bl + 2 && q[0] == '-' && q[1] == '-' && !memcmp (q + 2, boundary.c (), bl))
				{
					const char *after = q + 2 + bl;
					bool last = after + 1 < le && after[0] == '-' && after[1] == '-';
					if (cur && k < 256)
					{	// the part ends before this line's CRLF
						const char *pe = q; if (pe > cur && pe[-1] == '\n') pe--; if (pe > cur && pe[-1] == '\r') pe--;
						starts[k] = cur; ends[k] = (int) (pe - cur); k++;
					}
					cur = le < end ? le + 1 : end;
					if (last) { cur = 0; break; }
				}
				q = le < end ? le + 1 : end;
			}
			if (cur && k < 256 && cur < end) { starts[k] = cur; ends[k] = (int) (end - cur); k++; }	// (no closing line)
			int first = np;
			for (int i = 0; i < k; i++) add ();
			parts[id].kid = first; parts[id].nkid = k;
			bool digest = ieq (parts[id].sub, "digest");
			for (int i = 0; i < k; i++) fill (first + i, starts[i], ends[i], depth + 1, digest ? "message" : "text", digest ? "rfc822" : "plain");
		}
		else if (ieq (P->type, "message") && ieq (P->sub, "rfc822") && depth < 12 && !P->enc[0])
		{	// a message forwarded as such: shown as an attachment (its name: its subject), its parts parsed too
			int k = add (); P = &parts[id];
			P->kid = k; P->nkid = 1;
			if (!P->name[0]) { Buf sub; int ba; int h2 = header_end (P->body, P->bn, &ba); if (header_get (P->body, h2, "Subject", sub)) { Buf d; decode_header (d, sub.c ()); Buf f; f.add (d.c ()); f.add (".eml"); scpy (P->name, f.c (), sizeof P->name); } }
			fill (k, P->body, P->bn, depth + 1, "text", "plain");
		}
	}

	// a part's body decoded (base64 / quoted-printable: the bytes)
	void decoded (const Part &p, Buf &out) const
	{
		if (ieq (p.enc, "base64")) b64_decode (out, p.body, p.bn);
		else if (ieq (p.enc, "quoted-printable")) qp_decode (out, p.body, p.bn);
		else out.add (p.body, p.bn);
	}
	// a text part's text in UTF-8, its lines ended by LF alone
	void text (const Part &p, Buf &out) const
	{
		Buf raw; decoded (p, raw);
		Buf u; to_utf8 (u, raw.c (), raw.n, p.charset[0] ? p.charset : "us-ascii");
		out.reserve (u.n);
		for (int i = 0; i < u.n; i++) if (!(u.p[i] == '\r' && i + 1 < u.n && u.p[i + 1] == '\n')) out.addc (u.p[i]);
	}
	// the part to show: in an alternative the richest one known (HTML) unless plain is asked; in mixed / related the
	// first text not an attachment -> its index (-1: none)
	int body_part (bool preferHtml = true) const { return pick (0, preferHtml); }
	int pick (int id, bool html) const
	{
		const Part &p = parts[id];
		if (p.nkid && ieq (p.type, "multipart"))
		{
			if (ieq (p.sub, "alternative"))
			{
				int best = -1;
				for (int i = 0; i < p.nkid; i++)
				{
					int r = pick (p.kid + i, html); if (r < 0) continue;
					bool isHtml = ieq (parts[r].sub, "html");
					if (best < 0 || (html ? isHtml : !isHtml)) best = r;
				}
				return best;
			}
			for (int i = 0; i < p.nkid; i++) { int r = pick (p.kid + i, html); if (r >= 0) return r; }
			return -1;
		}
		if (ieq (p.type, "message")) return -1;
		if (ieq (p.type, "text") && (ieq (p.sub, "plain") || ieq (p.sub, "html")) && !p.attachment) return id;
		return -1;
	}
	// an attachment: said so, or a leaf not shown and not one of the HTML's pictures
	bool is_attachment (int id, int shown) const
	{
		const Part &p = parts[id];
		if (id == shown || ieq (p.type, "multipart")) return false;
		if (p.depth > 0 && ieq (parts[parent (id)].type, "message")) return false;	// (inside a forwarded message)
		if (p.attachment) return true;
		if (ieq (p.type, "text") && (ieq (p.sub, "plain") || ieq (p.sub, "html")) && !p.name[0]) return false;	// (an alternative)
		if (p.cid[0] && ieq (p.type, "image") && !p.attachment) return false;		// (the HTML's picture)
		return p.bn > 0;
	}
	int parent (int id) const { for (int i = 0; i < np; i++) if (parts[i].nkid && id >= parts[i].kid && id < parts[i].kid + parts[i].nkid) return i; return 0; }
	// the attachments' indexes (at most max)
	int attachments (int *out, int max) const
	{
		int shown = body_part (), k = 0;
		for (int i = 1; i < np && k < max; i++) if (is_attachment (i, shown)) out[k++] = i;
		if (np == 1 && parts[0].attachment && k < max) out[k++] = 0;			// (the message is only a file)
		return k;
	}
	// a cid: picture's part (-1: none)
	int by_cid (const char *cid) const
	{
		for (int i = 0; i < np; i++) if (parts[i].cid[0] && ieq (parts[i].cid, cid)) return i;
		return -1;
	}
	// a header of the message as it came (an address list: parse_addrs () decodes its names itself)
	bool header_raw (const char *name, Buf &out) const { out.clear (); return np && header_get (parts[0].hdr, parts[0].hn, name, out); }
	// a decoded header of the message
	bool header (const char *name, Buf &out) const
	{
		Buf v; out.clear ();
		if (!np || !header_get (parts[0].hdr, parts[0].hn, name, v)) return false;
		decode_header (out, v.c ());
		return true;
	}
	// a part's size once decoded (about)
	long size_of (const Part &p) const { return ieq (p.enc, "base64") ? (long) p.bn * 3 / 4 * 76 / 78 : p.bn; }
};

// ---- writing ---------------------------------------------------------------------------------------------------------------
static void qp_encode (Buf &o, const char *s, int n)
{
	static const char *H = "0123456789ABCDEF";
	int col = 0;
	for (int i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c == '\r') continue;
		if (c == '\n')
		{	// a space at the line's end encoded
			if (o.n && (o.p[o.n - 1] == ' ' || o.p[o.n - 1] == '\t')) { char t = o.p[o.n - 1]; o.n--; o.addc ('='); o.addc (H[t >> 4]); o.addc (H[t & 15]); }
			o.add ("\r\n"); col = 0; continue;
		}
		bool lit = (c >= 33 && c <= 126 && c != '=') || c == ' ' || c == '\t';
		if (col == 0 && c == '.') lit = false;			// (a leading dot: safe from SMTP's dot-stuffing slips)
		int w = lit ? 1 : 3;
		if (col + w > 75) { o.add ("=\r\n"); col = 0; }
		if (lit) o.addc ((char) c); else { o.addc ('='); o.addc (H[c >> 4]); o.addc (H[c & 15]); }
		col += w;
	}
}
// "Name <a@b>, c@d" -> the header's value with the names encoded when not ASCII
static void encode_addrs (Buf &o, const char *list)
{
	Addr a[64]; int k = parse_addrs (list, a, 64);
	for (int i = 0; i < k; i++)
	{
		if (i) o.add (",\r\n ");
		if (a[i].name[0])
		{
			bool ascii = true, special = false;
			for (const char *p = a[i].name; *p; p++) { if ((unsigned char) *p >= 0x80) ascii = false; if (strchr ("()<>[]:;@\\,.\"", *p)) special = true; }
			if (!ascii) encode_header (o, a[i].name);
			else if (special) { o.addc ('"'); for (const char *p = a[i].name; *p; p++) { if (*p == '"' || *p == '\\') o.addc ('\\'); o.addc (*p); } o.addc ('"'); }
			else o.add (a[i].name);
			o.addf (" <%s>", a[i].email);
		}
		else o.add (a[i].email);
	}
}
struct Attachment { const char *name; const char *type; const void *data; int n; const char *cid; };	// (cid: an inline picture)
struct Outgoing
{
	const char *from, *to, *cc, *bcc, *subject, *inReplyTo, *references;
	const char *text, *html;			// UTF-8; html may be 0 (the text alone)
	const Attachment *att; int natt;
	long long date; int tzMin;			// the time (seconds since 1970, UTC) and the zone's offset
	char msgid[160];				// filled by build ()
};
static unsigned mime_rand ()
{
	static unsigned s; if (!s) s = 0x9E3779B9u ^ (unsigned) (size_t) &s;
	s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}
static void mime_seed (unsigned x) { for (unsigned i = 0; i < (x & 15) + 1; i++) mime_rand (); }
static void boundary (char *b, int cap) { snprintf (b, cap, "=_onyx_%08x%08x", mime_rand (), mime_rand ()); }
static void base64_part (Buf &o, const void *d, int n) { b64_encode (o, d, n, 76); if (o.n && o.p[o.n - 1] != '\n') o.add ("\r\n"); }
static bool needs_qp (const char *s)
{
	int col = 0;
	for (; *s; s++) { if ((unsigned char) *s >= 0x80 || *s == '=') return true; if (*s == '\n') col = 0; else if (++col > 900) return true; }
	return false;
}
static void text_part (Buf &o, const char *sub, const char *s)
{
	bool qp = needs_qp (s);
	o.addf ("Content-Type: text/%s; charset=\"utf-8\"\r\nContent-Transfer-Encoding: %s\r\n\r\n", sub, qp ? "quoted-printable" : "7bit");
	if (qp) qp_encode (o, s, (int) strlen (s));
	else for (const char *p = s; *p; p++) { if (*p == '\n' && (p == s || p[-1] != '\r')) o.addc ('\r'); o.addc (*p); }
	if (o.n < 2 || o.p[o.n - 1] != '\n') o.add ("\r\n");
}
static void attachment_part (Buf &o, const Attachment &a)
{
	// the name: plain and quoted when ASCII, else RFC 2231 (name*=utf-8''...)
	bool ascii = true; for (const char *p = a.name; *p; p++) if ((unsigned char) *p >= 0x80 || *p == '"' || *p == '\\') ascii = false;
	Buf v;
	if (ascii) v.addf ("=\"%s\"", a.name);
	else { v.add ("*=utf-8''"); for (const unsigned char *p = (const unsigned char *) a.name; *p; p++) { if (isalnum (*p) || strchr ("-._~", *p)) v.addc ((char) *p); else v.addf ("%%%02X", *p); } }
	o.addf ("Content-Type: %s; name%s\r\n", a.type ? a.type : "application/octet-stream", v.c ());
	o.addf ("Content-Disposition: %s; filename%s\r\n", a.cid ? "inline" : "attachment", v.c ());
	if (a.cid) o.addf ("Content-ID: <%s>\r\n", a.cid);
	o.add ("Content-Transfer-Encoding: base64\r\n\r\n");
	base64_part (o, a.data, a.n);
}
// the message, ready for SMTP and APPEND (CRLF); m.msgid set
static void build (Buf &o, Outgoing &m)
{
	const char *at = strchr (m.from ? m.from : "", '@'); char dom[100] = "onyx.local";
	if (at) { scpy (dom, at + 1, sizeof dom); char *e = strpbrk (dom, "> "); if (e) *e = 0; }
	mime_seed ((unsigned) m.date);
	snprintf (m.msgid, sizeof m.msgid, "%08x.%08x.%lld@%s", mime_rand (), mime_rand (), m.date, dom);
	char date[64]; format_date (date, sizeof date, m.date, m.tzMin);
	o.addf ("Date: %s\r\nFrom: ", date); encode_addrs (o, m.from); o.add ("\r\n");
	if (m.to && m.to[0]) { o.add ("To: "); encode_addrs (o, m.to); o.add ("\r\n"); }
	if (m.cc && m.cc[0]) { o.add ("Cc: "); encode_addrs (o, m.cc); o.add ("\r\n"); }
	o.add ("Subject: "); encode_header (o, m.subject ? m.subject : ""); o.add ("\r\n");
	o.addf ("Message-ID: <%s>\r\n", m.msgid);
	if (m.inReplyTo && m.inReplyTo[0]) o.addf ("In-Reply-To: %s\r\n", m.inReplyTo);
	if (m.references && m.references[0]) o.addf ("References: %s\r\n", m.references);
	o.add ("MIME-Version: 1.0\r\nUser-Agent: Onyx Mail\r\n");
	int inl = 0, files = 0; for (int i = 0; i < m.natt; i++) (m.att[i].cid ? inl : files)++;
	char bm[40], ba[40], br[40]; boundary (bm, sizeof bm); boundary (ba, sizeof ba); boundary (br, sizeof br);
	if (files) o.addf ("Content-Type: multipart/mixed; boundary=\"%s\"\r\n\r\nThis is a message in the MIME format.\r\n--%s\r\n", bm, bm);
	if (m.html)
	{
		o.addf ("Content-Type: multipart/alternative; boundary=\"%s\"\r\n\r\n", ba);
		if (!files) o.add ("This is a message in the MIME format.\r\n");
		o.addf ("--%s\r\n", ba);
		text_part (o, "plain", m.text ? m.text : "");
		o.addf ("--%s\r\n", ba);
		if (inl) { o.addf ("Content-Type: multipart/related; boundary=\"%s\"\r\n\r\n--%s\r\n", br, br); }
		text_part (o, "html", m.html);
		if (inl)
		{
			for (int i = 0; i < m.natt; i++) if (m.att[i].cid) { o.addf ("--%s\r\n", br); attachment_part (o, m.att[i]); }
			o.addf ("--%s--\r\n", br);
		}
		o.addf ("--%s--\r\n", ba);
	}
	else text_part (o, "plain", m.text ? m.text : "");
	if (files)
	{
		for (int i = 0; i < m.natt; i++) if (!m.att[i].cid) { o.addf ("--%s\r\n", bm); attachment_part (o, m.att[i]); }
		o.addf ("--%s--\r\n", bm);
	}
}
// the address alone of each of "Name <a@b>, c@d" (SMTP's RCPT) -> how many
static int addr_emails (const char *list, char out[][160], int max)
{
	Addr a[64]; int k = parse_addrs (list ? list : "", a, max < 64 ? max : 64);
	for (int i = 0; i < k; i++) scpy (out[i], a[i].email, 160);
	return k;
}

} // namespace mail

#endif
