//
// pocketqi/text.h -- PocketQI: the small helpers of text, dates and files the rest shares. Text is UTF-8 everywhere
// but in Onyx BASIC, whose sources are Latin-1 and whose strings are code page 437 (basfont.h's tables): the
// skills' words cross over here. The dates come from the system's clock (kapi_get_datetime), counted in days since
// 1970-01-01 to step through a calendar.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef POCKETQI_TEXT_H
#define POCKETQI_TEXT_H

#include "appkit/appkit.h"
#include "basic/basfont.h"		// (the CP437 <-> Latin-1 tables)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace pq {

// ---- strings ----------------------------------------------------------------------------------------------------
static std::string trim (std::string s)
{
	while (!s.empty () && (s.back () == ' ' || s.back () == '\t' || s.back () == '\r' || s.back () == '\n')) s.pop_back ();
	size_t b = 0;
	while (b < s.size () && (s[b] == ' ' || s[b] == '\t')) b++;
	return s.substr (b);
}

static std::string lower (std::string s)
{
	for (char &c : s) if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
	return s;
}

static bool starts_ci (const char *s, const char *p)
{
	for (; *p; s++, p++)
	{
		char a = *s, b = *p;
		if (a >= 'a' && a <= 'z') a = (char) (a - 32);
		if (b >= 'a' && b <= 'z') b = (char) (b - 32);
		if (a != b) return false;
	}
	return true;
}

static std::string num (long v) { char b[24]; snprintf (b, sizeof b, "%ld", v); return b; }

// the words of s split by sep (empty fields kept)
static std::vector<std::string> split (const std::string &s, char sep)
{
	std::vector<std::string> v;
	size_t a = 0;
	for (size_t i = 0; i <= s.size (); i++)
		if (i == s.size () || s[i] == sep) { v.push_back (s.substr (a, i - a)); a = i + 1; }
	return v;
}

// ---- UTF-8 (the app, the LLM) <-> Latin-1 (BASIC sources, COMMAND$) <-> CP437 (inside the VM) ---------------------
static std::string to_latin1 (const std::string &s)
{
	std::string o;
	for (size_t i = 0; i < s.size (); i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c < 0x80) { o += (char) c; continue; }
		unsigned v = 0, k = 0;
		if ((c & 0xE0) == 0xC0) { v = c & 0x1F; k = 1; }
		else if ((c & 0xF0) == 0xE0) { v = c & 0x0F; k = 2; }
		else if ((c & 0xF8) == 0xF0) { v = c & 0x07; k = 3; }
		else { o += '?'; continue; }
		for (unsigned j = 0; j < k && i + 1 < s.size (); j++) v = (v << 6) | ((unsigned char) s[++i] & 0x3F);
		// (what an LLM often writes beyond Latin-1)
		if (v < 256) o += (char) v;
		else if (v == 0x153) o += "oe";
		else if (v == 0x152) o += "OE";
		else if (v == 0x2018 || v == 0x2019) o += '\'';
		else if (v == 0x201C || v == 0x201D) o += '"';
		else if (v == 0x2026) o += "...";
		else if (v == 0x2013 || v == 0x2014) o += '-';
		else if (v == 0x20AC) o += "euros";
		else o += '?';
	}
	return o;
}

static std::string from_latin1 (const std::string &s)
{
	std::string o;
	for (unsigned char c : s) { if (c < 0x80) o += (char) c; else { o += (char) (0xC0 | (c >> 6)); o += (char) (0x80 | (c & 0x3F)); } }
	return o;
}

static std::string to_cp437 (const std::string &utf8)
{
	std::string l = to_latin1 (utf8), o;
	for (unsigned char c : l) o += (char) basLatin1To437[c];
	return o;
}

static std::string from_cp437 (const std::string &s)
{
	std::string l;
	for (unsigned char c : s) l += (char) bas437ToLatin1[c];
	return from_latin1 (l);
}

// A UTF-8 file (the memory, written by the app) read by a skill: in CP437 as the rest of the VM -- only when it is
// valid UTF-8 with at least one accent (a file a skill wrote is CP437 already: "é" = 0x82, a lone continuation byte).
static std::string utf8_file_to_cp437 (const std::string &s)
{
	bool multi = false;
	for (size_t i = 0; i < s.size (); i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c < 0x80) continue;
		int n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		if (n < 0) return s;
		for (int k = 1; k <= n; k++)
			if (i + k >= s.size () || ((unsigned char) s[i + k] & 0xC0) != 0x80) return s;
		i += (size_t) n;
		multi = true;
	}
	return multi ? to_cp437 (s) : s;
}

static void url_encode (const char *in, std::string &out)
{
	static const char hex[] = "0123456789ABCDEF";
	out.clear ();
	for (const unsigned char *p = (const unsigned char *) in; *p; p++)
	{
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.')
			out += (char) *p;
		else { out += '%'; out += hex[*p >> 4]; out += hex[*p & 15]; }
	}
}

// s as a JSON string, its quotes included
static std::string json_str (const std::string &v)
{
	std::string r = "\"";
	for (char c : v)
	{
		if (c == '"' || c == '\\') { r += '\\'; r += c; }
		else if ((unsigned char) c < 0x20) r += ' ';
		else r += c;
	}
	return r + "\"";
}

// ---- files (AppKit's) -------------------------------------------------------------------------------------------
static bool read_file (const std::string &path, std::string &out)
{
	out.clear ();
	void *f = kapi_open (path.c_str ());
	if (!f) return false;
	char b[4096];
	int n;
	while ((n = kapi_read (f, b, sizeof b)) > 0) out.append (b, (size_t) n);
	kapi_close (f);
	return true;
}

static bool write_file (const std::string &path, const std::string &data)
{
	return kapi_save_file (path.c_str (), data.data (), (unsigned) data.size ()) == (int) data.size ();
}

static bool file_exists (const std::string &path)
{
	void *f = kapi_open (path.c_str ());
	if (f) { kapi_close (f); return true; }
	void *d = kapi_opendir (path.c_str ());
	if (d) { kapi_closedir (d); return true; }
	return false;
}

// every folder of the path made ("SD:/apps/pocketqi.app/etc")
static void make_dirs (const std::string &path)
{
	for (size_t i = 0; i <= path.size (); i++)
		if ((i == path.size () || path[i] == '/') && i > 0 && path[i - 1] != ':')
			kapi_mkdir (path.substr (0, i).c_str ());
}

// the subfolders of a folder, sorted
static std::vector<std::string> sub_dirs (const std::string &path)
{
	std::vector<std::string> v;
	void *d = kapi_opendir (path.c_str ());
	if (!d) return v;
	struct kapi_dirent e;
	while (kapi_readdir (d, &e) == 1)
		if (e.is_dir && e.name[0] != '.') v.push_back (e.name);
	kapi_closedir (d);
	for (size_t i = 1; i < v.size (); i++)
		for (size_t j = i; j > 0 && v[j] < v[j - 1]; j--) std::swap (v[j], v[j - 1]);
	return v;
}

// ---- dates ------------------------------------------------------------------------------------------------------
struct Date { int y, m, d, hh, mm, ss; };

static Date now ()
{
	Date t = { 2026, 1, 1, 0, 0, 0 };
	kapi_get_datetime (&t.y, &t.m, &t.d, &t.hh, &t.mm, &t.ss);
	if (t.y < 2000) t.y = 2026;
	if (t.m < 1 || t.m > 12) t.m = 1;
	if (t.d < 1 || t.d > 31) t.d = 1;
	return t;
}

static long day_number (int y, int m, int d)			// days since 1970-01-01
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

static void from_day_number (long z, int *y, int *m, int *d)
{
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	*d = (int) (doy - (153 * mp + 2) / 5 + 1);
	*m = (int) (mp < 10 ? mp + 3 : mp - 9);
	*y = (int) (yoe + era * 400 + (*m <= 2));
}

static int week_day (long day) { return (int) (((day % 7) + 7 + 4) % 7); }	// 0 Sunday (1970-01-01 was a Thursday)

static std::string iso (long day)
{
	int y, m, d;
	from_day_number (day, &y, &m, &d);
	char b[16];
	snprintf (b, sizeof b, "%04d-%02d-%02d", y, m, d);
	return b;
}

static long seconds_now () { Date t = now (); return day_number (t.y, t.m, t.d) * 86400L + t.hh * 3600L + t.mm * 60L + t.ss; }

} // namespace pq

#endif
