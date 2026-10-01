//
// Apps/media/tags.h -- what a song says about itself, read only: its title, artist, album artist, album,
// genre, year, track and disc numbers, its length, and where its cover is (a picture inside the file:
// its offset and length). MP3: ID3v2 (2.2, 2.3, 2.4) then ID3v1, the length from the Xing / Info /
// VBRI header or the bit rate; FLAC: STREAMINFO, VORBIS_COMMENT, PICTURE; Ogg Vorbis: the comment header,
// the last page's granule; WAV: fmt, data, LIST INFO; MIDI: the first track's name, its length.
// What is missing comes from the path: the file's name (a leading "03 - " is the track), the folder
// (the album), the folder above (the artist).
//
#ifndef _media_tags_h
#define _media_tags_h

#include "decode.h"

namespace media {

enum { FMT_MP3, FMT_OGG, FMT_FLAC, FMT_WAV, FMT_MIDI, FMT_N };
static const char *const FMT_NAME[FMT_N] = { "MP3", "OGG", "FLAC", "WAV", "MIDI" };

static inline int format_of (const char *path)
{
	const char *e = ext_of (path);
	if (!strcasecmp (e, "mp3")) return FMT_MP3;
	if (!strcasecmp (e, "ogg") || !strcasecmp (e, "oga")) return FMT_OGG;
	if (!strcasecmp (e, "flac")) return FMT_FLAC;
	if (!strcasecmp (e, "wav")) return FMT_WAV;
	if (!strcasecmp (e, "mid") || !strcasecmp (e, "midi") || !strcasecmp (e, "kar") || !strcasecmp (e, "rmi")) return FMT_MIDI;
	return -1;
}

struct Tags
{
	char title[128], artist[96], albumArtist[96], album[128], genre[48];
	int year, track, disc, durMs, fmt;
	u64 coverOff; unsigned coverLen;		// a picture inside the file (0: none)
	void clear () { memset (this, 0, sizeof *this); fmt = -1; }
};

// ---- text ---------------------------------------------------------------------------------------------
static inline void put_utf8 (char *out, int cap, int *k, unsigned c)
{
	if (c == 0) return;
	if (c < 0x80) { if (*k + 1 < cap) out[(*k)++] = (char) c; }
	else if (c < 0x800) { if (*k + 2 < cap) { out[(*k)++] = (char) (0xC0 | c >> 6); out[(*k)++] = (char) (0x80 | (c & 63)); } }
	else if (c < 0x10000) { if (*k + 3 < cap) { out[(*k)++] = (char) (0xE0 | c >> 12); out[(*k)++] = (char) (0x80 | (c >> 6 & 63)); out[(*k)++] = (char) (0x80 | (c & 63)); } }
	else if (*k + 4 < cap) { out[(*k)++] = (char) (0xF0 | c >> 18); out[(*k)++] = (char) (0x80 | (c >> 12 & 63)); out[(*k)++] = (char) (0x80 | (c >> 6 & 63)); out[(*k)++] = (char) (0x80 | (c & 63)); }
}
// trim spaces at both ends, control characters (a tab: a space)
static inline void tidy (char *s)
{
	int k = 0, n = (int) strlen (s);
	for (int i = 0; i < n; i++) { unsigned char c = (unsigned char) s[i]; if (c == '\t' || c == '\n' || c == '\r') c = ' '; if (c >= 32 || c >= 0x80) s[k++] = (char) c; }
	s[k] = 0;
	while (k > 0 && s[k - 1] == ' ') s[--k] = 0;
	int a = 0; while (s[a] == ' ') a++;
	if (a) memmove (s, s + a, (size_t) (k - a + 1));
}
// ID3's text: enc 0 Latin-1, 1 UTF-16 with a BOM, 2 UTF-16BE, 3 UTF-8 -> UTF-8 (the first string)
static inline void id3_text (const unsigned char *p, int n, int enc, char *out, int cap)
{
	int k = 0;
	if (enc == 0) { for (int i = 0; i < n && p[i]; i++) put_utf8 (out, cap, &k, p[i]); }
	else if (enc == 3) { for (int i = 0; i < n && p[i] && k < cap - 1; i++) out[k++] = (char) p[i]; }
	else
	{
		bool be = enc == 2; int i = 0;
		if (enc == 1 && n >= 2) { if (p[0] == 0xFE && p[1] == 0xFF) { be = true; i = 2; } else if (p[0] == 0xFF && p[1] == 0xFE) { be = false; i = 2; } }
		for (; i + 1 < n; i += 2)
		{
			unsigned c = be ? (unsigned) p[i] << 8 | p[i + 1] : (unsigned) p[i + 1] << 8 | p[i];
			if (c == 0) break;
			if (c >= 0xD800 && c < 0xDC00 && i + 3 < n)
			{
				unsigned d = be ? (unsigned) p[i + 2] << 8 | p[i + 3] : (unsigned) p[i + 3] << 8 | p[i + 2];
				c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i += 2;
			}
			put_utf8 (out, cap, &k, c);
		}
	}
	out[k] = 0; tidy (out);
}
// the length of an ID3 string (with its terminator) in its encoding
static inline int id3_strlen (const unsigned char *p, int n, int enc)
{
	if (enc == 1 || enc == 2) { for (int i = 0; i + 1 < n; i += 2) if (!p[i] && !p[i + 1]) return i + 2; return n; }
	for (int i = 0; i < n; i++) if (!p[i]) return i + 1;
	return n;
}
static inline int leading_int (const char *s) { int v = 0; while (*s == ' ') s++; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }
static inline void scopy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

static const char *const ID3_GENRES[] = { "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal",
	"New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska", "Death Metal", "Pranks",
	"Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental", "Acid", "House",
	"Game", "Sound Clip", "Gospel", "Noise", "Alternative Rock", "Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop",
	"Instrumental Rock", "Ethnic", "Gothic", "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream", "Southern Rock",
	"Comedy", "Cult", "Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American", "Cabaret", "New Wave", "Psychedelic",
	"Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock", "Folk" };
static inline void fix_genre (char *g, int cap)
{	// "(17)", "17", "(17)Rock": the name
	const char *p = g; if (*p == '(') p++;
	if (*p >= '0' && *p <= '9')
	{
		int n = leading_int (p);
		const char *q = p; while (*q >= '0' && *q <= '9') q++; if (*q == ')') q++;
		if (*q) { char t[48]; scopy (t, q, sizeof t); scopy (g, t, cap); }
		else if (n < (int) (sizeof ID3_GENRES / sizeof *ID3_GENRES)) scopy (g, ID3_GENRES[n], cap);
		else g[0] = 0;
	}
}

// ---- MP3 ---------------------------------------------------------------------------------------------
static inline unsigned syncsafe (const unsigned char *p) { return (unsigned) (p[0] & 127) << 21 | (p[1] & 127) << 14 | (p[2] & 127) << 7 | (p[3] & 127); }
static inline unsigned be32u (const unsigned char *p) { return (unsigned) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static inline unsigned le32u (const unsigned char *p) { return (unsigned) p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

static void id3v2_frame (Tags *t, const char *id, const unsigned char *d, int n, u64 fileOff)
{
	if (n <= 0) return;
	int enc = d[0];
	char *dst = 0; int cap = 0;
	if (!strcmp (id, "TIT2") || !strcmp (id, "TT2")) { dst = t->title; cap = sizeof t->title; }
	else if (!strcmp (id, "TPE1") || !strcmp (id, "TP1")) { dst = t->artist; cap = sizeof t->artist; }
	else if (!strcmp (id, "TPE2") || !strcmp (id, "TP2")) { dst = t->albumArtist; cap = sizeof t->albumArtist; }
	else if (!strcmp (id, "TALB") || !strcmp (id, "TAL")) { dst = t->album; cap = sizeof t->album; }
	else if (!strcmp (id, "TCON") || !strcmp (id, "TCO")) { dst = t->genre; cap = sizeof t->genre; }
	if (dst) { id3_text (d + 1, n - 1, enc, dst, cap); if (dst == t->genre) fix_genre (t->genre, sizeof t->genre); return; }
	char tmp[32];
	if (!strcmp (id, "TRCK") || !strcmp (id, "TRK")) { id3_text (d + 1, n - 1, enc, tmp, sizeof tmp); t->track = leading_int (tmp); return; }
	if (!strcmp (id, "TPOS") || !strcmp (id, "TPA")) { id3_text (d + 1, n - 1, enc, tmp, sizeof tmp); t->disc = leading_int (tmp); return; }
	if (!strcmp (id, "TYER") || !strcmp (id, "TDRC") || !strcmp (id, "TYE")) { id3_text (d + 1, n - 1, enc, tmp, sizeof tmp); if (!t->year) t->year = leading_int (tmp); return; }
	if ((!strcmp (id, "APIC") || !strcmp (id, "PIC")) && !t->coverLen)
	{
		int p = 1;
		if (id[3]) p += id3_strlen (d + p, n - p, 0);	// the MIME type (APIC); PIC: three letters
		else p += 3;
		if (p >= n) return;
		int type = d[p++];
		p += id3_strlen (d + p, n - p, enc);		// the description
		if (p < n && (type == 3 || type == 0 || !t->coverLen)) { t->coverOff = fileOff + (u64) p; t->coverLen = (unsigned) (n - p); }
	}
}
// The ID3v2 tag at the file's start -> its whole size (0: none)
static unsigned read_id3v2 (Src &s, Tags *t)
{
	unsigned char h[10];
	s.seek (0);
	if (s.read (h, 10) != 10 || memcmp (h, "ID3", 3)) return 0;
	int ver = h[3]; bool unsync = (h[5] & 0x80) != 0;
	unsigned size = syncsafe (h + 6) + 10 + ((h[5] & 0x10) ? 10 : 0);
	unsigned body = syncsafe (h + 6);
	if (body > (64u << 20)) return size;
	unsigned char *b = new unsigned char[body + 1];
	unsigned got = (unsigned) s.read (b, body);
	unsigned p = 0;
	if ((h[5] & 0x40) && ver >= 3 && got >= 4) p += ver == 4 ? syncsafe (b) : be32u (b) + 4;	// the extended header
	while (p + (ver == 2 ? 6u : 10u) <= got)
	{
		char id[5] = { 0 }; unsigned n; int hl;
		if (ver == 2) { memcpy (id, b + p, 3); n = (unsigned) b[p + 3] << 16 | b[p + 4] << 8 | b[p + 5]; hl = 6; }
		else { memcpy (id, b + p, 4); n = ver == 4 ? syncsafe (b + p + 4) : be32u (b + p + 4); hl = 10; }
		if (!id[0] || id[0] < 'A' || id[0] > 'Z') break;
		if (p + hl + n > got) break;
		bool compressed = ver >= 3 && (b[p + 9] & (ver == 4 ? 0x0C : 0xC0));
		if (!compressed && !unsync) id3v2_frame (t, id, b + p + hl, (int) n, 10 + p + hl);
		else if (!compressed && strcmp (id, "APIC")) id3v2_frame (t, id, b + p + hl, (int) n, 10 + p + hl);	// (a picture kept only when not unsynchronised)
		p += hl + n;
	}
	delete[] b;
	return size;
}
static void read_id3v1 (Src &s, Tags *t)
{
	if (s.size < 128) return;
	unsigned char b[128];
	s.seek (s.size - 128);
	if (s.read (b, 128) != 128 || memcmp (b, "TAG", 3)) return;
	char x[32];
	if (!t->title[0]) { id3_text (b + 3, 30, 0, x, sizeof x); scopy (t->title, x, sizeof t->title); }
	if (!t->artist[0]) { id3_text (b + 33, 30, 0, x, sizeof x); scopy (t->artist, x, sizeof t->artist); }
	if (!t->album[0]) { id3_text (b + 63, 30, 0, x, sizeof x); scopy (t->album, x, sizeof t->album); }
	if (!t->year) { id3_text (b + 93, 4, 0, x, sizeof x); t->year = leading_int (x); }
	if (!t->track && b[125] == 0 && b[126]) t->track = b[126];
	if (!t->genre[0] && b[127] < sizeof ID3_GENRES / sizeof *ID3_GENRES) scopy (t->genre, ID3_GENRES[b[127]], sizeof t->genre);
}
// the length: the first frame's header, its Xing / Info / VBRI header, else the bit rate
static void mp3_length (Src &s, unsigned start, Tags *t)
{
	static const int BR[2][16] = { { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 },	// MPEG-1 layer III
				       { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 } };	// MPEG-2 / 2.5
	static const int SR[3] = { 44100, 48000, 32000 };
	unsigned char b[4096];
	s.seek (start);
	int n = (int) s.read (b, sizeof b);
	for (int i = 0; i + 4 <= n; i++)
	{
		if (b[i] != 0xFF || (b[i + 1] & 0xE0) != 0xE0) continue;
		int ver = (b[i + 1] >> 3) & 3, layer = (b[i + 1] >> 1) & 3, bri = b[i + 2] >> 4, sri = (b[i + 2] >> 2) & 3;
		if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3) continue;	// (layer III only)
		bool m1 = ver == 3;
		int rate = SR[sri] / (m1 ? 1 : ver == 2 ? 2 : 4), kbps = BR[m1 ? 0 : 1][bri];
		bool mono = (b[i + 3] >> 6) == 3;
		int spf = m1 ? 1152 : 576;
		int side = m1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
		int x = i + 4 + side;
		if (x + 16 <= n && (!memcmp (b + x, "Xing", 4) || !memcmp (b + x, "Info", 4)) && (b[x + 7] & 1))
		{ unsigned frames = be32u (b + x + 8); t->durMs = (int) ((u64) frames * spf * 1000 / rate); return; }
		x = i + 4 + 32;
		if (x + 18 <= n && !memcmp (b + x, "VBRI", 4)) { unsigned frames = be32u (b + x + 14); t->durMs = (int) ((u64) frames * spf * 1000 / rate); return; }
		u64 bytes = s.size - start; if (s.size >= 128) { /* an ID3v1 tag: not music */ }
		t->durMs = (int) (bytes * 8 / (u64) kbps);
		return;
	}
}

// ---- Vorbis comments (FLAC, Ogg) ---------------------------------------------------------------------------
static void vorbis_comments (const unsigned char *d, unsigned n, Tags *t)
{
	if (n < 8) return;
	unsigned p = 4 + le32u (d);			// the vendor
	if (p + 4 > n) return;
	unsigned cnt = le32u (d + p); p += 4;
	for (unsigned i = 0; i < cnt && p + 4 <= n; i++)
	{
		unsigned l = le32u (d + p); p += 4;
		if (p + l > n) break;
		const char *c = (const char *) d + p;
		const char *eq = (const char *) memchr (c, '=', l);
		if (eq)
		{
			int kl = (int) (eq - c); int vl = (int) l - kl - 1;
			char v[160]; int k = vl < (int) sizeof v - 1 ? vl : (int) sizeof v - 1; memcpy (v, eq + 1, (size_t) k); v[k] = 0; tidy (v);
			#define KEY(s) (kl == (int) sizeof (s) - 1 && !strncasecmp (c, s, (size_t) kl))
			if (KEY ("TITLE")) scopy (t->title, v, sizeof t->title);
			else if (KEY ("ARTIST") && !t->artist[0]) scopy (t->artist, v, sizeof t->artist);
			else if (KEY ("ALBUMARTIST") || KEY ("ALBUM ARTIST") || KEY ("ALBUM_ARTIST")) scopy (t->albumArtist, v, sizeof t->albumArtist);
			else if (KEY ("ALBUM")) scopy (t->album, v, sizeof t->album);
			else if (KEY ("GENRE") && !t->genre[0]) scopy (t->genre, v, sizeof t->genre);
			else if (KEY ("DATE") || KEY ("YEAR")) { if (!t->year) t->year = leading_int (v); }
			else if (KEY ("TRACKNUMBER")) t->track = leading_int (v);
			else if (KEY ("DISCNUMBER")) t->disc = leading_int (v);
			#undef KEY
		}
		p += l;
	}
}
static void read_flac (Src &s, Tags *t)
{
	unsigned char h[4];
	s.seek (0);
	if (s.read (h, 4) != 4) return;
	if (!memcmp (h, "ID3", 3)) { unsigned sz = read_id3v2 (s, t); s.seek (sz); if (s.read (h, 4) != 4) return; }
	if (memcmp (h, "fLaC", 4)) return;
	for (int guard = 0; guard < 64; guard++)
	{
		unsigned char bh[4];
		if (s.read (bh, 4) != 4) return;
		int type = bh[0] & 127; bool last = (bh[0] & 128) != 0;
		unsigned len = (unsigned) bh[1] << 16 | bh[2] << 8 | bh[3];
		u64 at = s.pos;
		if (type == 0 && len >= 18)
		{
			unsigned char si[18]; s.read (si, 18);
			unsigned rate = (unsigned) si[10] << 12 | si[11] << 4 | si[12] >> 4;
			u64 total = (u64) (si[13] & 15) << 32 | be32u (si + 14);
			if (rate) t->durMs = (int) (total * 1000 / rate);
		}
		else if (type == 4 && len < (4u << 20))
		{
			unsigned char *d = new unsigned char[len]; s.read (d, len); vorbis_comments (d, len, t); delete[] d;
		}
		else if (type == 6 && !t->coverLen && len >= 32)
		{
			unsigned char ph[8]; s.read (ph, 8);			// type, MIME length
			unsigned ml = be32u (ph + 4); s.seek (s.pos + ml);
			unsigned char dl[4]; s.read (dl, 4); s.seek (s.pos + be32u (dl) + 16);	// the description; w h depth colours
			unsigned char ll[4]; s.read (ll, 4);
			t->coverOff = s.pos; t->coverLen = be32u (ll);
		}
		if (last) return;
		s.seek (at + len);
	}
}

// ---- Ogg Vorbis ---------------------------------------------------------------------------------------------
static void read_ogg (Src &s, Tags *t)
{
	unsigned n = s.size < 65536 ? (unsigned) s.size : 65536;
	unsigned char *b = new unsigned char[n];
	s.seek (0); n = (unsigned) s.read (b, n);
	unsigned rate = 0;
	for (unsigned i = 0; i + 7 < n; i++)
	{
		if (b[i] == 1 && !memcmp (b + i + 1, "vorbis", 6) && i + 16 <= n && !rate) rate = le32u (b + i + 12);
		if (b[i] == 3 && !memcmp (b + i + 1, "vorbis", 6))
		{	// (the comment packet, as far as the first 64 KB go -- across pages: their headers in the way are rare
			//  in so small a packet; a cover picture inside would make it long: not read)
			vorbis_comments (b + i + 7, n - i - 7, t);
			break;
		}
	}
	delete[] b;
	// the length: the last page's granule position
	if (rate && s.size > 27)
	{
		unsigned m = s.size < 65536 ? (unsigned) s.size : 65536;
		unsigned char *e = new unsigned char[m];
		s.seek (s.size - m); m = (unsigned) s.read (e, m);
		for (int i = (int) m - 27; i >= 0; i--)
			if (!memcmp (e + i, "OggS", 4))
			{
				u64 g = 0; for (int k = 7; k >= 0; k--) g = g << 8 | e[i + 6 + k];
				if (g != ~0ull) { t->durMs = (int) (g * 1000 / rate); break; }
			}
		delete[] e;
	}
}

// ---- WAV ------------------------------------------------------------------------------------------------------
static void read_wav (Src &s, Tags *t)
{
	unsigned char h[12];
	s.seek (0);
	if (s.read (h, 12) != 12 || memcmp (h, "RIFF", 4) || memcmp (h + 8, "WAVE", 4)) return;
	unsigned rate = 0, bpf = 0;
	for (int guard = 0; guard < 64; guard++)
	{
		unsigned char ch[8];
		if (s.read (ch, 8) != 8) break;
		unsigned len = le32u (ch + 4); u64 at = s.pos;
		if (!memcmp (ch, "fmt ", 4) && len >= 16) { unsigned char f[16]; s.read (f, 16); rate = le32u (f + 4); bpf = (unsigned) f[12] | f[13] << 8; }
		else if (!memcmp (ch, "data", 4) && rate && bpf) t->durMs = (int) ((u64) len / bpf * 1000 / rate);
		else if (!memcmp (ch, "LIST", 4) && len >= 4 && len < (1u << 20))
		{
			unsigned char *d = new unsigned char[len]; s.read (d, len);
			if (!memcmp (d, "INFO", 4))
				for (unsigned p = 4; p + 8 <= len; )
				{
					unsigned l = le32u (d + p + 4); if (p + 8 + l > len) break;
					char v[128]; id3_text (d + p + 8, (int) l, 0, v, sizeof v);
					if (!memcmp (d + p, "INAM", 4)) scopy (t->title, v, sizeof t->title);
					else if (!memcmp (d + p, "IART", 4)) scopy (t->artist, v, sizeof t->artist);
					else if (!memcmp (d + p, "IPRD", 4)) scopy (t->album, v, sizeof t->album);
					else if (!memcmp (d + p, "IGNR", 4)) scopy (t->genre, v, sizeof t->genre);
					else if (!memcmp (d + p, "ICRD", 4)) t->year = leading_int (v);
					else if (!memcmp (d + p, "ITRK", 4) || !memcmp (d + p, "IPRT", 4)) t->track = leading_int (v);
					p += 8 + ((l + 1) & ~1u);
				}
			delete[] d;
		}
		s.seek (at + len + (len & 1));
	}
}

// ---- the path's part --------------------------------------------------------------------------------------------
static void from_path (const char *path, Tags *t)
{
	const char *e = path + strlen (path);
	const char *b = e; while (b > path && b[-1] != '/' && b[-1] != ':') b--;
	if (!t->title[0])
	{
		char n[128]; int k = 0;
		const char *dot = strrchr (b, '.'); if (!dot) dot = e;
		for (const char *p = b; p < dot && k < (int) sizeof n - 1; p++) n[k++] = *p == '_' ? ' ' : *p;
		n[k] = 0;
		// "03 - Title", "03. Title", "03 Title": the track
		const char *q = n; int tr = 0, digits = 0;
		while (*q >= '0' && *q <= '9' && digits < 3) { tr = tr * 10 + (*q - '0'); q++; digits++; }
		if (digits && (*q == ' ' || *q == '.' || *q == '-' || *q == '_'))
		{
			while (*q == ' ' || *q == '.' || *q == '-' || *q == '_') q++;
			if (*q) { if (!t->track) t->track = tr; memmove (n, q, strlen (q) + 1); }
		}
		tidy (n); scopy (t->title, n, sizeof t->title);
	}
	// the folder: the album; the one above: the artist
	if (b > path)
	{
		const char *f1e = b - 1, *f1 = f1e; while (f1 > path && f1[-1] != '/' && f1[-1] != ':') f1--;
		char f[128]; int k = 0; for (const char *p = f1; p < f1e && k < (int) sizeof f - 1; p++) f[k++] = *p; f[k] = 0;
		bool root = !strcasecmp (f, "Music") || !strcasecmp (f, "Musique") || !f[0];
		if (!t->album[0] && !root) scopy (t->album, f, sizeof t->album);
		if (!t->artist[0] && f1 > path && !root)
		{
			const char *f2e = f1 - 1, *f2 = f2e; while (f2 > path && f2[-1] != '/' && f2[-1] != ':') f2--;
			k = 0; for (const char *p = f2; p < f2e && k < (int) sizeof f - 1; p++) f[k++] = *p; f[k] = 0;
			if (f[0] && strcasecmp (f, "Music") && strcasecmp (f, "Musique")) scopy (t->artist, f, sizeof t->artist);
		}
	}
}

// Everything known of the file at path -> false: not a song of ours / unreadable.
static bool read_tags (const char *path, Tags *t)
{
	t->clear ();
	t->fmt = format_of (path);
	if (t->fmt < 0) return false;
	if (t->fmt == FMT_MIDI)
	{
		MidiSong m; char err[64];
		if (!m.load (path, err, sizeof err)) return false;
		scopy (t->title, m.title, sizeof t->title); tidy (t->title);
		t->durMs = (int) (m.length * 1000 / SOUND_RATE) + 1000;
		scopy (t->genre, "MIDI", sizeof t->genre);
	}
	else
	{
		Src s; if (!s.open (path)) return false;
		switch (t->fmt)
		{
		case FMT_MP3: { unsigned st = read_id3v2 (s, t); read_id3v1 (s, t); mp3_length (s, st, t); break; }
		case FMT_FLAC: read_flac (s, t); break;
		case FMT_OGG: read_ogg (s, t); break;
		case FMT_WAV: read_wav (s, t); break;
		}
	}
	from_path (path, t);
	return true;
}

} // namespace media

#endif
