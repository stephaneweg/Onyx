//
// fms.h -- the FM song model of fmtracker and its file formats (portable C++: new / delete
// only, so tools/tests/run_fms_test.sh builds it on a PC too).
//
// .FMS ("fm-song-project", FM Song for QBasic / AdLib, 2001): binary, little endian --
//   "fm-song-project" (15) | title (20) | author (20) | comment (50) | patterns (s16)
//   8 x instrument: name (8) + 26 parameter bytes (below)
//   per pattern: rows (s16) | speed (s16, a row lasts speed / 20 s) | 8 mute flags (1 = muted)
//                8 channels x rows note bytes
// A note byte: 0 = nothing new (the note goes on, shown "---"), 128 = silence (key off,
// shown empty), else sharp * 64 + octave * 8 + note (1..7 = C D E F G A B).
//
// The 26 instrument bytes are pairs (operator 1 = modulator, operator 2 = carrier, as the
// AdLib's OPL2 registers): attack, decay, sustain level, release, output level (attenuation),
// key scale level, "amplitude vibrato" (OPL VIB bit), "pitch vibrato" (OPL AM bit),
// multiplier, key scale rate, wave; then feedback, connection; then sustained (EG type).
// .FMI (one instrument, text): "fm-song instrument", "NAME", then the same pairs as
// "a,b" lines, but sustained comes BEFORE "feedback,connection".
//
#ifndef _fms_h
#define _fms_h

#include <kern/kapi_abi.h>		// struct kapi_fm_instrument

#define FMS_CH		8
#define FMS_MAXPAT	64
#define FMS_MAXROWS	256
#define FMS_CONT	0		// note byte: nothing new
#define FMS_OFF		128		// note byte: silence

enum { P_AR = 0, P_DR = 2, P_SL = 4, P_RR = 6, P_TL = 8, P_KSL = 10, P_VIB = 12, P_AM = 14,
       P_MULT = 16, P_KSR = 18, P_WAVE = 20, P_FB = 22, P_CON = 23, P_EGT = 24 };

struct FmsIns { char name[9]; unsigned char p[26]; };
struct FmsPattern { int rows, speed; unsigned char mute[FMS_CH]; unsigned char *n; };	// n[ch * rows + row]
struct FmsSong
{
	char title[21], author[21], comment[51];
	int npat;
	FmsPattern pat[FMS_MAXPAT];
	FmsIns ins[FMS_CH];
};

static inline void fms_copy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline int  fms_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }

// A decent default instrument (a soft electric piano).
static inline void fms_default_ins (FmsIns *in)
{
	static const unsigned char p[26] = { 15, 15, 1, 2, 5, 7, 3, 4, 20, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 3, 0, 1, 0 };
	fms_copy (in->name, "EPIANO", sizeof in->name);
	for (int i = 0; i < 26; i++) in->p[i] = p[i];
}

static inline bool fms_pattern_alloc (FmsPattern *pt, int rows, int speed)
{
	if (rows < 1) rows = 1;
	if (rows > FMS_MAXROWS) rows = FMS_MAXROWS;
	pt->rows = rows; pt->speed = speed < 1 ? 1 : speed;
	for (int c = 0; c < FMS_CH; c++) pt->mute[c] = 0;
	pt->n = new unsigned char[FMS_CH * rows];
	for (int i = 0; i < FMS_CH * rows; i++) pt->n[i] = FMS_CONT;
	return pt->n != 0;
}
// Change the row count, keeping the notes.
static inline void fms_pattern_resize (FmsPattern *pt, int rows)
{
	if (rows < 1) rows = 1;
	if (rows > FMS_MAXROWS) rows = FMS_MAXROWS;
	if (rows == pt->rows) return;
	unsigned char *n = new unsigned char[FMS_CH * rows];
	for (int c = 0; c < FMS_CH; c++)
		for (int r = 0; r < rows; r++) n[c * rows + r] = r < pt->rows ? pt->n[c * pt->rows + r] : FMS_CONT;
	delete [] pt->n; pt->n = n; pt->rows = rows;
}

static inline void fms_clear (FmsSong *s)
{
	for (int i = 0; i < s->npat; i++) { delete [] s->pat[i].n; s->pat[i].n = 0; }
	s->npat = 0;
}
static inline void fms_new (FmsSong *s)
{
	fms_copy (s->title, "Untitled", sizeof s->title); s->author[0] = 0; s->comment[0] = 0;
	s->npat = 1;
	fms_pattern_alloc (&s->pat[0], 64, 3);
	for (int c = 0; c < FMS_CH; c++) fms_default_ins (&s->ins[c]);
}

// ---- .FMS ----------------------------------------------------------------------------------------
static inline void fms_field (char *dst, int cap, const unsigned char *src, int n)	// padded -> trimmed
{
	int k = 0;
	for (int i = 0; i < n && k < cap - 1; i++) dst[k++] = src[i] >= 32 ? (char) src[i] : ' ';
	while (k > 0 && dst[k - 1] == ' ') k--;
	dst[k] = 0;
}
static inline int fms_s16 (const unsigned char *p) { return (short) (p[0] | (p[1] << 8)); }

// Parse a whole .FMS file; false if it is not one. (s must be empty: fms_clear first.)
static inline bool fms_parse (const unsigned char *d, int n, FmsSong *s)
{
	const char *magic = "fm-song-project";
	if (n < 107 + FMS_CH * 34) return false;
	for (int i = 0; i < 15; i++) if (d[i] != (unsigned char) magic[i]) return false;
	int p = 15;
	fms_field (s->title, sizeof s->title, d + p, 20); p += 20;
	fms_field (s->author, sizeof s->author, d + p, 20); p += 20;
	fms_field (s->comment, sizeof s->comment, d + p, 50); p += 50;
	int npat = fms_s16 (d + p); p += 2;
	if (npat < 1 || npat > FMS_MAXPAT) return false;
	for (int c = 0; c < FMS_CH; c++)
	{
		fms_field (s->ins[c].name, sizeof s->ins[c].name, d + p, 8); p += 8;
		for (int i = 0; i < 26; i++) s->ins[c].p[i] = d[p + i];
		p += 26;
	}
	s->npat = 0;
	for (int t = 0; t < npat; t++)
	{
		if (p + 12 > n) return s->npat > 0;
		int rows = fms_s16 (d + p), speed = fms_s16 (d + p + 2); p += 4;
		if (rows < 1 || rows > FMS_MAXROWS) return s->npat > 0;
		FmsPattern &pt = s->pat[s->npat];
		fms_pattern_alloc (&pt, rows, speed);
		for (int c = 0; c < FMS_CH; c++) pt.mute[c] = d[p + c] ? 1 : 0;
		p += 8;
		for (int c = 0; c < FMS_CH; c++)
			for (int r = 0; r < rows; r++) pt.n[c * rows + r] = p < n ? d[p++] : FMS_CONT;
		s->npat++;
	}
	return true;
}

// Serialise; returns the size (out must hold fms_size (s) bytes).
static inline int fms_size (const FmsSong *s)
{
	int n = 107 + FMS_CH * 34;
	for (int t = 0; t < s->npat; t++) n += 12 + FMS_CH * s->pat[t].rows;
	return n;
}
static inline int fms_write (const FmsSong *s, unsigned char *o)
{
	int p = 0;
	const char *magic = "fm-song-project";
	for (int i = 0; i < 15; i++) o[p++] = (unsigned char) magic[i];
	auto pad = [&] (const char *str, int w) { int l = fms_len (str); for (int i = 0; i < w; i++) o[p++] = i < l ? (unsigned char) str[i] : ' '; };
	auto s16 = [&] (int v) { o[p++] = (unsigned char) (v & 255); o[p++] = (unsigned char) ((v >> 8) & 255); };
	pad (s->title, 20); pad (s->author, 20); pad (s->comment, 50);
	s16 (s->npat);
	for (int c = 0; c < FMS_CH; c++) { pad (s->ins[c].name, 8); for (int i = 0; i < 26; i++) o[p++] = s->ins[c].p[i]; }
	for (int t = 0; t < s->npat; t++)
	{
		const FmsPattern &pt = s->pat[t];
		s16 (pt.rows); s16 (pt.speed);
		for (int c = 0; c < FMS_CH; c++) o[p++] = pt.mute[c];
		for (int c = 0; c < FMS_CH; c++) for (int r = 0; r < pt.rows; r++) o[p++] = pt.n[c * pt.rows + r];
	}
	return p;
}

// ---- .FMI ----------------------------------------------------------------------------------------
// FMI line order (pair index into p[]): AR DR SL RR TL KSL VIB AM MULT KSR WAVE EGT (FB,CON)
static const int FMI_ORDER[12] = { P_AR, P_DR, P_SL, P_RR, P_TL, P_KSL, P_VIB, P_AM, P_MULT, P_KSR, P_WAVE, P_EGT };

static inline bool fmi_parse (const char *t, FmsIns *in)
{
	int line = 0, pairs = 0;
	const char *p = t;
	char name[16] = "";
	while (*p && pairs < 13)
	{
		const char *e = p; while (*e && *e != '\n') e++;
		char l[96]; int k = 0;
		for (const char *q = p; q < e && k < 95; q++) if (*q != '\r' && *q != '"') l[k++] = *q;
		l[k] = 0;
		if (line == 0) { const char *m = "fm-song instrument"; for (int i = 0; m[i]; i++) if (l[i] != m[i]) return false; }
		else if (line == 1) { int j = 0; while (l[j] == ' ') j++; fms_copy (name, l + j, 9); }
		else if (k)
		{
			int a = 0, b = 0, i = 0;
			while (l[i] == ' ') i++;
			while (l[i] >= '0' && l[i] <= '9') a = a * 10 + (l[i++] - '0');
			while (l[i] == ' ' || l[i] == ',') i++;
			while (l[i] >= '0' && l[i] <= '9') b = b * 10 + (l[i++] - '0');
			if (pairs < 12) { in->p[FMI_ORDER[pairs]] = (unsigned char) a; in->p[FMI_ORDER[pairs] + 1] = (unsigned char) b; }
			else { in->p[P_FB] = (unsigned char) a; in->p[P_CON] = (unsigned char) b; }
			pairs++;
		}
		line++;
		p = *e ? e + 1 : e;
	}
	if (pairs < 13) return false;
	int n = fms_len (name); while (n > 0 && name[n - 1] == ' ') name[--n] = 0;
	fms_copy (in->name, name, sizeof in->name);
	return true;
}
static inline int fmi_write (const FmsIns *in, char *o)
{
	int p = 0;
	auto str = [&] (const char *s) { for (int i = 0; s[i]; i++) o[p++] = s[i]; };
	auto num = [&] (int v) { char b[8]; int k = 0; if (!v) b[k++] = '0'; while (v) { b[k++] = (char) ('0' + v % 10); v /= 10; } while (k) o[p++] = b[--k]; };
	str ("\"fm-song instrument\"\r\n\""); str (in->name); str ("\"\r\n");
	for (int i = 0; i < 12; i++) { num (in->p[FMI_ORDER[i]]); o[p++] = ','; num (in->p[FMI_ORDER[i] + 1]); str ("\r\n"); }
	num (in->p[P_FB]); o[p++] = ','; num (in->p[P_CON]); str ("\r\n");
	o[p] = 0;
	return p;
}

// ---- to the synthesizer ----------------------------------------------------------------------------
static inline void fms_to_kapi (const FmsIns *in, struct kapi_fm_instrument *k)
{
	for (int o = 0; o < 2; o++)
	{
		struct kapi_fm_op &op = k->op[o];
		op.attack = in->p[P_AR + o] & 15; op.decay = in->p[P_DR + o] & 15;
		op.sustain = in->p[P_SL + o] & 15; op.release = in->p[P_RR + o] & 15;
		op.level = in->p[P_TL + o] & 63; op.ksl = in->p[P_KSL + o] & 3;
		op.mult = in->p[P_MULT + o] & 15; op.wave = in->p[P_WAVE + o] & 3;
		op.flags = (in->p[P_EGT + o] ? FM_SUSTAINED : 0) | (in->p[P_AM + o] ? FM_TREMOLO : 0)
			 | (in->p[P_VIB + o] ? FM_VIBRATO : 0) | (in->p[P_KSR + o] ? FM_KSR : 0);
	}
	k->feedback = in->p[P_FB] & 7; k->connection = in->p[P_CON] & 1;
}

// ---- notes -------------------------------------------------------------------------------------------
// The pitch of a note byte, in milli-Hz: standard tuning (A4 = 440 Hz, C4 = middle C
// = 261.63 Hz, equal temperament). (FM Song's own AdLib F-number table played everything a
// semitone higher; its octave numbers are kept.)
static inline unsigned fms_note_mhz (unsigned char v)
{
	static const unsigned oct4[12] = { 261626, 277183, 293665, 311127, 329628, 349228,
					   369994, 391995, 415305, 440000, 466164, 493883 };
	static const int semi[8] = { 0, 0, 2, 4, 5, 7, 9, 11 };	// (index 1..7 = C..B)
	int note = v & 7, oct = (v >> 3) & 7, sharp = (v >> 6) & 1;
	if (note == 0 || (v & 128)) return 0;
	int s = semi[note] + sharp;
	if (s >= 12) { s -= 12; oct++; }
	unsigned f = oct4[s];
	return oct >= 4 ? f << (oct - 4) : f >> (4 - oct);
}
static inline unsigned char fms_make_note (int note /*1..7*/, int sharp, int oct)
{
	if (sharp && (note == 3 || note == 7))			// E# = F, B# = the next C
	{
		sharp = 0;
		if (note == 3) note = 4; else { note = 1; if (oct < 7) oct++; }
	}
	return (unsigned char) ((sharp ? 64 : 0) + (oct & 7) * 8 + (note & 7));
}
// A note moved by delta semitones (octaves 0..7); anything else is returned unchanged.
static inline unsigned char fms_transpose (unsigned char v, int delta)
{
	static const int semi[8] = { 0, 0, 2, 4, 5, 7, 9, 11 };
	static const unsigned char note[12] = { 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7 }, sharp[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
	if ((v & 128) || (v & 7) == 0) return v;
	int p = ((v >> 3) & 7) * 12 + semi[v & 7] + ((v >> 6) & 1) + delta;
	if (p < 0) p = 0;
	if (p > 8 * 12 - 1) p = 8 * 12 - 1;
	return (unsigned char) (sharp[p % 12] * 64 + (p / 12) * 8 + note[p % 12]);
}

// "C#4" / "D-3" / "---" / "" (silence)
static inline void fms_note_text (unsigned char v, char *o)
{
	if (v & 128) { o[0] = 0; return; }			// silence
	if ((v & 7) == 0) { o[0] = o[1] = o[2] = '-'; o[3] = 0; return; }	// goes on
	o[0] = "?CDEFGAB"[v & 7]; o[1] = (v & 64) ? '#' : '-'; o[2] = (char) ('0' + ((v >> 3) & 7)); o[3] = 0;
}

#endif
