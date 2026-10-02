//
// Apps/photos/exif.h -- what Photos knows of a picture without decoding it: its size (JPEG's SOF, PNG's IHDR, GIF's,
// BMP's, WebP's and PCX's headers), and in a JPEG the EXIF (TIFF) fields: when it was taken (DateTimeOriginal: the
// camera's local time), the camera (Make, Model), the exposure (f-number, exposure time, ISO, focal length), the
// orientation (1..8: how to turn the pixels), where the small preview the camera made is (IFD1). When there is no
// EXIF date, one in the file's name ("IMG_20260927_164200.jpg", "Screenshot 2026-09-27 at 16.42.00.png").
// Also: an EXIF block copied into a new JPEG (Save keeps the camera's facts, the orientation set to 1).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_exif_h
#define _photos_exif_h

#include "kapi.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace photos {

// days since 1970-01-01 of a civil date, and back (Howard Hinnant's)
static long long days_civil (long long y, int m, int d)
{
	y -= m <= 2;
	long long era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned) (y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long long) doe - 719468;
}
static void civil (long long z, int *y, int *m, int *d)
{
	z += 719468; long long era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned) (z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; long long yy = (long long) yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	*d = (int) (doy - (153 * mp + 2) / 5 + 1); *m = (int) (mp < 10 ? mp + 3 : mp - 9); *y = (int) (yy + (*m <= 2));
}
// a local date and time as seconds (the photos' times are the camera's local time: no zone)
static long long local_secs (int y, int mo, int d, int h, int mi, int s) { return days_civil (y, mo, d) * 86400LL + h * 3600 + mi * 60 + s; }
static bool valid_date (int y, int mo, int d, int h = 0, int mi = 0, int s = 0)
{ return y >= 1900 && y <= 2200 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h >= 0 && h < 24 && mi >= 0 && mi < 60 && s >= 0 && s < 61; }

struct PicInfo
{
	int w, h;			// pixels as stored (before the orientation); 0 unknown
	int orient;			// 1..8 (EXIF), 1 = as stored
	long long taken;		// local_secs, 0 unknown
	char camera[64];		// "Google Pixel 8"
	char expo[64];			// "f/1.7 · 1/640 s · ISO 50 · 6.9 mm"
	unsigned thumbOff, thumbLen;	// the camera's preview (a JPEG) in the file, 0 none
	unsigned exifOff, exifLen;	// the APP1 "Exif" segment (marker included) in the file, 0 none
};

// ---- the TIFF block of an EXIF ---------------------------------------------------------------------------------------------
struct Tiff
{
	const unsigned char *b; unsigned n; bool le;
	unsigned u16 (unsigned o) const { if (o + 2 > n) return 0; return le ? b[o] | b[o + 1] << 8 : b[o] << 8 | b[o + 1]; }
	unsigned u32 (unsigned o) const { if (o + 4 > n) return 0; return le ? b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (unsigned) b[o + 3] << 24 : (unsigned) b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]; }
	// an entry's value offset (inline when it fits in 4 bytes)
	unsigned voff (unsigned e) const
	{
		static const int SZ[13] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };
		unsigned t = u16 (e + 2), c = u32 (e + 4); unsigned sz = (t < 13 ? SZ[t] : 1) * c;
		return sz <= 4 ? e + 8 : u32 (e + 8);
	}
	unsigned num (unsigned e) const { unsigned t = u16 (e + 2); return t == 3 ? u16 (voff (e)) : u32 (voff (e)); }
	bool rational (unsigned e, unsigned *a, unsigned *c) const { unsigned o = voff (e); *a = u32 (o); *c = u32 (o + 4); return *c != 0; }
	void str (unsigned e, char *out, int cap) const
	{
		unsigned o = voff (e), c = u32 (e + 4); int k = 0;
		for (unsigned i = 0; i < c && o + i < n && k < cap - 1; i++) { char ch = (char) b[o + i]; if (!ch) break; out[k++] = ch; }
		while (k && out[k - 1] == ' ') k--;
		out[k] = 0;
	}
};

static void trim_model (char *make, char *model, char *out, int cap)
{
	// "Google" + "Pixel 8" -> "Google Pixel 8"; "Canon" + "Canon EOS 80D" -> "Canon EOS 80D"; "NIKON CORPORATION" -> "Nikon"
	char mk[64]; int k = 0;
	for (int i = 0; make[i] && make[i] != ' ' && k < 63; i++) mk[k++] = make[i];
	mk[k] = 0;
	if (k > 1) { bool upper = true; for (int i = 0; i < k; i++) if (mk[i] >= 'a' && mk[i] <= 'z') upper = false; if (upper && k > 3) for (int i = 1; i < k; i++) if (mk[i] >= 'A' && mk[i] <= 'Z') mk[i] += 32; }
	bool has = false; for (int i = 0; mk[0] && model[i]; i++) { int j = 0; while (mk[j] && (model[i + j] | 32) == (mk[j] | 32)) j++; if (!mk[j]) { has = true; break; } }
	if (!model[0]) snprintf (out, cap, "%s", mk);
	else if (has || !mk[0]) snprintf (out, cap, "%s", model);
	else snprintf (out, cap, "%s %s", mk, model);
}

static void parse_tiff (const unsigned char *b, unsigned n, PicInfo &pi)
{
	if (n < 8) return;
	Tiff t { b, n, b[0] == 'I' };
	if (t.u16 (2) != 42) return;
	unsigned ifd0 = t.u32 (4), exif = 0;
	char make[64] = "", model[64] = "";
	unsigned fA = 0, fB = 0, eA = 0, eB = 0, iso = 0, flA = 0, flB = 0;
	long long dt0 = 0, dtO = 0;
	auto date = [&] (unsigned e) -> long long {
		char s[24]; t.str (e, s, sizeof s); int y, mo, d, h, mi, se;
		if (sscanf (s, "%d:%d:%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) == 6 && valid_date (y, mo, d, h, mi, se)) return local_secs (y, mo, d, h, mi, se);
		return 0;
	};
	for (int pass = 0; pass < 3; pass++)
	{
		unsigned ifd = pass == 0 ? ifd0 : pass == 1 ? exif : 0;
		if (pass == 2) { unsigned c = t.u16 (ifd0); ifd = t.u32 (ifd0 + 2 + c * 12); }	// IFD1: the preview
		if (!ifd || ifd + 2 > n) continue;
		unsigned c = t.u16 (ifd); if (c > 400) continue;
		for (unsigned i = 0; i < c; i++)
		{
			unsigned e = ifd + 2 + i * 12; if (e + 12 > n) break;
			unsigned tag = t.u16 (e);
			if (pass == 0)
				switch (tag)
				{
				case 0x010F: t.str (e, make, sizeof make); break;
				case 0x0110: t.str (e, model, sizeof model); break;
				case 0x0112: pi.orient = (int) t.num (e); break;
				case 0x0132: dt0 = date (e); break;
				case 0x8769: exif = t.num (e); break;
				}
			else if (pass == 1)
				switch (tag)
				{
				case 0x9003: dtO = date (e); break;
				case 0x829D: t.rational (e, &fA, &fB); break;
				case 0x829A: t.rational (e, &eA, &eB); break;
				case 0x8827: iso = t.num (e); break;
				case 0x920A: t.rational (e, &flA, &flB); break;
				case 0xA002: if (!pi.w) pi.w = (int) t.num (e); break;
				case 0xA003: if (!pi.h) pi.h = (int) t.num (e); break;
				}
			else
			{
				if (tag == 0x0201) pi.thumbOff = t.num (e);
				if (tag == 0x0202) pi.thumbLen = t.num (e);
			}
		}
	}
	if (pi.orient < 1 || pi.orient > 8) pi.orient = 1;
	pi.taken = dtO ? dtO : dt0;
	trim_model (make, model, pi.camera, sizeof pi.camera);
	// "f/1.7 · 1/640 s · ISO 50 · 6.9 mm"
	char o[96] = ""; int k = 0;
	auto add = [&] (const char *s) { if (k) k += snprintf (o + k, sizeof o - k, " \xC2\xB7 "); k += snprintf (o + k, sizeof o - k, "%s", s); if (k > (int) sizeof o - 1) k = sizeof o - 1; };
	char s[32];
	if (fA && fB) { unsigned v = fA * 10 / fB; snprintf (s, sizeof s, v % 10 ? "f/%u.%u" : "f/%u", v / 10, v % 10); add (s); }
	if (eA && eB)
	{
		if (eA >= eB) { unsigned v = eA * 10 / eB; snprintf (s, sizeof s, v % 10 ? "%u.%u s" : "%u s", v / 10, v % 10); }
		else snprintf (s, sizeof s, "1/%u s", (eB + eA / 2) / eA);
		add (s);
	}
	if (iso) { snprintf (s, sizeof s, "ISO %u", iso); add (s); }
	if (flA && flB) { unsigned v = flA * 10 / flB; snprintf (s, sizeof s, v % 10 ? "%u.%u mm" : "%u mm", v / 10, v % 10); add (s); }
	snprintf (pi.expo, sizeof pi.expo, "%s", o);
}

// ---- a date in the file's name ----------------------------------------------------------------------------------------------
static long long date_in_name (const char *name)
{
	const char *b = strrchr (name, '/'); b = b ? b + 1 : name;
	for (const char *p = b; *p; p++)
	{
		if (p[0] != '1' && p[0] != '2') continue;
		if (p > b && p[-1] >= '0' && p[-1] <= '9') continue;
		int y, mo, d, h = 12, mi = 0, s = 0; const char *q = p;
		auto dig = [&] (int n, int *v) { int r = 0; for (int i = 0; i < n; i++) { if (q[i] < '0' || q[i] > '9') return false; r = r * 10 + q[i] - '0'; } *v = r; q += n; return true; };
		if (!dig (4, &y)) continue;
		bool sep = *q == '-' || *q == '_' || *q == '.'; if (sep) q++;
		if (!dig (2, &mo)) continue;
		if (sep) { if (*q != '-' && *q != '_' && *q != '.') continue; q++; }
		if (!dig (2, &d)) continue;
		if (*q >= '0' && *q <= '9') continue;
		if (!valid_date (y, mo, d) || y < 1990) continue;
		// a time after it: _164200, -16-42-00, " at 16.42.00", T164200
		const char *r = q;
		if (*r == '_' || *r == '-' || *r == ' ' || *r == 'T') r++;
		if (!strncmp (r, "at ", 3)) r += 3;
		q = r; int hh, mm, ss = 0;
		if (dig (2, &hh)) { if (*q == '.' || *q == '-' || *q == ':' || *q == '_') q++; if (dig (2, &mm)) { if (*q == '.' || *q == '-' || *q == ':') q++; if (!dig (2, &ss)) ss = 0; if (valid_date (y, mo, d, hh, mm, ss)) { h = hh; mi = mm; s = ss; } } }
		return local_secs (y, mo, d, h, mi, s);
	}
	return 0;
}

// ---- reading the header ------------------------------------------------------------------------------------------------------
static inline unsigned be16 (const unsigned char *p) { return p[0] << 8 | p[1]; }
static inline unsigned le16 (const unsigned char *p) { return p[0] | p[1] << 8; }
static inline unsigned le32 (const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned) p[3] << 24; }

static int read_at (void *f, unsigned long long at, unsigned char *b, int n)
{
	if (kapi_seek (f, at) != 0) return 0;
	int got = 0;
	while (got < n) { int r = kapi_read (f, b + got, n - got); if (r <= 0) break; got += r; }
	return got;
}

// the facts of a picture file; false: not one Photos can read
static bool pic_info (const char *path, PicInfo &pi)
{
	memset (&pi, 0, sizeof pi); pi.orient = 1;
	void *f = kapi_open (path);
	if (!f) return false;
	enum { HEAD = 64 * 1024 };
	unsigned char *b = (unsigned char *) malloc (HEAD + 16);
	int n = b ? read_at (f, 0, b, HEAD) : 0;
	bool ok = false;
	if (n >= 4 && b[0] == 0xFF && b[1] == 0xD8)
	{
		ok = true;
		unsigned long long o = 2;
		unsigned char seg[4];
		for (int guard = 0; guard < 200; guard++)
		{
			const unsigned char *s; unsigned char tmp[16];
			if (o + 12 <= (unsigned long long) n) s = b + o; else { if (read_at (f, o, tmp, 12) < 4) break; s = tmp; }
			if (s[0] != 0xFF) break;
			unsigned m = s[1];
			if (m == 0xFF) { o++; continue; }
			if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { o += 2; continue; }
			if (m == 0xD9 || m == 0xDA) break;
			unsigned len = be16 (s + 2);
			if ((m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) { pi.h = (int) be16 (s + 5); pi.w = (int) be16 (s + 7); break; }
			if (m == 0xE1 && !pi.exifOff && o + 10 <= (unsigned long long) n && !memcmp (b + o + 4, "Exif\0\0", 6))
			{
				pi.exifOff = (unsigned) o; pi.exifLen = len + 2;
				unsigned end = (unsigned) o + 2 + len; if (end > (unsigned) n) end = (unsigned) n;
				unsigned base = (unsigned) o + 10;
				int w0 = pi.w, h0 = pi.h;
				parse_tiff (b + base, end - base, pi);
				if (pi.thumbOff) pi.thumbOff += base;
				pi.w = w0; pi.h = h0;		// (the SOF's size is the truth)
			}
			o += 2 + len;
			(void) seg;
		}
	}
	else if (n >= 24 && !memcmp (b, "\x89PNG", 4)) { ok = true; pi.w = (int) (b[16] << 24 | b[17] << 16 | b[18] << 8 | b[19]); pi.h = (int) (b[20] << 24 | b[21] << 16 | b[22] << 8 | b[23]); }
	else if (n >= 10 && !memcmp (b, "GIF8", 4)) { ok = true; pi.w = (int) le16 (b + 6); pi.h = (int) le16 (b + 8); }
	else if (n >= 26 && b[0] == 'B' && b[1] == 'M') { ok = true; pi.w = (int) le32 (b + 18); int h = (int) le32 (b + 22); pi.h = h < 0 ? -h : h; }
	else if (n >= 30 && !memcmp (b, "RIFF", 4) && !memcmp (b + 8, "WEBP", 4))
	{
		ok = true;
		if (!memcmp (b + 12, "VP8 ", 4)) { pi.w = (int) (le16 (b + 26) & 0x3FFF); pi.h = (int) (le16 (b + 28) & 0x3FFF); }
		else if (!memcmp (b + 12, "VP8L", 4)) { unsigned v = le32 (b + 21); pi.w = (int) (v & 0x3FFF) + 1; pi.h = (int) ((v >> 14) & 0x3FFF) + 1; }
		else if (!memcmp (b + 12, "VP8X", 4)) { pi.w = (int) (b[24] | b[25] << 8 | b[26] << 16) + 1; pi.h = (int) (b[27] | b[28] << 8 | b[29] << 16) + 1; }
	}
	else if (n >= 128 && b[0] == 10) { ok = true; pi.w = (int) le16 (b + 8) - (int) le16 (b + 4) + 1; pi.h = (int) le16 (b + 10) - (int) le16 (b + 6) + 1; }
	free (b);
	kapi_close (f);
	if (ok && !pi.taken) pi.taken = date_in_name (path);
	return ok;
}

// the camera's own small preview (a JPEG's bytes, malloc'd), 0 none
static unsigned char *exif_thumb (const char *path, const PicInfo &pi, unsigned *len)
{
	if (!pi.thumbOff || pi.thumbLen < 100 || pi.thumbLen > 200000) return 0;
	void *f = kapi_open (path); if (!f) return 0;
	unsigned char *b = (unsigned char *) malloc (pi.thumbLen);
	int n = b ? read_at (f, pi.thumbOff, b, (int) pi.thumbLen) : 0;
	kapi_close (f);
	if (n != (int) pi.thumbLen || b[0] != 0xFF || b[1] != 0xD8) { free (b); return 0; }
	*len = pi.thumbLen; return b;
}

// a JPEG with the original's EXIF segment put back after its SOI, the orientation set to 1 and the preview's
// pointers dropped (the preview would show the old picture): -> new bytes (new[]), or 0 (then keep jpg as is)
static unsigned char *jpeg_with_exif (const unsigned char *jpg, unsigned n, const char *origPath, const PicInfo &pi, unsigned *outN)
{
	if (!pi.exifOff || pi.exifLen < 20 || pi.exifLen > 65537 || n < 4) return 0;
	void *f = kapi_open (origPath); if (!f) return 0;
	unsigned char *seg = new unsigned char[pi.exifLen];
	int got = read_at (f, pi.exifOff, seg, (int) pi.exifLen);
	kapi_close (f);
	if (got != (int) pi.exifLen || seg[0] != 0xFF || seg[1] != 0xE1) { delete[] seg; return 0; }
	// the orientation -> 1; IFD1's link -> 0 (no preview)
	unsigned char *tb = seg + 10; unsigned tn = pi.exifLen - 10;
	Tiff t { tb, tn, tb[0] == 'I' };
	unsigned ifd0 = t.u32 (4);
	if (ifd0 && ifd0 + 2 < tn)
	{
		unsigned c = t.u16 (ifd0);
		for (unsigned i = 0; i < c && ifd0 + 2 + i * 12 + 12 <= tn; i++)
		{
			unsigned e = ifd0 + 2 + i * 12;
			if (t.u16 (e) == 0x0112) { unsigned o = e + 8; if (t.le) { tb[o] = 1; tb[o + 1] = 0; } else { tb[o] = 0; tb[o + 1] = 1; } }
		}
		unsigned nx = ifd0 + 2 + c * 12;
		if (nx + 4 <= tn) tb[nx] = tb[nx + 1] = tb[nx + 2] = tb[nx + 3] = 0;
	}
	// the new JPEG may begin with its own APP0 (JFIF): the EXIF goes right after SOI
	unsigned char *o = new unsigned char[n + pi.exifLen];
	o[0] = 0xFF; o[1] = 0xD8;
	memcpy (o + 2, seg, pi.exifLen);
	unsigned skip = 2;
	if (n > 6 && jpg[2] == 0xFF && jpg[3] == 0xE0) skip = 4 + be16 (jpg + 4);	// (JFIF and EXIF do not go together)
	memcpy (o + 2 + pi.exifLen, jpg + skip, n - skip);
	*outN = 2 + pi.exifLen + n - skip;
	delete[] seg;
	return o;
}

} // namespace photos

#endif
