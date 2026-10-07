//
// filekit/tar.h -- the archives' engine: tar, tar.gz (.tgz) and a single .gz file, read only. A tar
// is read where it is; a gzip is first unpacked whole to a file of the moment (SD:/tmp, removed when
// the archive is closed) -- zlib's inflate as a stream --, then read as a tar, or, when what it holds is
// not one, shown as one entry (the file's name without ".gz"). ustar's prefix, GNU's long names ('L')
// and pax's "path" records are followed; links and devices are left out.
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
#ifndef _filekit_tar_h
#define _filekit_tar_h

#include "arc.h"
#include "arcpath.h"		// mkdirs
#include "zlib.h"

namespace arc {

class TarArchive : public Archive
{
public:
	char src[300];				// where the tar's bytes are read: the file, or the unpacked copy
	bool gz, single;			// a gzip around it; ... that holds one plain file, not a tar
	TarArchive () : gz (false), single (false) { src[0] = 0; }
	~TarArchive () override { drop_tmp (); }
	const char *format () const override { return gz ? (single ? "GZIP" : "TAR.GZ") : "TAR"; }
	const char *methodName (const Entry &x) const override { return x.dir ? "" : gz ? "Deflate" : "Store"; }

	bool open (const char *p) override
	{
		clear (); drop_tmp ();
		scopy (path, p, sizeof path);
		scopy (src, p, sizeof src);
		gz = single = false;
		u8 m[2] = { 0, 0 };
		{ Reader r; if (!r.open (p)) { fail ("Cannot open the file."); return false; } if (r.size >= 2) r.read (m, 2); }
		if (m[0] == 0x1F && m[1] == 0x8B)
		{
			gz = true;
			if (!gunzip (p)) return false;
		}
		Reader r;
		if (!r.open (src)) { fail ("Cannot open the file."); return false; }
		if (!read_tar (r))
		{
			if (!gz) { fail ("This is not a tar archive."); return false; }
			// a plain file that was gzipped: one entry
			clear (); single = true;
			char nm[300]; scopy (nm, base_of (p), sizeof nm);
			int n2 = (int) strlen (nm);
			if (n2 > 3 && !ci_cmp (nm + n2 - 3, ".gz")) nm[n2 - 3] = 0;
			Entry &x = add ();
			x.name = sdup (nm); x.size = r.size; x.offset = 0; x.dostime = dos_now ();
			Reader orig; if (orig.open (p)) x.packed = orig.size;
		}
		else if (gz)					// what each entry takes: its share of the gzip's size
		{
			u64 total = totalSize (), packed = 0;
			{ Reader orig; if (orig.open (p)) packed = orig.size; }
			for (int i = 0; total && i < n; i++) e[i].packed = e[i].size * packed / total;
		}
		return true;
	}
	bool extract (int i, Sink &out, Progress *pr) override
	{
		if (i < 0 || i >= n) { fail ("No such entry."); return false; }
		const Entry &x = e[i];
		if (x.dir) return true;
		Reader r;
		if (!r.open (src) || !r.seek (x.offset)) { fail ("Cannot read the archive."); return false; }
		static const u32 CH = 64 * 1024;
		u8 *b = (u8 *) malloc (CH);
		if (!b) { fail ("Not enough memory."); return false; }
		bool ok = true;
		for (u64 left = x.size; ok && left; )
		{
			u32 k = left > CH ? CH : (u32) left;
			if (!r.read (b, k)) { fail ("The archive is cut short."); ok = false; break; }
			if (!out.write (b, k)) { fail ("Cannot write the file."); ok = false; break; }
			left -= k;
			if (pr && !pr->step (k)) { fail ("Cancelled."); ok = false; }
		}
		free (b);
		return ok;
	}

private:
	char tmp[300] = "";
	void drop_tmp () { if (tmp[0]) { kapi_remove (tmp); tmp[0] = 0; } }

	static u64 octal (const u8 *p, int n)
	{
		if (p[0] & 0x80)				// GNU's binary size (a file over 8 GB)
		{
			u64 v = 0; for (int i = 1; i < n; i++) v = v << 8 | p[i];
			return v;
		}
		u64 v = 0; int i = 0;
		while (i < n && (p[i] == ' ' || p[i] == 0)) i++;
		for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8 + (u64) (p[i] - '0');
		return v;
	}
	// seconds from 1970 (UTC) -> a DOS date and time
	static u32 dos_of_unix (u64 t)
	{
		u64 days = t / 86400; u32 s = (u32) (t % 86400);
		long long z = (long long) days + 719468, era = z / 146097;
		unsigned doe = (unsigned) (z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		long long y = (long long) yoe + era * 400;
		unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1;
		unsigned mo = mp < 10 ? mp + 3 : mp - 9;
		if (mo <= 2) y++;
		if (y < 1980) return 0;
		return (u32) (((y - 1980) << 25) | (mo << 21) | (d << 16) | ((s / 3600) << 11) | ((s / 60 % 60) << 5) | (s % 60 / 2));
	}
	static bool header_ok (const u8 *h)
	{
		unsigned sum = 0;
		for (int i = 0; i < 512; i++) sum += (i >= 148 && i < 156) ? ' ' : h[i];
		return sum == (unsigned) octal (h + 148, 8);
	}
	bool read_tar (Reader &r)
	{
		u8 h[512];
		u64 pos = 0;
		char *longName = 0;
		int zeros = 0;
		while (pos + 512 <= r.size)
		{
			if (!r.read_at (pos, h, 512)) break;
			pos += 512;
			bool empty = true; for (int i = 0; i < 512 && empty; i++) if (h[i]) empty = false;
			if (empty) { if (++zeros >= 2) break; continue; }
			zeros = 0;
			if (!header_ok (h)) { free (longName); return n > 0; }	// (not a tar; or damaged after some entries)
			u64 size = octal (h + 124, 12);
			char type = (char) h[156];
			u64 next = pos + ((size + 511) & ~511ull);
			if (type == 'L' || type == 'x')				// the next entry's name
			{
				u32 k = size > 4096 ? 4096 : (u32) size;
				char *d = (char *) malloc ((size_t) k + 1);
				if (d && r.read_at (pos, d, k))
				{
					d[k] = 0;
					if (type == 'L') { free (longName); longName = sdup (d); }
					else							// pax: "<len> path=<name>\n" records
						for (char *q = d; *q; )
						{
							int len = atoi (q); if (len <= 0) break;
							char *sp = strchr (q, ' ');
							if (sp && !strncmp (sp + 1, "path=", 5) && len < (int) k + 1)
							{
								char *e2 = q + len - 1;		// (the record's newline)
								free (longName); longName = sdup (sp + 6, (int) (e2 - (sp + 6)));
							}
							q += len;
						}
				}
				free (d);
				pos = next; continue;
			}
			if (type == 'g') { pos = next; continue; }			// (pax's global header)
			char nm[512]; int k = 0;
			if (!memcmp (h + 257, "ustar", 5) && h[345])
			{
				for (int i = 0; i < 155 && h[345 + i]; i++) nm[k++] = (char) h[345 + i];
				nm[k++] = '/';
			}
			for (int i = 0; i < 100 && h[i]; i++) nm[k++] = (char) h[i];
			nm[k] = 0;
			const char *name = longName ? longName : nm;
			int nl = (int) strlen (name);
			bool dir = type == '5' || (nl && name[nl - 1] == '/');
			if (type == '0' || type == 0 || type == '7' || dir)		// a file, a folder (links and devices: left out)
			{
				Entry &x = add ();
				x.name = clean_name (name, nl, true);
				x.dir = dir; x.size = dir ? 0 : size; x.packed = x.size; x.offset = pos;
				x.dostime = dos_of_unix (octal (h + 136, 12));
				if (!x.name || !x.name[0]) { free (x.name); n--; }
			}
			free (longName); longName = 0;
			pos = next;
		}
		free (longName);
		return n > 0;
	}
	// The gzip unpacked to a file of the moment (src then names it).
	bool gunzip (const char *p)
	{
		Reader r;
		if (!r.open (p)) { fail ("Cannot open the file."); return false; }
		mkdirs ("SD:/tmp");
		static unsigned s_serial;
		char num[24]; u64_str ((u64) kapi_get_ticks () * 16 + (s_serial++ & 15), num, sizeof num);
		scopy (tmp, "SD:/tmp/fk-", sizeof tmp); scat (tmp, num, sizeof tmp); scat (tmp, ".tar", sizeof tmp);
		FileSink out;
		if (!out.open (tmp)) { tmp[0] = 0; fail ("Cannot unpack the archive (SD:/tmp)."); return false; }
		static const u32 CH = 64 * 1024;
		u8 *in = (u8 *) malloc (CH), *ob = (u8 *) malloc (CH);
		z_stream z; memset (&z, 0, sizeof z);
		bool ok = in && ob && inflateInit2 (&z, 15 + 16) == Z_OK;
		bool done = false;
		for (u64 left = r.size; ok && !done; )
		{
			if (z.avail_in == 0)
			{
				if (!left) break;
				u32 k = left > CH ? CH : (u32) left;
				if (!r.read (in, k)) { ok = false; break; }
				left -= k; z.next_in = in; z.avail_in = k;
			}
			z.next_out = ob; z.avail_out = CH;
			int rc = inflate (&z, Z_NO_FLUSH);
			if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) { ok = false; break; }
			u32 got = CH - z.avail_out;
			if (got && !out.write (ob, got)) { ok = false; break; }
			if (rc == Z_STREAM_END)
			{
				if (z.avail_in == 0 && !left) done = true;
				else if (inflateReset (&z) != Z_OK) ok = false;	// (several gzip members one after the other)
			}
		}
		if (ok) inflateEnd (&z);
		free (in); free (ob);
		out.close ();
		if (!ok) { drop_tmp (); fail ("The gzip archive is damaged."); return false; }
		scopy (src, tmp, sizeof src);
		return true;
	}
};

} // namespace arc

#endif
