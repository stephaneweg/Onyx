//
// filekit/zip.h -- ZIP: read (the central directory, zip64, names in CP437 or UTF-8, Info-ZIP's
// Unicode path), extract (stored, Deflate through zlib; ZipCrypto with a password), and write by
// rewriting the archive into a new file: the entries kept copied as they are packed (a new local
// header, the same bytes), the new ones stored or deflated (zlib, levels 1..9), the central
// directory after them (zip64 records when an offset, a size or the count needs them).
// Not yet: AES (WinZip's), Deflate64, bzip2, LZMA, zstd methods -- they are listed, not extracted.
//
#ifndef _archiver_zip_h
#define _archiver_zip_h

#include "arc.h"
#include "zlib.h"

namespace arc {

static inline u16 rd16 (const u8 *p) { return (u16) (p[0] | p[1] << 8); }
static inline u32 rd32 (const u8 *p) { return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16 | (u32) p[3] << 24; }
static inline u64 rd64 (const u8 *p) { return (u64) rd32 (p) | (u64) rd32 (p + 4) << 32; }
static inline u8 *wr16 (u8 *p, u32 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); return p + 2; }
static inline u8 *wr32 (u8 *p, u32 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); p[2] = (u8) (v >> 16); p[3] = (u8) (v >> 24); return p + 4; }
static inline u8 *wr64 (u8 *p, u64 v) { wr32 (p, (u32) v); return wr32 (p + 4, (u32) (v >> 32)); }

enum { ZIP_LOCAL = 0x04034b50, ZIP_CENTRAL = 0x02014b50, ZIP_EOCD = 0x06054b50, ZIP_EOCD64 = 0x06064b50,
       ZIP_LOC64 = 0x07064b50, ZIP_DESC = 0x08074b50 };
enum { ZF_ENCRYPTED = 1, ZF_DESCRIPTOR = 8, ZF_UTF8 = 0x800 };
static const u32 Z32 = 0xFFFFFFFFu;
enum { IOBUF = 65536 };

// ZipCrypto (PKWARE's traditional encryption): the three keys
struct ZipCrypto
{
	u32 k0, k1, k2;
	void init (const char *pw)
	{
		k0 = 0x12345678; k1 = 0x23456789; k2 = 0x34567890;
		for (; *pw; pw++) update ((u8) *pw);
	}
	void update (u8 c)
	{
		k0 = (u32) crc32 (k0 ^ 0xFFFFFFFFu, &c, 1) ^ 0xFFFFFFFFu;
		k1 = (k1 + (k0 & 0xFF)) * 134775813u + 1;
		u8 h = (u8) (k1 >> 24);
		k2 = (u32) crc32 (k2 ^ 0xFFFFFFFFu, &h, 1) ^ 0xFFFFFFFFu;
	}
	u8 decrypt (u8 c)
	{
		u16 t = (u16) (k2 | 2);
		u8 p = (u8) (c ^ ((t * (t ^ 1)) >> 8));
		update (p);
		return p;
	}
};

class ZipArchive : public Archive
{
public:
	char password[128];
	u64  sfx;				// bytes before the archive (a self-extractor's)
	ZipArchive () : sfx (0) { password[0] = 0; }
	const char *format () const override { return "ZIP"; }
	bool writable () const override { return true; }
	const char *methodName (const Entry &x) const override
	{
		if (x.dir) return "";
		switch (x.method)
		{
		case 0: return x.encrypted ? "Store, crypted" : "Store";
		case 8: return x.encrypted ? "Deflate, crypted" : "Deflate";
		case 9: return "Deflate64"; case 12: return "BZip2"; case 14: return "LZMA";
		case 93: return "Zstd"; case 95: return "XZ"; case 98: return "PPMd"; case 99: return "AES";
		default: return "Other";
		}
	}

	// ---- reading ----------------------------------------------------------------------------------
	bool open (const char *p) override
	{
		clear (); scopy (path, p, sizeof path); error[0] = 0;
		Reader r;
		if (!r.open (p)) { fail ("Cannot open the file."); return false; }
		if (r.size < 22) { fail ("Not a ZIP archive (too short)."); return false; }
		u32 tail = r.size < 65557 + 20 ? (u32) r.size : 65557 + 20;
		u8 *t = (u8 *) malloc (tail);
		if (!t || !r.read_at (r.size - tail, t, tail)) { free (t); fail ("Cannot read the file."); return false; }
		int e0 = -1;
		for (int i = (int) tail - 22; i >= 0; i--)
			if (rd32 (t + i) == ZIP_EOCD && (u64) i + 22 + rd16 (t + i + 20) <= tail) { e0 = i; break; }
		if (e0 < 0) { free (t); fail ("Not a ZIP archive (no central directory)."); return false; }
		u64 eocdPos = r.size - tail + (u64) e0;
		u64 count = rd16 (t + e0 + 10), cdSize = rd32 (t + e0 + 12), cdOff = rd32 (t + e0 + 16);
		u16 clen = rd16 (t + e0 + 20);
		if (clen) comment = sdup ((const char *) t + e0 + 22, clen);
		u64 cdEnd = eocdPos;
		if (e0 >= 20 && rd32 (t + e0 - 20) == ZIP_LOC64)
		{
			u64 z64 = rd64 (t + e0 - 20 + 8);
			u8 z[56];
			if (r.read_at (z64, z, 56) && rd32 (z) == ZIP_EOCD64)
			{ count = rd64 (z + 32); cdSize = rd64 (z + 40); cdOff = rd64 (z + 48); cdEnd = z64; }
			else
			{	// (an archive with bytes before it: the zip64 record moved too)
				u64 guess = eocdPos - 20 - 56;
				if (r.read_at (guess, z, 56) && rd32 (z) == ZIP_EOCD64)
				{ count = rd64 (z + 32); cdSize = rd64 (z + 40); cdOff = rd64 (z + 48); cdEnd = guess; }
			}
		}
		free (t);
		if (cdSize > cdEnd) { fail ("The central directory is damaged."); return false; }
		u64 cdStart = cdEnd - cdSize;
		sfx = cdStart >= cdOff ? cdStart - cdOff : 0;
		if (cdSize > 256u * 1024 * 1024) { fail ("The central directory is too large."); return false; }
		u8 *cd = (u8 *) malloc ((size_t) cdSize + 1);
		if (!cd) { fail ("Not enough memory for the directory."); return false; }
		if (!r.read_at (cdStart, cd, (u32) cdSize)) { free (cd); fail ("Cannot read the central directory."); return false; }
		u64 q = 0;
		for (u64 k = 0; k < count && q + 46 <= cdSize; k++)
		{
			const u8 *h = cd + q;
			if (rd32 (h) != ZIP_CENTRAL) break;
			u16 nlen = rd16 (h + 28), xlen = rd16 (h + 30), cl = rd16 (h + 32);
			if (q + 46 + nlen + xlen + cl > cdSize) break;
			Entry &x = add ();
			x.made = rd16 (h + 4); x.ver = rd16 (h + 6); x.flags = rd16 (h + 8); x.method = rd16 (h + 10);
			x.dostime = (u32) rd16 (h + 14) << 16 | rd16 (h + 12);
			x.crc = rd32 (h + 16); x.packed = rd32 (h + 20); x.size = rd32 (h + 24);
			x.intAttr = rd16 (h + 36); x.extAttr = rd32 (h + 38); x.offset = rd32 (h + 42);
			const char *nm = (const char *) h + 46;
			const u8 *xf = h + 46 + nlen;
			char *uname = 0;
			// the extra field: zip64's sizes / offset, Info-ZIP's Unicode path, AES; the rest kept
			x.cext = (u8 *) malloc (xlen ? xlen : 1); x.cextLen = 0;
			for (u32 j = 0; j + 4 <= xlen;)
			{
				u16 id = rd16 (xf + j), sz = rd16 (xf + j + 2);
				if (j + 4 + sz > xlen) break;
				const u8 *d = xf + j + 4;
				if (id == 0x0001)
				{
					u32 o = 0;
					if (x.size == Z32 && o + 8 <= sz) { x.size = rd64 (d + o); o += 8; }
					if (x.packed == Z32 && o + 8 <= sz) { x.packed = rd64 (d + o); o += 8; }
					if (x.offset == Z32 && o + 8 <= sz) { x.offset = rd64 (d + o); o += 8; }
				}
				else
				{
					if (id == 0x7075 && sz > 5 && !uname) uname = clean_name ((const char *) d + 5, sz - 5, true);
					if (id == 0x9901 && sz >= 7) x.method = 99;
					memcpy (x.cext + x.cextLen, xf + j, (size_t) sz + 4); x.cextLen = (u16) (x.cextLen + sz + 4);
				}
				j += 4u + sz;
			}
			if (cl) { x.comment = (u8 *) sdup ((const char *) h + 46 + nlen + xlen, cl); x.commentLen = cl; }
			x.dir = (nlen && (nm[nlen - 1] == '/' || nm[nlen - 1] == '\\')) || ((x.extAttr & 0x10) && x.size == 0 && x.method == 0);
			// (no UTF-8 flag: Info-ZIP on Unix writes UTF-8 without it; a name that is valid UTF-8 is read so)
			x.name = uname ? uname : clean_name (nm, nlen, (x.flags & ZF_UTF8) != 0 || valid_utf8 (nm, nlen));
			x.encrypted = (x.flags & ZF_ENCRYPTED) != 0;
			x.offset += sfx;
			if (!x.name || !x.name[0]) { free (x.name); free (x.cext); free (x.comment); n--; }
			q += 46u + nlen + xlen + cl;
		}
		free (cd);
		if ((u64) n < count && n == 0) { fail ("The central directory is damaged."); return false; }
		return true;
	}

	// Where entry x's data starts (past its local header), its local extra field into *lx (malloc'ed).
	bool dataStart (Reader &r, const Entry &x, u64 *start, u8 **lx = 0, u16 *lxLen = 0)
	{
		u8 h[30];
		if (!r.read_at (x.offset, h, 30) || rd32 (h) != ZIP_LOCAL) { fail ("A local header is damaged: ", x.name); return false; }
		u16 nlen = rd16 (h + 26), xlen = rd16 (h + 28);
		*start = x.offset + 30 + nlen + xlen;
		if (lx)
		{
			u8 *b = (u8 *) malloc ((size_t) xlen + 1); u16 k = 0;
			u8 *raw = (u8 *) malloc ((size_t) xlen + 1);
			if (b && raw && (xlen == 0 || r.read_at (x.offset + 30 + nlen, raw, xlen)))
				for (u32 j = 0; j + 4 <= xlen;)	// (all but zip64's: the writer makes its own)
				{
					u16 id = rd16 (raw + j), sz = rd16 (raw + j + 2);
					if (j + 4 + sz > xlen) break;
					if (id != 0x0001) { memcpy (b + k, raw + j, (size_t) sz + 4); k = (u16) (k + sz + 4); }
					j += 4u + sz;
				}
			free (raw);
			*lx = b; *lxLen = k;
		}
		return true;
	}

	bool extract (int i, Sink &out, Progress *pr) override
	{
		if (i < 0 || i >= n) return false;
		Entry &x = e[i];
		if (x.dir) return true;
		if (x.method == 99) { fail ("AES encryption is not supported yet: ", x.name); return false; }
		if (x.method != 0 && x.method != 8) { fail ("This compression method is not supported yet: ", x.name); return false; }
		Reader r;
		if (!r.open (path)) { fail ("Cannot open the archive."); return false; }
		u64 pos;
		if (!dataStart (r, x, &pos)) return false;
		u64 left = x.packed;
		ZipCrypto zc; bool crypt = x.encrypted;
		if (crypt)
		{
			if (!password[0]) { fail ("A password is needed: ", x.name); return false; }
			u8 hd[12];
			if (left < 12 || !r.read_at (pos, hd, 12)) { fail ("The archive is damaged: ", x.name); return false; }
			zc.init (password);
			for (int k = 0; k < 12; k++) hd[k] = zc.decrypt (hd[k]);
			u8 check = (x.flags & ZF_DESCRIPTOR) ? (u8) (x.dostime >> 8) : (u8) (x.crc >> 24);
			if (hd[11] != check) { fail ("Wrong password for ", x.name); return false; }
			pos += 12; left -= 12;
		}
		u8 *in = (u8 *) malloc (IOBUF), *ob = (u8 *) malloc (IOBUF);
		if (!in || !ob) { free (in); free (ob); fail ("Not enough memory."); return false; }
		bool ok = r.seek (pos);
		u32 crc = (u32) crc32 (0, 0, 0); u64 outn = 0;
		z_stream zs; memset (&zs, 0, sizeof zs);
		bool inflating = x.method == 8;
		if (inflating && inflateInit2 (&zs, -MAX_WBITS) != Z_OK) ok = false;
		int zr = Z_OK;
		while (ok && (left || (inflating && zr != Z_STREAM_END)))
		{
			u32 k = left > IOBUF ? (u32) IOBUF : (u32) left;
			if (k && !r.read (in, k)) { fail ("Cannot read the archive: ", x.name); ok = false; break; }
			left -= k;
			if (crypt) for (u32 j = 0; j < k; j++) in[j] = zc.decrypt (in[j]);
			if (!inflating)
			{
				crc = (u32) crc32 (crc, in, k); outn += k;
				if (!out.write (in, k)) { fail ("Cannot write ", x.name); ok = false; break; }
				if (pr && !pr->step (k)) { fail ("Cancelled."); ok = false; break; }
				continue;
			}
			zs.next_in = in; zs.avail_in = k;
			do
			{
				zs.next_out = ob; zs.avail_out = IOBUF;
				zr = inflate (&zs, Z_NO_FLUSH);
				if (zr != Z_OK && zr != Z_STREAM_END && !(zr == Z_BUF_ERROR && k == 0 && zs.avail_out == IOBUF))
				{ fail ("The data are damaged: ", x.name); ok = false; break; }
				u32 got = IOBUF - zs.avail_out;
				if (got)
				{
					crc = (u32) crc32 (crc, ob, got); outn += got;
					if (!out.write (ob, got)) { fail ("Cannot write ", x.name); ok = false; break; }
					if (pr && !pr->step (got)) { fail ("Cancelled."); ok = false; break; }
				}
				if (zr == Z_BUF_ERROR) break;
			}
			while (ok && zr != Z_STREAM_END && (zs.avail_in || zs.avail_out == 0));
			if (ok && k == 0 && zr != Z_STREAM_END) { fail ("The data end too early: ", x.name); ok = false; }
		}
		if (inflating) inflateEnd (&zs);
		free (in); free (ob);
		if (ok && (crc != x.crc || outn != x.size)) { fail (crypt ? "Wrong password or damaged data: " : "Checksum error (damaged data): ", x.name); ok = false; }
		return ok;
	}

	// ---- writing ----------------------------------------------------------------------------------
	struct Cent { char *name; u64 off, size, packed; u32 crc, time, flags, extAttr; int method; u16 made, ver, intAttr;
		      u8 *cext; u16 cextLen; u8 *comment; u16 commentLen; };

	// a local header (+ zip64's extra when the sizes need it: sizes known, or a big file streamed)
	static u32 localHeader (u8 *b, const char *name, u32 flags, int method, u32 time, u32 crc, u64 packed, u64 size,
				 const u8 *lx, u16 lxLen, bool z64)
	{
		u32 nl = (u32) strlen (name);
		u8 *p = b;
		p = wr32 (p, ZIP_LOCAL); p = wr16 (p, z64 ? 45 : 20); p = wr16 (p, flags); p = wr16 (p, (u32) method);
		p = wr16 (p, time & 0xFFFF); p = wr16 (p, time >> 16); p = wr32 (p, crc);
		p = wr32 (p, z64 ? Z32 : (u32) packed); p = wr32 (p, z64 ? Z32 : (u32) size);
		p = wr16 (p, nl); p = wr16 (p, (z64 ? 20u : 0u) + lxLen);
		memcpy (p, name, nl); p += nl;
		if (z64) { p = wr16 (p, 1); p = wr16 (p, 16); p = wr64 (p, size); p = wr64 (p, packed); }
		if (lxLen) { memcpy (p, lx, lxLen); p += lxLen; }
		return (u32) (p - b);
	}

	bool rewrite (const Plan &plan, const char *dest, Progress *pr) override
	{
		error[0] = 0;
		int total = plan.nkeep + plan.nadd;
		Cent *c = (Cent *) calloc ((size_t) (total ? total : 1), sizeof (Cent));
		u8 *buf = (u8 *) malloc (IOBUF), *zb = (u8 *) malloc (IOBUF), *hb = (u8 *) malloc (70000);
		FileSink out;
		bool ok = c && buf && zb && hb;
		if (!ok) fail ("Not enough memory.");
		if (ok && !out.open (dest)) { fail ("Cannot write ", dest); ok = false; }
		Reader r;
		if (ok && plan.nkeep && !r.open (path)) { fail ("Cannot open the archive."); ok = false; }
		int nc = 0;
		// the entries kept: a new local header, the same packed bytes
		for (int k = 0; ok && k < plan.nkeep; k++)
		{
			const Entry &x = e[plan.keep[k]];
			const char *nm = plan.rename[k] ? plan.rename[k] : x.name;
			char *full = (char *) malloc (strlen (nm) + 2);
			strcpy (full, nm); if (x.dir) strcat (full, "/");
			u64 start; u8 *lx = 0; u16 lxLen = 0;
			if (!dataStart (r, x, &start, &lx, &lxLen)) { free (full); ok = false; break; }
			bool desc = x.encrypted && (x.flags & ZF_DESCRIPTOR);	// (ZipCrypto's check byte depends on it)
			u32 flags = (x.flags & ~(u32) (ZF_DESCRIPTOR | ZF_UTF8)) | (desc ? ZF_DESCRIPTOR : 0) | (is_ascii (full) ? 0 : ZF_UTF8);
			bool z64 = x.size >= Z32 || x.packed >= Z32;
			Cent &ce = c[nc++];
			ce.name = full; ce.off = out.written; ce.size = x.size; ce.packed = x.packed; ce.crc = x.crc;
			ce.time = x.dostime; ce.flags = flags; ce.method = x.method; ce.extAttr = x.extAttr; ce.made = x.made;
			ce.ver = x.ver; ce.intAttr = x.intAttr;
			ce.cext = x.cextLen ? (u8 *) sdup ((const char *) x.cext, x.cextLen) : 0; ce.cextLen = x.cextLen;
			ce.comment = x.commentLen ? (u8 *) sdup ((const char *) x.comment, x.commentLen) : 0; ce.commentLen = x.commentLen;
			if (pr) pr->file (nm);
			u32 hl = localHeader (hb, full, flags, x.method, x.dostime, desc ? 0 : x.crc, desc ? 0 : x.packed,
					      desc ? 0 : x.size, lx, lxLen, z64);
			free (lx);
			if (!out.write (hb, hl)) { fail ("Cannot write ", dest); ok = false; break; }
			ok = r.seek (start);
			for (u64 left = x.packed; ok && left;)
			{
				u32 k2 = left > IOBUF ? (u32) IOBUF : (u32) left;
				if (!r.read (buf, k2)) { fail ("Cannot read the archive: ", x.name); ok = false; break; }
				if (!out.write (buf, k2)) { fail ("Cannot write ", dest); ok = false; break; }
				left -= k2;
				if (pr && !pr->step (k2)) { fail ("Cancelled."); ok = false; }
			}
			if (ok && desc && !writeDesc (out, hb, x.crc, x.packed, x.size, z64)) { fail ("Cannot write ", dest); ok = false; }
		}
		// the new ones
		u32 now = dos_now ();
		for (int k = 0; ok && k < plan.nadd; k++)
		{
			const NewItem &it = plan.add[k];
			Cent &ce = c[nc++];
			if (pr) pr->file (it.name);
			if (!it.disk)
			{	// a folder
				char *full = (char *) malloc (strlen (it.name) + 2); strcpy (full, it.name); strcat (full, "/");
				ce.name = full; ce.off = out.written; ce.time = now; ce.flags = is_ascii (full) ? 0 : ZF_UTF8;
				ce.extAttr = 0x10; ce.made = 20; ce.ver = 20;
				u32 hl = localHeader (hb, full, ce.flags, 0, now, 0, 0, 0, 0, 0, false);
				if (!out.write (hb, hl)) { fail ("Cannot write ", dest); ok = false; }
				continue;
			}
			ce.name = sdup (it.name); ce.off = out.written; ce.time = now; ce.made = 20; ce.extAttr = 0x20;
			Reader f;
			if (!f.open (it.disk)) { fail ("Cannot read ", it.disk); ok = false; break; }
			u64 size = f.size;
			bool store = plan.level == 0 || (plan.storePacked && packed_ext (it.name)) || size == 0;
			bool z64 = size >= Z32 - 0x10000;
			u32 utf = is_ascii (it.name) ? 0 : ZF_UTF8;
			ce.size = size; ce.ver = z64 ? 45 : 20;
			if (store)
			{	// the CRC first (a pass over the file), then a header with every size known
				u32 crc = (u32) crc32 (0, 0, 0);
				for (u64 left = size; ok && left;)
				{
					u32 k2 = left > IOBUF ? (u32) IOBUF : (u32) left;
					if (!f.read (buf, k2)) { fail ("Cannot read ", it.disk); ok = false; break; }
					crc = (u32) crc32 (crc, buf, k2); left -= k2;
				}
				if (!ok) break;
				ce.crc = crc; ce.packed = size; ce.method = 0; ce.flags = utf;
				u32 hl = localHeader (hb, it.name, ce.flags, 0, now, crc, size, size, 0, 0, z64);
				ok = out.write (hb, hl) && f.seek (0);
				for (u64 left = size; ok && left;)
				{
					u32 k2 = left > IOBUF ? (u32) IOBUF : (u32) left;
					if (!f.read (buf, k2)) { fail ("Cannot read ", it.disk); ok = false; break; }
					if (!out.write (buf, k2)) { fail ("Cannot write ", dest); ok = false; break; }
					left -= k2;
					if (pr && !pr->step (k2)) { fail ("Cancelled."); ok = false; }
				}
				if (!ok && !error[0]) fail ("Cannot write ", dest);
				continue;
			}
			// a small file: deflated in memory, stored if that did not make it smaller; sizes known
			if (size <= 4u * 1024 * 1024)
			{
				u8 *src = (u8 *) malloc ((size_t) size), *dst = 0;
				z_stream zs; memset (&zs, 0, sizeof zs);
				uLong bound = 0;
				bool mem = src && f.read (src, (u32) size) && deflateInit2 (&zs, plan.level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK;
				if (mem)
				{
					bound = deflateBound (&zs, (uLong) size);
					dst = (u8 *) malloc (bound);
					zs.next_in = src; zs.avail_in = (uInt) size; zs.next_out = dst; zs.avail_out = (uInt) bound;
					mem = dst && deflate (&zs, Z_FINISH) == Z_STREAM_END;
					deflateEnd (&zs);
				}
				if (!mem) { free (src); free (dst); fail ("Cannot read ", it.disk); ok = false; break; }
				u32 crc = (u32) crc32 (crc32 (0, 0, 0), src, (uInt) size);
				bool st = zs.total_out >= size;
				ce.crc = crc; ce.method = st ? 0 : 8; ce.packed = st ? size : zs.total_out;
				ce.flags = utf | (st ? 0u : plan.level >= 8 ? 2u : plan.level <= 2 ? 4u : 0u);
				u32 hl = localHeader (hb, it.name, ce.flags, ce.method, now, crc, ce.packed, size, 0, 0, false);
				ok = out.write (hb, hl) && out.write (st ? src : dst, (u32) ce.packed);
				free (src); free (dst);
				if (!ok) { fail ("Cannot write ", dest); break; }
				if (pr && !pr->step (size)) { fail ("Cancelled."); ok = false; }
				continue;
			}
			// deflated, streamed: the sizes and the CRC in a data descriptor after the data
			ce.method = 8; ce.flags = utf | ZF_DESCRIPTOR | (plan.level >= 8 ? 2u : plan.level <= 2 ? 4u : 0u);
			u32 hl = localHeader (hb, it.name, ce.flags, 8, now, 0, 0, 0, 0, 0, z64);
			if (!out.write (hb, hl)) { fail ("Cannot write ", dest); ok = false; break; }
			z_stream zs; memset (&zs, 0, sizeof zs);
			if (deflateInit2 (&zs, plan.level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
			{ fail ("Not enough memory."); ok = false; break; }
			u32 crc = (u32) crc32 (0, 0, 0); u64 packed = 0;
			for (u64 left = size;;)
			{
				u32 k2 = left > IOBUF ? (u32) IOBUF : (u32) left;
				if (k2 && !f.read (buf, k2)) { fail ("Cannot read ", it.disk); ok = false; break; }
				crc = (u32) crc32 (crc, buf, k2); left -= k2;
				zs.next_in = buf; zs.avail_in = k2;
				int flush = left ? Z_NO_FLUSH : Z_FINISH, zr;
				do
				{
					zs.next_out = zb; zs.avail_out = IOBUF;
					zr = deflate (&zs, flush);
					u32 got = IOBUF - zs.avail_out;
					if (got && !out.write (zb, got)) { fail ("Cannot write ", dest); ok = false; break; }
					packed += got;
				}
				while (zs.avail_out == 0);
				if (!ok) break;
				if (pr && k2 && !pr->step (k2)) { fail ("Cancelled."); ok = false; break; }
				if (flush == Z_FINISH && zr == Z_STREAM_END) break;
				if (!left && zr != Z_STREAM_END) continue;
			}
			deflateEnd (&zs);
			if (!ok) break;
			ce.crc = crc; ce.packed = packed;
			if (!writeDesc (out, hb, crc, packed, size, z64)) { fail ("Cannot write ", dest); ok = false; }
		}
		// the central directory
		u64 cdStart = out.written;
		for (int k = 0; ok && k < nc; k++)
		{
			Cent &ce = c[k];
			bool zs = ce.size >= Z32, zp = ce.packed >= Z32, zo = ce.off >= Z32;
			u32 nl = (u32) strlen (ce.name), zl = (zs || zp || zo) ? 4u + 8u * (zs + zp + zo) : 0;
			u8 *p = hb;
			p = wr32 (p, ZIP_CENTRAL); p = wr16 (p, ce.made ? ce.made : 20); p = wr16 (p, zl ? 45 : (ce.ver ? ce.ver : 20));
			p = wr16 (p, ce.flags); p = wr16 (p, (u32) ce.method); p = wr16 (p, ce.time & 0xFFFF); p = wr16 (p, ce.time >> 16);
			p = wr32 (p, ce.crc); p = wr32 (p, zp ? Z32 : (u32) ce.packed); p = wr32 (p, zs ? Z32 : (u32) ce.size);
			p = wr16 (p, nl); p = wr16 (p, zl + ce.cextLen); p = wr16 (p, ce.commentLen); p = wr16 (p, 0);
			p = wr16 (p, ce.intAttr); p = wr32 (p, ce.extAttr); p = wr32 (p, zo ? Z32 : (u32) ce.off);
			memcpy (p, ce.name, nl); p += nl;
			if (zl)
			{
				p = wr16 (p, 1); p = wr16 (p, zl - 4);
				if (zs) p = wr64 (p, ce.size);
				if (zp) p = wr64 (p, ce.packed);
				if (zo) p = wr64 (p, ce.off);
			}
			if (ce.cextLen) { memcpy (p, ce.cext, ce.cextLen); p += ce.cextLen; }
			if (ce.commentLen) { memcpy (p, ce.comment, ce.commentLen); p += ce.commentLen; }
			if (!out.write (hb, (u32) (p - hb))) { fail ("Cannot write ", dest); ok = false; }
		}
		if (ok)
		{
			u64 cdSize = out.written - cdStart, eocd64 = out.written;
			bool z64 = nc >= 0xFFFF || cdStart >= Z32 || cdSize >= Z32;
			u8 *p = hb;
			if (z64)
			{
				p = wr32 (p, ZIP_EOCD64); p = wr64 (p, 44); p = wr16 (p, 45); p = wr16 (p, 45); p = wr32 (p, 0); p = wr32 (p, 0);
				p = wr64 (p, (u64) nc); p = wr64 (p, (u64) nc); p = wr64 (p, cdSize); p = wr64 (p, cdStart);
				p = wr32 (p, ZIP_LOC64); p = wr32 (p, 0); p = wr64 (p, eocd64); p = wr32 (p, 1);
			}
			u32 cl = comment ? (u32) strlen (comment) : 0; if (cl > 65535) cl = 65535;
			p = wr32 (p, ZIP_EOCD); p = wr16 (p, 0); p = wr16 (p, 0);
			p = wr16 (p, nc >= 0xFFFF ? 0xFFFF : (u32) nc); p = wr16 (p, nc >= 0xFFFF ? 0xFFFF : (u32) nc);
			p = wr32 (p, cdSize >= Z32 ? Z32 : (u32) cdSize); p = wr32 (p, cdStart >= Z32 ? Z32 : (u32) cdStart);
			p = wr16 (p, cl);
			if (cl) { memcpy (p, comment, cl); p += cl; }
			if (!out.write (hb, (u32) (p - hb))) { fail ("Cannot write ", dest); ok = false; }
		}
		out.close ();
		for (int k = 0; k < nc; k++) { free (c[k].name); free (c[k].cext); free (c[k].comment); }
		free (c); free (buf); free (zb); free (hb);
		return ok && out.ok;
	}

	static bool writeDesc (FileSink &out, u8 *b, u32 crc, u64 packed, u64 size, bool z64)
	{
		u8 *p = b;
		p = wr32 (p, ZIP_DESC); p = wr32 (p, crc);
		if (z64) { p = wr64 (p, packed); p = wr64 (p, size); }
		else { p = wr32 (p, (u32) packed); p = wr32 (p, (u32) size); }
		return out.write (b, (u32) (p - b));
	}
};

} // namespace arc

#endif
