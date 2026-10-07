//
// emoticons.h -- the Telegram app's emoticons, in the way of the old messengers: the emoji Telegram's
// text carries (and the typed ":)", ";)", "(Y)", "<3"... of MSN's days) drawn as little pictures, by
// vectors (uikit/vpaint.h) at any size -- no picture file, no other licence. An emoji with no picture of
// its own here is drawn as a small round badge; the joiners and variation selectors are skipped.
//
//   int id, len;
//   if ((id = emo_at (text, n, &len, true)) >= 0) { emo_draw (cv, id, x, y, 18); text += len; }
//
// MIT licence.
//
#ifndef TG_EMOTICONS_H
#define TG_EMOTICONS_H

#include "uikit/uikit.h"
#include "uikit/vpaint.h"

using namespace uikit;

enum
{
	EMO_SMILE, EMO_GRIN, EMO_WINK, EMO_SAD, EMO_CRY, EMO_TONGUE, EMO_SURPRISED, EMO_ANGRY, EMO_COOL, EMO_LAUGH,
	EMO_LOVE, EMO_KISS, EMO_CONFUSED, EMO_NEUTRAL, EMO_BLUSH, EMO_THINK, EMO_HEART, EMO_BROKEN, EMO_THUMBUP,
	EMO_THUMBDOWN, EMO_STAR, EMO_SUN, EMO_COFFEE, EMO_ROSE, EMO_GIFT, EMO_PARTY, EMO_FIRE, EMO_CLAP, EMO_OK,
	EMO_PRAY, EMO_SLEEP, EMO_SICK,
	EMO_COUNT,			// (the picker's)
	EMO_OTHER = EMO_COUNT		// an emoji without a picture here: a badge
};

struct EmoInfo { unsigned cp; const char *text; const char *name; };	// the emoji sent, a typed form, its name

static const EmoInfo emo_info[EMO_COUNT] = {
	{ 0x1F642, ":)", TRN ("Smile") }, { 0x1F600, ":D", TRN ("Big smile") }, { 0x1F609, ";)", TRN ("Wink") }, { 0x1F641, ":(", TRN ("Sad") },
	{ 0x1F622, ":'(", TRN ("Crying") }, { 0x1F61B, ":P", TRN ("Tongue out") }, { 0x1F62E, ":O", TRN ("Surprised") }, { 0x1F620, ":@", TRN ("Angry") },
	{ 0x1F60E, "(H)", TRN ("Cool") }, { 0x1F602, 0, TRN ("Tears of joy") }, { 0x1F60D, 0, TRN ("In love") }, { 0x1F618, ":*", TRN ("Kiss") },
	{ 0x1F615, ":S", TRN ("Confused") }, { 0x1F610, ":|", TRN ("Neutral") }, { 0x1F633, ":$", TRN ("Embarrassed") }, { 0x1F914, 0, TRN ("Thinking") },
	{ 0x2764, "<3", TRN ("Heart") }, { 0x1F494, "</3", TRN ("Broken heart") }, { 0x1F44D, "(Y)", TRN ("Thumbs up") }, { 0x1F44E, "(N)", TRN ("Thumbs down") },
	{ 0x2B50, "(*)", TRN ("Star") }, { 0x2600, "(#)", TRN ("Sunshine") }, { 0x2615, "(C)", TRN ("Coffee") }, { 0x1F339, "(F)", TRN ("Rose") },
	{ 0x1F381, "(G)", TRN ("Gift") }, { 0x1F389, 0, TRN ("Party") }, { 0x1F525, 0, TRN ("Fire") }, { 0x1F44F, 0, TRN ("Applause") },
	{ 0x1F44C, 0, TRN ("OK") }, { 0x1F64F, 0, TRN ("Thank you") }, { 0x1F634, 0, TRN ("Sleepy") }, { 0x1F922, 0, TRN ("Sick") } };

// More code points drawn as one of the pictures.
static int emo_of_cp (unsigned cp)
{
	for (int i = 0; i < EMO_COUNT; i++) if (emo_info[i].cp == cp) return i;
	switch (cp)
	{
	case 0x263A: case 0x1F60A: case 0x1F607: case 0x1F917: case 0x1F643: return EMO_SMILE;
	case 0x1F603: case 0x1F604: case 0x1F601: case 0x1F606: case 0x1F605: case 0x1F929: case 0x1F973: return EMO_GRIN;
	case 0x1F61C: return EMO_WINK;
	case 0x2639: case 0x1F61E: case 0x1F614: case 0x1F61F: case 0x1F625: case 0x1F613: case 0x1F629: case 0x1F62B: return EMO_SAD;
	case 0x1F62D: case 0x1F97A: return EMO_CRY;
	case 0x1F61D: case 0x1F92A: return EMO_TONGUE;
	case 0x1F62F: case 0x1F632: case 0x1F631: case 0x1F628: case 0x1F630: case 0x1F92F: return EMO_SURPRISED;
	case 0x1F621: case 0x1F624: case 0x1F92C: case 0x1F47F: return EMO_ANGRY;
	case 0x1F923: return EMO_LAUGH;
	case 0x1F970: case 0x1F63B: return EMO_LOVE;
	case 0x1F617: case 0x1F619: case 0x1F61A: case 0x1F48B: return EMO_KISS;
	case 0x1F612: case 0x1F644: case 0x1F928: return EMO_CONFUSED;
	case 0x1F611: case 0x1F636: case 0x1F60F: return EMO_NEUTRAL;
	case 0x1F92D: case 0x1F648: return EMO_BLUSH;
	case 0x2665: case 0x1F496: case 0x1F497: case 0x1F493: case 0x1F495: case 0x1F49E: case 0x1F498: case 0x1F9E1:
	case 0x1F49B: case 0x1F49A: case 0x1F499: case 0x1F49C: case 0x1F5A4: case 0x1F90D: case 0x1F90E: case 0x2763: return EMO_HEART;
	case 0x1F31F: case 0x2728: case 0x1F320: case 0x2B51: return EMO_STAR;
	case 0x1F31E: case 0x1F324: case 0x26C5: return EMO_SUN;
	case 0x1F375: case 0x1F37A: case 0x1F377: return EMO_COFFEE;
	case 0x1F33A: case 0x1F337: case 0x1F338: case 0x1F490: case 0x1F33C: case 0x1F33B: return EMO_ROSE;
	case 0x1F38A: case 0x1F382: case 0x1F388: case 0x1F386: case 0x1F387: return EMO_PARTY;
	case 0x1F4A5: return EMO_FIRE;
	case 0x1F64C: case 0x1F44B: return EMO_CLAP;
	case 0x2705: case 0x2714: case 0x1F197: return EMO_OK;
	case 0x1F62A: case 0x1F971: case 0x1F4A4: return EMO_SLEEP;
	case 0x1F92E: case 0x1F637: case 0x1F912: case 0x1F915: return EMO_SICK;
	}
	return -1;
}

// An emoji's range: what the font cannot draw (a badge when it has no picture here).
static bool emo_range (unsigned cp)
{
	return (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) ||
	       (cp >= 0x1F000 && cp <= 0x1F2FF) || cp == 0x2764 || cp == 0x203C || cp == 0x2049;
}
// What is skipped after (or between the parts of) an emoji: variation selectors, the joiner, skin tones.
static bool emo_mod (unsigned cp) { return cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x20E3 || (cp >= 0x1F3FB && cp <= 0x1F3FF); }

static unsigned emo_u8 (const char *s, int n, int *len)
{
	const unsigned char *p = (const unsigned char *) s;
	unsigned c = p[0];
	int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
	if (k > n) k = 1;
	unsigned cp = k == 1 ? c : k == 2 ? c & 0x1F : k == 3 ? c & 0x0F : c & 0x07;
	for (int i = 1; i < k; i++) { if ((p[i] & 0xC0) != 0x80) { *len = 1; return c; } cp = (cp << 6) | (p[i] & 0x3F); }
	*len = k;
	return cp;
}

// An emoticon at s (n bytes left)? -> its id (EMO_OTHER: an emoji badge) and *len the bytes it takes
// (with its modifiers and a whole ZWJ sequence); -1 none. typed: ":)" and the rest count too.
static int emo_at (const char *s, int n, int *len, bool typed)
{
	if (n <= 0) return -1;
	int k;
	unsigned cp = emo_u8 (s, n, &k);
	if (cp >= 0x80 && (emo_range (cp) || emo_of_cp (cp) >= 0))
	{
		int id = emo_of_cp (cp);
		if (id < 0) id = EMO_OTHER;
		int o = k;
		for (;;)					// modifiers, and what a joiner joins
		{
			if (o >= n) break;
			int m;
			unsigned c2 = emo_u8 (s + o, n - o, &m);
			if (emo_mod (c2)) { o += m; if (c2 == 0x200D && o < n) { emo_u8 (s + o, n - o, &m); o += m; } continue; }
			if (cp >= 0x1F1E6 && cp <= 0x1F1FF && c2 >= 0x1F1E6 && c2 <= 0x1F1FF) { o += m; cp = 0; continue; }	// (a flag: two letters)
			break;
		}
		*len = o;
		return id;
	}
	if (!typed) return -1;
	for (int i = 0; i < EMO_COUNT; i++)
	{
		const char *t = emo_info[i].text;
		if (!t) continue;
		int tl = 0;
		while (t[tl]) tl++;
		if (tl > n) continue;
		bool eq = true;
		for (int j = 0; j < tl && eq; j++)
		{
			char a = s[j], b = t[j];
			if (a >= 'a' && a <= 'z' && b >= 'A' && b <= 'Z') a = (char) (a - 32);
			eq = a == b;
		}
		if (!eq) continue;
		// ":O" and the like only as words of their own (not inside a URL "http://...")
		if (tl < n && s[tl] != ' ' && s[tl] != '\n' && s[tl] != '.' && s[tl] != ',' && s[tl] != '!' && s[tl] != '?' && s[tl] != ')') continue;
		*len = tl;
		return i;
	}
	return -1;
}

// ---- drawing ----------------------------------------------------------------------------------------

static inline int EV (float px) { return (int) (px * 16.0f); }

// A face: the disc, its shading and outline.
static void emo_face (Canvas &cv, float cx, float cy, float r, unsigned face, unsigned rim)
{
	VPath p;
	p.circle (EV (cx), EV (cy), EV (r + 0.6f)); p.fill (cv, rim);
	p.clear (); p.circle (EV (cx), EV (cy), EV (r)); p.fill (cv, face);
	p.clear (); p.circle (EV (cx), EV (cy + r * 0.18f), EV (r * 0.86f)); p.fill (cv, uk_mix (face, 0xE07800, 60), 110);
	p.clear (); p.ellipse (EV (cx - r * 0.25f), EV (cy - r * 0.45f), EV (r * 0.5f), EV (r * 0.3f)); p.fill (cv, 0xFFFFFF, 140);
}
static void emo_eyes (Canvas &cv, float cx, float cy, float r, unsigned ink, bool winkRight = false)
{
	VPath p;
	float ex = r * 0.36f, ey = cy - r * 0.18f;
	p.ellipse (EV (cx - ex), EV (ey), EV (r * 0.11f + 0.4f), EV (r * 0.2f + 0.4f));
	if (!winkRight) p.ellipse (EV (cx + ex), EV (ey), EV (r * 0.11f + 0.4f), EV (r * 0.2f + 0.4f));
	p.fill (cv, ink);
	if (winkRight) { p.clear (); p.arc (EV (cx + ex), EV (ey + r * 0.1f), EV (r * 0.16f), 20, 160, EV (r * 0.09f + 0.6f)); p.fill (cv, ink); }
}
static void emo_mouth (Canvas &cv, float cx, float cy, float r, int a0, int a1, float rr, unsigned ink, float dy = 0.12f)
{
	VPath p;
	p.arc (EV (cx), EV (cy + r * dy), EV (r * rr), a0, a1, EV (r * 0.09f + 0.6f));
	p.fill (cv, ink);
}
static void emo_heart (Canvas &cv, float cx, float cy, float s, unsigned c, unsigned rim, bool broken = false)
{
	VPath p;
	float r = s * 0.26f;
	for (int pass = 0; pass < 2; pass++)
	{
		float g = pass ? 0 : 0.8f;
		p.clear ();
		p.circle (EV (cx - r * 0.95f), EV (cy - r * 0.55f), EV (r + g));
		p.circle (EV (cx + r * 0.95f), EV (cy - r * 0.55f), EV (r + g));
		int tri[6] = { EV (cx - r * 1.9f - g), EV (cy - r * 0.3f), EV (cx + r * 1.9f + g), EV (cy - r * 0.3f), EV (cx), EV (cy + r * 2.0f + g) };
		p.poly (tri, 3);
		p.fill (cv, pass ? c : rim);
	}
	p.clear (); p.ellipse (EV (cx - r * 1.0f), EV (cy - r * 0.9f), EV (r * 0.45f), EV (r * 0.3f)); p.fill (cv, 0xFFFFFF, 150);
	if (broken)
	{
		int zig[8] = { EV (cx + 0.4f), EV (cy - r * 1.4f), EV (cx - r * 0.5f), EV (cy - r * 0.2f), EV (cx + r * 0.4f), EV (cy + r * 0.5f), EV (cx - r * 0.1f), EV (cy + r * 2.0f) };
		p.clear (); p.polyline (zig, 4, EV (s * 0.09f + 0.6f)); p.fill (cv, 0xFFFFFF);
	}
}
static void emo_thumb (Canvas &cv, float x, float y, float s, bool up)
{
	VPath p;
	unsigned skin = 0xF5C26B, rim = 0xB57A1E;
	float sy = up ? 1.0f : -1.0f, my = y + s * 0.5f;
	for (int pass = 0; pass < 2; pass++)
	{
		float g = pass ? 0 : 0.7f;
		p.clear ();
		p.rrect (EV (x + s * 0.3f - g), EV (my - s * 0.12f * sy - (up ? 0 : s * 0.3f) - g), EV (s * 0.55f + 2 * g), EV (s * 0.42f + 2 * g), EV (s * 0.1f));
		p.rrect (EV (x + s * 0.4f - g), EV (my - sy * s * 0.42f - (up ? 0 : s * 0.22f) - g), EV (s * 0.17f + 2 * g), EV (s * 0.36f + 2 * g), EV (s * 0.08f));
		p.fill (cv, pass ? skin : rim);
	}
	p.clear (); p.rrect (EV (x + s * 0.1f), EV (my - s * 0.12f * sy - (up ? 0 : s * 0.3f)), EV (s * 0.18f), EV (s * 0.42f), EV (s * 0.04f)); p.fill (cv, 0x3C6EB4);
}

// Emoticon id drawn in the s x s box at (x, y).
static void emo_draw (Canvas &cv, int id, int x, int y, int s)
{
	float r = s * 0.46f, cx = x + s * 0.5f, cy = y + s * 0.5f;
	unsigned yel = 0xFFD43B, rim = 0xC27C0E, ink = 0x4A3008, red = 0xE8364F;
	VPath p;
	switch (id)
	{
	case EMO_HEART: emo_heart (cv, cx, cy, (float) s, red, 0x9E1328); return;
	case EMO_BROKEN: emo_heart (cv, cx, cy, (float) s, red, 0x9E1328, true); return;
	case EMO_THUMBUP: emo_thumb (cv, (float) x, (float) y, (float) s, true); return;
	case EMO_THUMBDOWN: emo_thumb (cv, (float) x, (float) y, (float) s, false); return;
	case EMO_STAR:
	{
		int pts[20];
		for (int i = 0; i < 10; i++)
		{
			float rr = i & 1 ? r * 0.45f : r * 1.02f;
			pts[i * 2] = EV (cx + rr * uk_cos (90 + i * 36) / 16384.0f);
			pts[i * 2 + 1] = EV (cy - rr * uk_sin (90 + i * 36) / 16384.0f + s * 0.06f);
		}
		p.polyline (pts, 10, EV (1.3f), true); p.fill (cv, 0xC48A00);
		p.clear (); p.poly (pts, 10); p.fill (cv, 0xFFCC1A);
		return;
	}
	case EMO_SUN:
		for (int i = 0; i < 8; i++)
		{
			int a = i * 45;
			p.line (EV (cx + r * 0.6f * uk_cos (a) / 16384.0f), EV (cy - r * 0.6f * uk_sin (a) / 16384.0f), EV (cx + r * uk_cos (a) / 16384.0f), EV (cy - r * uk_sin (a) / 16384.0f), EV (s * 0.08f + 0.5f));
		}
		p.fill (cv, 0xF59E0B);
		emo_face (cv, cx, cy, r * 0.58f, 0xFFD43B, 0xD98A00);
		return;
	case EMO_COFFEE:
		p.rrect (EV (x + s * 0.18f), EV (y + s * 0.38f), EV (s * 0.5f), EV (s * 0.46f), EV (s * 0.08f)); p.fill (cv, 0x7A4A24);
		p.clear (); p.arc (EV (x + s * 0.7f), EV (y + s * 0.58f), EV (s * 0.12f), -90, 90, EV (s * 0.07f + 0.5f)); p.fill (cv, 0x7A4A24);
		p.clear (); p.rrect (EV (x + s * 0.22f), EV (y + s * 0.38f), EV (s * 0.42f), EV (s * 0.08f), EV (1)); p.fill (cv, 0xC89A6A);
		p.clear (); p.arc (EV (x + s * 0.36f), EV (y + s * 0.26f), EV (s * 0.08f), 90, 270, EV (1.1f)); p.arc (EV (x + s * 0.5f), EV (y + s * 0.2f), EV (s * 0.08f), 90, 270, EV (1.1f)); p.fill (cv, 0x9A9A9A);
		return;
	case EMO_ROSE:
		p.line (EV (cx), EV (cy), EV (cx - s * 0.05f), EV (y + s * 0.95f), EV (s * 0.07f + 0.5f)); p.fill (cv, 0x2E8B3A);
		p.clear (); p.ellipse (EV (cx + s * 0.14f), EV (cy + s * 0.2f), EV (s * 0.14f), EV (s * 0.07f)); p.fill (cv, 0x3BA34A);
		p.clear (); p.circle (EV (cx), EV (y + s * 0.34f), EV (s * 0.26f)); p.fill (cv, 0xA3122A);
		p.clear (); p.circle (EV (cx), EV (y + s * 0.32f), EV (s * 0.2f)); p.fill (cv, 0xE0304A);
		p.clear (); p.arc (EV (cx), EV (y + s * 0.33f), EV (s * 0.1f), 0, 300, EV (1.1f)); p.fill (cv, 0x8E0B20);
		return;
	case EMO_GIFT:
		p.rrect (EV (x + s * 0.12f), EV (y + s * 0.36f), EV (s * 0.76f), EV (s * 0.52f), EV (1.5f)); p.fill (cv, 0x2F7BD8);
		p.clear (); p.rect (EV (x + s * 0.08f), EV (y + s * 0.28f), EV (s * 0.84f), EV (s * 0.16f)); p.fill (cv, 0x4A93F0);
		p.clear (); p.rect (EV (cx - s * 0.07f), EV (y + s * 0.28f), EV (s * 0.14f), EV (s * 0.6f)); p.fill (cv, 0xF4C430);
		p.clear (); p.ellipse (EV (cx - s * 0.15f), EV (y + s * 0.2f), EV (s * 0.14f), EV (s * 0.09f)); p.ellipse (EV (cx + s * 0.15f), EV (y + s * 0.2f), EV (s * 0.14f), EV (s * 0.09f)); p.fill (cv, 0xF4C430);
		return;
	case EMO_PARTY:
	{
		int cone[6] = { EV (x + s * 0.12f), EV (y + s * 0.9f), EV (x + s * 0.42f), EV (y + s * 0.3f), EV (x + s * 0.7f), EV (y + s * 0.62f) };
		p.poly (cone, 3); p.fill (cv, 0xF59E0B);
		p.clear (); p.line (EV (x + s * 0.27f), EV (y + s * 0.6f), EV (x + s * 0.47f), EV (y + s * 0.72f), EV (1.4f)); p.fill (cv, 0xE8364F);
		static const unsigned conf[5] = { 0xE8364F, 0x2F7BD8, 0x3BA34A, 0xA855F7, 0xF4C430 };
		for (int i = 0; i < 5; i++) { p.clear (); p.circle (EV (x + s * (0.55f + 0.08f * i)), EV (y + s * (0.12f + 0.1f * (i % 3))), EV (s * 0.05f + 0.4f)); p.fill (cv, conf[i]); }
		return;
	}
	case EMO_FIRE:
	{
		int f1[10] = { EV (cx), EV (y + s * 0.05f), EV (cx + r * 0.85f), EV (cy + r * 0.2f), EV (cx + r * 0.6f), EV (y + s * 0.95f), EV (cx - r * 0.6f), EV (y + s * 0.95f), EV (cx - r * 0.85f), EV (cy + r * 0.1f) };
		p.poly (f1, 5); p.fill (cv, 0xF0582A);
		int f2[8] = { EV (cx), EV (cy - r * 0.2f), EV (cx + r * 0.45f), EV (cy + r * 0.5f), EV (cx), EV (y + s * 0.95f), EV (cx - r * 0.45f), EV (cy + r * 0.5f) };
		p.clear (); p.poly (f2, 4); p.fill (cv, 0xFFC93C);
		return;
	}
	case EMO_CLAP: case EMO_PRAY: case EMO_OK:
	{
		unsigned skin = 0xF5C26B, srim = 0xB57A1E;
		if (id == EMO_OK)
		{
			p.circle (EV (cx - r * 0.25f), EV (cy - r * 0.05f), EV (r * 0.42f)); p.fill (cv, srim);
			p.clear (); p.hole (EV (cx - r * 0.25f), EV (cy - r * 0.05f), EV (r * 0.2f)); p.circle (EV (cx - r * 0.25f), EV (cy - r * 0.05f), EV (r * 0.34f)); p.fill (cv, skin);
			p.clear (); for (int i = 0; i < 3; i++) p.rrect (EV (cx + r * (0.05f + 0.25f * i)), EV (cy - r * 0.95f + i * r * 0.1f), EV (r * 0.22f), EV (r * 0.9f), EV (r * 0.1f)); p.fill (cv, skin);
			p.clear (); p.rrect (EV (cx - r * 0.2f), EV (cy + r * 0.2f), EV (r * 0.9f), EV (r * 0.7f), EV (r * 0.25f)); p.fill (cv, skin);
			return;
		}
		p.ellipse (EV (cx - r * 0.28f), EV (cy + r * 0.05f), EV (r * 0.36f), EV (r * 0.8f)); p.ellipse (EV (cx + r * 0.28f), EV (cy + r * 0.05f), EV (r * 0.36f), EV (r * 0.8f)); p.fill (cv, srim);
		p.clear (); p.ellipse (EV (cx - r * 0.28f), EV (cy + r * 0.05f), EV (r * 0.3f), EV (r * 0.74f)); p.ellipse (EV (cx + r * 0.28f), EV (cy + r * 0.05f), EV (r * 0.3f), EV (r * 0.74f)); p.fill (cv, skin);
		if (id == EMO_CLAP) { p.clear (); p.line (EV (x + s * 0.1f), EV (y + s * 0.18f), EV (x + s * 0.2f), EV (y + s * 0.28f), EV (1.2f)); p.line (EV (x + s * 0.9f), EV (y + s * 0.18f), EV (x + s * 0.8f), EV (y + s * 0.28f), EV (1.2f)); p.fill (cv, 0xF59E0B); }
		return;
	}
	case EMO_OTHER:
		p.circle (EV (cx), EV (cy), EV (r * 0.86f)); p.fill (cv, 0x9AA5B1);
		p.clear (); p.circle (EV (cx), EV (cy), EV (r * 0.74f)); p.fill (cv, 0xE7EBEF);
		p.clear (); p.circle (EV (cx - r * 0.28f), EV (cy - r * 0.15f), EV (r * 0.1f + 0.4f)); p.circle (EV (cx + r * 0.28f), EV (cy - r * 0.15f), EV (r * 0.1f + 0.4f)); p.fill (cv, 0x6B7682);
		p.clear (); p.arc (EV (cx), EV (cy + r * 0.05f), EV (r * 0.35f), 200, 340, EV (1.1f)); p.fill (cv, 0x6B7682);
		return;
	}
	// the faces
	unsigned face = yel;
	if (id == EMO_ANGRY) { face = 0xF26B4B; rim = 0xA8321A; }
	if (id == EMO_SICK) { face = 0xA6D86E; rim = 0x5E8F2A; ink = 0x2E4A12; }
	if (id == EMO_BLUSH) face = 0xFFC94A;
	emo_face (cv, cx, cy, r, face, rim);
	switch (id)
	{
	case EMO_SMILE: emo_eyes (cv, cx, cy, r, ink); emo_mouth (cv, cx, cy, r, 200, 340, 0.55f, ink, 0.0f); break;
	case EMO_GRIN:
		emo_eyes (cv, cx, cy, r, ink);
		{ VPath m; m.ellipse (EV (cx), EV (cy + r * 0.32f), EV (r * 0.5f), EV (r * 0.3f)); m.fill (cv, ink);
		  m.clear (); m.rect (EV (cx - r * 0.6f), EV (cy + r * 0.02f), EV (r * 1.2f), EV (r * 0.26f)); m.fill (cv, face);
		  m.clear (); m.ellipse (EV (cx), EV (cy + r * 0.27f), EV (r * 0.36f), EV (r * 0.08f)); m.fill (cv, 0xFFFFFF); }
		break;
	case EMO_WINK: emo_eyes (cv, cx, cy, r, ink, true); emo_mouth (cv, cx, cy, r, 200, 340, 0.55f, ink, 0.0f); break;
	case EMO_SAD: emo_eyes (cv, cx, cy, r, ink); emo_mouth (cv, cx, cy, r, 30, 150, 0.42f, ink, 0.65f); break;
	case EMO_CRY:
		emo_eyes (cv, cx, cy, r, ink); emo_mouth (cv, cx, cy, r, 30, 150, 0.42f, ink, 0.65f);
		{ VPath t; t.ellipse (EV (cx - r * 0.5f), EV (cy + r * 0.25f), EV (r * 0.13f + 0.4f), EV (r * 0.26f)); t.ellipse (EV (cx + r * 0.5f), EV (cy + r * 0.25f), EV (r * 0.13f + 0.4f), EV (r * 0.26f)); t.fill (cv, 0x4AA8F0); }
		break;
	case EMO_TONGUE:
		emo_eyes (cv, cx, cy, r, ink); emo_mouth (cv, cx, cy, r, 200, 340, 0.5f, ink, 0.0f);
		{ VPath t; t.ellipse (EV (cx + r * 0.08f), EV (cy + r * 0.56f), EV (r * 0.2f), EV (r * 0.22f)); t.fill (cv, 0xE0546A); }
		break;
	case EMO_SURPRISED:
		emo_eyes (cv, cx, cy, r, ink);
		{ VPath m; m.ellipse (EV (cx), EV (cy + r * 0.42f), EV (r * 0.2f), EV (r * 0.24f)); m.fill (cv, ink); }
		break;
	case EMO_ANGRY:
		emo_eyes (cv, cx, cy, r, ink); emo_mouth (cv, cx, cy, r, 30, 150, 0.4f, ink, 0.7f);
		{ VPath b; b.line (EV (cx - r * 0.6f), EV (cy - r * 0.55f), EV (cx - r * 0.15f), EV (cy - r * 0.35f), EV (r * 0.1f + 0.6f)); b.line (EV (cx + r * 0.6f), EV (cy - r * 0.55f), EV (cx + r * 0.15f), EV (cy - r * 0.35f), EV (r * 0.1f + 0.6f)); b.fill (cv, ink); }
		break;
	case EMO_COOL:
		{ VPath g; g.rrect (EV (cx - r * 0.82f), EV (cy - r * 0.38f), EV (r * 0.72f), EV (r * 0.42f), EV (r * 0.15f)); g.rrect (EV (cx + r * 0.1f), EV (cy - r * 0.38f), EV (r * 0.72f), EV (r * 0.42f), EV (r * 0.15f));
		  g.line (EV (cx - r * 0.2f), EV (cy - r * 0.3f), EV (cx + r * 0.2f), EV (cy - r * 0.3f), EV (r * 0.1f + 0.5f)); g.fill (cv, 0x1E2430);
		  g.clear (); g.line (EV (cx - r * 0.65f), EV (cy - r * 0.3f), EV (cx - r * 0.45f), EV (cy - r * 0.3f), EV (0.9f)); g.fill (cv, 0xFFFFFF, 160); }
		emo_mouth (cv, cx, cy, r, 210, 330, 0.5f, ink, 0.05f);
		break;
	case EMO_LAUGH:
		{ VPath e; e.arc (EV (cx - r * 0.36f), EV (cy - r * 0.15f), EV (r * 0.17f), 20, 160, EV (r * 0.09f + 0.6f)); e.arc (EV (cx + r * 0.36f), EV (cy - r * 0.15f), EV (r * 0.17f), 20, 160, EV (r * 0.09f + 0.6f)); e.fill (cv, ink);
		  VPath m; m.ellipse (EV (cx), EV (cy + r * 0.35f), EV (r * 0.5f), EV (r * 0.3f)); m.fill (cv, ink);
		  m.clear (); m.rect (EV (cx - r * 0.6f), EV (cy + r * 0.05f), EV (r * 1.2f), EV (r * 0.26f)); m.fill (cv, face);
		  VPath t; t.ellipse (EV (cx - r * 0.72f), EV (cy + r * 0.05f), EV (r * 0.14f), EV (r * 0.28f)); t.ellipse (EV (cx + r * 0.72f), EV (cy + r * 0.05f), EV (r * 0.14f), EV (r * 0.28f)); t.fill (cv, 0x4AA8F0); }
		break;
	case EMO_LOVE:
		emo_heart (cv, cx - r * 0.36f, cy - r * 0.18f, r * 0.62f, red, 0x9E1328); emo_heart (cv, cx + r * 0.36f, cy - r * 0.18f, r * 0.62f, red, 0x9E1328);
		emo_mouth (cv, cx, cy, r, 200, 340, 0.5f, ink, 0.05f);
		break;
	case EMO_KISS:
		emo_eyes (cv, cx, cy, r, ink);
		{ VPath m; m.arc (EV (cx), EV (cy + r * 0.35f), EV (r * 0.13f), 270, 450, EV (r * 0.09f + 0.6f)); m.arc (EV (cx), EV (cy + r * 0.6f), EV (r * 0.13f), 270, 450, EV (r * 0.09f + 0.6f)); m.fill (cv, ink); }
		emo_heart (cv, cx + r * 0.65f, cy + r * 0.45f, r * 0.45f, red, 0x9E1328);
		break;
	case EMO_CONFUSED:
		emo_eyes (cv, cx, cy, r, ink);
		{ VPath m; int w[8] = { EV (cx - r * 0.45f), EV (cy + r * 0.45f), EV (cx - r * 0.15f), EV (cy + r * 0.32f), EV (cx + r * 0.15f), EV (cy + r * 0.5f), EV (cx + r * 0.45f), EV (cy + r * 0.36f) };
		  m.polyline (w, 4, EV (r * 0.09f + 0.6f)); m.fill (cv, ink); }
		break;
	case EMO_NEUTRAL:
		emo_eyes (cv, cx, cy, r, ink);
		{ VPath m; m.line (EV (cx - r * 0.38f), EV (cy + r * 0.42f), EV (cx + r * 0.38f), EV (cy + r * 0.42f), EV (r * 0.09f + 0.6f)); m.fill (cv, ink); }
		break;
	case EMO_BLUSH:
		emo_eyes (cv, cx, cy, r, ink);
		{ VPath c; c.ellipse (EV (cx - r * 0.55f), EV (cy + r * 0.2f), EV (r * 0.2f), EV (r * 0.12f)); c.ellipse (EV (cx + r * 0.55f), EV (cy + r * 0.2f), EV (r * 0.2f), EV (r * 0.12f)); c.fill (cv, 0xF0607A, 170); }
		emo_mouth (cv, cx, cy, r, 220, 320, 0.3f, ink, 0.25f);
		break;
	case EMO_THINK:
		emo_eyes (cv, cx, cy, r, ink);
		{ VPath m; m.line (EV (cx - r * 0.1f), EV (cy + r * 0.42f), EV (cx + r * 0.4f), EV (cy + r * 0.32f), EV (r * 0.09f + 0.6f)); m.fill (cv, ink);
		  m.clear (); m.rrect (EV (cx - r * 0.75f), EV (cy + r * 0.45f), EV (r * 0.6f), EV (r * 0.32f), EV (r * 0.15f)); m.fill (cv, 0xF5C26B); }
		break;
	case EMO_SLEEP:
		{ VPath e; e.arc (EV (cx - r * 0.36f), EV (cy - r * 0.25f), EV (r * 0.17f), 200, 340, EV (r * 0.08f + 0.6f)); e.arc (EV (cx + r * 0.36f), EV (cy - r * 0.25f), EV (r * 0.17f), 200, 340, EV (r * 0.08f + 0.6f)); e.fill (cv, ink); }
		{ VPath m; m.ellipse (EV (cx), EV (cy + r * 0.42f), EV (r * 0.13f), EV (r * 0.16f)); m.fill (cv, ink); }
		{ VPath z; int zz[8] = { EV (cx + r * 0.45f), EV (y + s * 0.02f), EV (cx + r * 0.85f), EV (y + s * 0.02f), EV (cx + r * 0.45f), EV (y + s * 0.2f), EV (cx + r * 0.85f), EV (y + s * 0.2f) };
		  z.polyline (zz, 4, EV (1.1f)); z.fill (cv, 0x2F7BD8); }
		break;
	case EMO_SICK:
		emo_eyes (cv, cx, cy, r, ink); emo_mouth (cv, cx, cy, r, 30, 150, 0.4f, ink, 0.65f);
		break;
	}
}

#endif
