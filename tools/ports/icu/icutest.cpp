// icutest -- the smoke test of Onyx's ICU port (tools/ports/icu; docs/03 §5.5): what WebKit asks of ICU,
// through ICU's C API as WebKit calls it -- collation (usearch / Intl.Collator), break iterators (WTF's
// TextBreakIterator: words and lines, Thai by its dictionary, Japanese), the converters of TextCodecICU
// (+ Shift_JIS / GBK / EUC-KR / Big5 -> UTF-8), number / date / plural formatting for JSC's Intl in a few
// locales, time zones, IDNA (WTF's URLParser), normalization, case mapping, charset detection.
// One line per check (PASS / FAIL), a summary; the exit status is the number of failures.
//
//   icutest            all the checks
//   icutest -v         also prints what each check produced
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is hereby granted,
// free of charge, to any person obtaining a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions: The above
// copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
#include <unicode/icudataver.h>
#include <unicode/ubrk.h>
#include <unicode/ucal.h>
#include <unicode/ucnv.h>
#include <unicode/ucol.h>
#include <unicode/ucsdet.h>
#include <unicode/udat.h>
#include <unicode/udatpg.h>
#include <unicode/uidna.h>
#include <unicode/uloc.h>
#include <unicode/unorm2.h>
#include <unicode/unumberformatter.h>
#include <unicode/upluralrules.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>
#include <unicode/uversion.h>
#include <unicode/usearch.h>
#include <unicode/ulistformatter.h>
#include <unicode/ureldatefmt.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

static int g_pass, g_fail;
static bool g_verbose;

static void check (bool ok, const char *what, const std::string &got = std::string ())
{
	if (ok)
		g_pass++;
	else
		g_fail++;
	printf ("%s  %s", ok ? "PASS" : "FAIL", what);
	if ((!ok || g_verbose) && !got.empty ())
		printf ("  [%s]", got.c_str ());
	printf ("\n");
}

static std::u16string u16 (const char *utf8)
{
	UErrorCode e = U_ZERO_ERROR;
	int32_t n = 0;
	u_strFromUTF8 (nullptr, 0, &n, utf8, -1, &e);
	std::u16string s (n, 0);
	e = U_ZERO_ERROR;
	u_strFromUTF8 ((UChar *) s.data (), n + 1, &n, utf8, -1, &e);
	return s;
}

static std::string u8 (const UChar *s, int32_t len = -1)
{
	UErrorCode e = U_ZERO_ERROR;
	int32_t n = 0;
	u_strToUTF8 (nullptr, 0, &n, s, len, &e);
	std::string r (n, 0);
	e = U_ZERO_ERROR;
	u_strToUTF8 (r.data (), n + 1, &n, s, len, &e);
	return r;
}

static std::string u8 (const std::u16string &s) { return u8 ((const UChar *) s.data (), (int32_t) s.size ()); }

// ---- collation ----
static std::string sorted (const char *locale, std::vector<const char *> words)
{
	UErrorCode e = U_ZERO_ERROR;
	UCollator *c = ucol_open (locale, &e);
	if (U_FAILURE (e))
		return std::string ("ucol_open: ") + u_errorName (e);
	std::vector<std::u16string> w;
	for (const char *s : words)
		w.push_back (u16 (s));
	std::sort (w.begin (), w.end (), [c] (const std::u16string &a, const std::u16string &b) {
		return ucol_strcoll (c, (const UChar *) a.data (), (int32_t) a.size (), (const UChar *) b.data (), (int32_t) b.size ()) == UCOL_LESS;
	});
	ucol_close (c);
	std::string r;
	for (auto &s : w)
		r += (r.empty () ? "" : " ") + u8 (s);
	return r;
}

// ---- break iteration: the segments (words: only those with a word rule status) ----
static std::string segments (UBreakIteratorType type, const char *locale, const char *text, bool wordsOnly)
{
	UErrorCode e = U_ZERO_ERROR;
	std::u16string t = u16 (text);
	UBreakIterator *bi = ubrk_open (type, locale, (const UChar *) t.data (), (int32_t) t.size (), &e);
	if (U_FAILURE (e))
		return std::string ("ubrk_open: ") + u_errorName (e);
	std::string r;
	int32_t start = ubrk_first (bi);
	for (int32_t end = ubrk_next (bi); end != UBRK_DONE; start = end, end = ubrk_next (bi)) {
		if (wordsOnly && ubrk_getRuleStatus (bi) == UBRK_WORD_NONE)
			continue;
		r += (r.empty () ? "" : "|") + u8 ((const UChar *) t.data () + start, end - start);
	}
	ubrk_close (bi);
	return r;
}

// ---- conversion ----
static std::string toUTF8 (const char *conv, const char *bytes, int32_t len)
{
	UErrorCode e = U_ZERO_ERROR;
	UConverter *c = ucnv_open (conv, &e);
	if (U_FAILURE (e))
		return std::string ("ucnv_open: ") + u_errorName (e);
	UChar buf[256];
	int32_t n = ucnv_toUChars (c, buf, 256, bytes, len, &e);
	ucnv_close (c);
	if (U_FAILURE (e))
		return std::string ("ucnv_toUChars: ") + u_errorName (e);
	return u8 (buf, n);
}

static std::string fromUTF8 (const char *conv, const char *utf8)
{
	UErrorCode e = U_ZERO_ERROR;
	UConverter *c = ucnv_open (conv, &e);
	if (U_FAILURE (e))
		return std::string ("ucnv_open: ") + u_errorName (e);
	std::u16string s = u16 (utf8);
	char buf[256];
	int32_t n = ucnv_fromUChars (c, buf, sizeof buf, (const UChar *) s.data (), (int32_t) s.size (), &e);
	ucnv_close (c);
	std::string hex;
	for (int32_t i = 0; i < n; i++) {
		char h[4];
		snprintf (h, sizeof h, "%02X", (unsigned char) buf[i]);
		hex += h;
	}
	return hex;
}

// ---- numbers (the skeletons JSC's Intl.NumberFormat builds) ----
static std::string number (const char *locale, const char *skeleton, double v)
{
	UErrorCode e = U_ZERO_ERROR;
	std::u16string sk = u16 (skeleton);
	UNumberFormatter *f = unumf_openForSkeletonAndLocale ((const UChar *) sk.data (), (int32_t) sk.size (), locale, &e);
	UFormattedNumber *r = unumf_openResult (&e);
	unumf_formatDouble (f, v, r, &e);
	UChar buf[128];
	int32_t n = unumf_resultToString (r, buf, 128, &e);
	unumf_closeResult (r);
	unumf_close (f);
	if (U_FAILURE (e))
		return std::string ("unumf: ") + u_errorName (e);
	return u8 (buf, n);
}

// ---- dates (Intl.DateTimeFormat: a skeleton through the pattern generator) ----
static std::string date (const char *locale, const char *skeleton, UDate when, const char *zone)
{
	UErrorCode e = U_ZERO_ERROR;
	UDateTimePatternGenerator *g = udatpg_open (locale, &e);
	std::u16string sk = u16 (skeleton);
	UChar pat[128];
	int32_t pn = udatpg_getBestPattern (g, (const UChar *) sk.data (), (int32_t) sk.size (), pat, 128, &e);
	udatpg_close (g);
	std::u16string z = u16 (zone);
	UDateFormat *df = udat_open (UDAT_PATTERN, UDAT_PATTERN, locale, (const UChar *) z.data (), (int32_t) z.size (), pat, pn, &e);
	UChar buf[128];
	int32_t n = udat_format (df, when, buf, 128, nullptr, &e);
	udat_close (df);
	if (U_FAILURE (e))
		return std::string ("udat: ") + u_errorName (e);
	return u8 (buf, n);
}

static bool contains (const std::string &s, const char *part) { return s.find (part) != std::string::npos; }

int main (int argc, char **argv)
{
	g_verbose = argc > 1 && strcmp (argv[1], "-v") == 0;
	UVersionInfo v;
	char vs[U_MAX_VERSION_STRING_LENGTH];
	u_getVersion (v);
	u_versionToString (v, vs);
	UErrorCode e = U_ZERO_ERROR;
	UVersionInfo dv;
	u_getDataVersion (dv, &e);
	char dvs[U_MAX_VERSION_STRING_LENGTH];
	u_versionToString (dv, dvs);
	printf ("icutest: ICU %s, data %s, Unicode %s, default locale %s\n", vs, U_SUCCESS (e) ? dvs : u_errorName (e), U_UNICODE_VERSION, uloc_getDefault ());
	check (U_SUCCESS (e) && strcmp (vs, "78.3") == 0, "ICU 78.3 with its data linked in", dvs);

	// collation: Swedish puts Ä after Z, English beside A; German phonebook; Japanese kana
	std::string s = sorted ("en", { "zebra", "Äpfel", "apple", "Zebra", "able" });
	check (s == "able Äpfel apple zebra Zebra", "collation: en (Ä as A)", s);
	s = sorted ("sv", { "zebra", "Äpfel", "apple", "Zebra", "able" });
	check (s == "able apple zebra Zebra Äpfel", "collation: sv (Ä after Z)", s);
	s = sorted ("zh", { "中", "国", "啊" });		// pinyin: a, guo, zhong
	check (s == "啊 国 中", "collation: zh (pinyin)", s);
	{
		// usearch, as WebCore's find-in-page: case and accent insensitive (primary strength)
		UErrorCode se = U_ZERO_ERROR;
		std::u16string pat = u16 ("cafe"), txt = u16 ("Le CAFÉ de la gare, un café.");
		UStringSearch *ss = usearch_open ((const UChar *) pat.data (), (int32_t) pat.size (), (const UChar *) txt.data (), (int32_t) txt.size (), "fr", nullptr, &se);
		ucol_setStrength (usearch_getCollator (ss), UCOL_PRIMARY);
		usearch_reset (ss);
		int hits = 0;
		for (int32_t p = usearch_first (ss, &se); p != USEARCH_DONE; p = usearch_next (ss, &se))
			hits++;
		usearch_close (ss);
		check (U_SUCCESS (se) && hits == 2, "usearch: \"cafe\" found twice (CAFÉ, café)", std::to_string (hits));
	}

	// break iterators
	s = segments (UBRK_WORD, "en", "Hello, world! It's 3.5 km.", true);
	check (s == "Hello|world|It's|3.5|km", "words: en", s);
	s = segments (UBRK_WORD, "th", "สวัสดีครับ ยินดีต้อนรับ", true);
	check (s == "สวัสดี|ครับ|ยินดี|ต้อนรับ", "words: th (the dictionary)", s);
	s = segments (UBRK_WORD, "ja", "日本語のテキストです", true);
	check (s == "日本語|の|テキスト|です", "words: ja (the CJ dictionary)", s);
	s = segments (UBRK_LINE, "en", "The quick (brown) fox.", false);
	check (s == "The |quick |(brown) |fox.", "lines: en", s);
	s = segments (UBRK_LINE, "th", "สวัสดีครับ", false);
	check (s == "สวัสดี|ครับ", "lines: th", s);
	s = segments (UBRK_LINE, "ja", "東京です。", false);
	check (s == "東|京|で|す。", "lines: ja (no break before 。)", s);
	s = segments (UBRK_LINE, "ja@lw=phrase", "東京都に住んでいます。", false);
	check (s == "東京|都に|住んでいます。", "lines: ja@lw=phrase (word-break: auto-phrase, the phrase model)", s);
	s = segments (UBRK_CHARACTER, "en", "e\xCC\x81" "👍🏽🇫🇷", false);
	check (s == "e\xCC\x81|👍🏽|🇫🇷", "graphemes: a combining mark, an emoji with a skin tone, a flag", s);
	s = segments (UBRK_SENTENCE, "en", "It rained. He came back.", false);
	check (s == "It rained. |He came back.", "sentences: en", s);

	// converters: the CJK ones, and every encoding WebKit's TextCodecICU registers
	s = toUTF8 ("Shift_JIS", "\x93\xFA\x96\x7B\x8C\xEA", 6);
	check (s == "日本語", "Shift_JIS -> UTF-8", s);
	s = toUTF8 ("GBK", "\xD6\xD0\xCE\xC4", 4);
	check (s == "中文", "GBK -> UTF-8", s);
	s = toUTF8 ("gb18030", "\x81\x30\x81\x30", 4);
	check (s == "\xC2\x80", "GB18030 (four-byte) -> UTF-8", s);
	s = toUTF8 ("EUC-KR", "\xC7\xD1\xB1\xDB", 4);
	check (s == "한글", "EUC-KR -> UTF-8", s);
	s = toUTF8 ("Big5", "\xA4\xA4\xA4\xE5", 4);
	check (s == "中文", "Big5 -> UTF-8", s);
	s = toUTF8 ("EUC-JP", "\xC6\xFC\xCB\xDC", 4);
	check (s == "日本", "EUC-JP -> UTF-8", s);
	s = toUTF8 ("ISO-2022-JP", "\x1B$B" "F|K\\" "\x1B(B", 10);
	check (s == "日本", "ISO-2022-JP -> UTF-8", s);
	s = fromUTF8 ("windows-1251", "Привет");
	check (s == "CFF0E8E2E5F2", "UTF-8 -> windows-1251", s);
	{
		static const char *const webkit[] = { "ISO-8859-2", "ISO-8859-4", "ISO-8859-5", "ISO-8859-10", "ISO-8859-13",
			"ISO-8859-14", "ISO-8859-15", "KOI8-R", "macintosh", "windows-1250", "windows-1251", "windows-1254",
			"windows-1256", "windows-1258", "x-mac-cyrillic", "x-mac-greek", "x-mac-centraleurroman", "x-mac-turkish",
			"EUC-TW", "ISO-8859-3", "ISO-8859-6", "ISO-8859-7", "ISO-8859-8", "ISO-8859-9", "KOI8-U", "windows-874",
			"windows-1252", "windows-1253", "windows-1255", "windows-1257", "IBM866", "windows-31j", "GB2312",
			"Big5-HKSCS", "windows-949", "ISO-2022-KR", "ISO-2022-CN", "UTF-16LE", "UTF-32", "US-ASCII", "ISO-8859-1" };
		std::string missing;
		for (const char *n : webkit) {
			UErrorCode ce = U_ZERO_ERROR;
			UConverter *c = ucnv_open (n, &ce);
			if (U_FAILURE (ce))
				missing += std::string (missing.empty () ? "" : " ") + n;
			else
				ucnv_close (c);
		}
		check (missing.empty (), "every legacy encoding WebKit's TextCodecICU registers (+ the CJK ones) opens", missing);
		check (ucnv_countAvailable () >= 40, "converters available", std::to_string (ucnv_countAvailable ()));
	}
	{
		UErrorCode de = U_ZERO_ERROR;
		UCharsetDetector *d = ucsdet_open (&de);
		const char *sjis = "\x93\xFA\x96\x7B\x8C\xEA\x82\xCC\x83\x65\x83\x4C\x83\x58\x83\x67\x82\xC5\x82\xB7\x81\x42\x93\xFA\x96\x7B\x8C\xEA\x82\xCC\x83\x65\x83\x4C\x83\x58\x83\x67\x82\xC5\x82\xB7\x81\x42";
		ucsdet_setText (d, sjis, (int32_t) strlen (sjis), &de);
		const UCharsetMatch *m = ucsdet_detect (d, &de);
		s = m ? ucsdet_getName (m, &de) : "none";
		ucsdet_close (d);
		check (s == "Shift_JIS", "charset detection (ucsdet): Shift_JIS text", s);
	}

	// numbers
	s = number ("en-US", "", 1234567.891);
	check (s == "1,234,567.891", "number: en-US", s);
	s = number ("de-DE", "", 1234567.891);
	check (s == "1.234.567,891", "number: de-DE", s);
	s = number ("fr-FR", "", 1234567.891);
	check (s == "1 234 567,891", "number: fr-FR (narrow no-break spaces)", s);
	s = number ("hi-IN", "", 1234567.891);
	check (s == "12,34,567.891", "number: hi-IN (lakh grouping)", s);
	s = number ("ar-EG", "", 1234.5);
	check (s == "١٬٢٣٤٫٥", "number: ar-EG (Arabic-Indic digits)", s);
	s = number ("de-DE", "currency/EUR", 1234.5);
	check (s == "1.234,50 €", "number: de-DE currency EUR", s);
	s = number ("ja-JP", "currency/JPY", 1234.5);
	check (s == "￥1,234" || s == "¥1,234" || s == "￥1,235" || s == "¥1,235", "number: ja-JP currency JPY", s);
	s = number ("en-US", "percent scale/100", 0.256);
	check (s == "25.6%", "number: en-US percent", s);
	s = number ("en-US", "compact-short", 1234567);
	check (s == "1.2M", "number: en-US compact", s);
	s = number ("fr-FR", "measure-unit/speed-kilometer-per-hour unit-width-full-name", 50);
	check (s == "50\u00A0kilomètres par heure" || s == "50\u202Fkilomètres par heure", "number: fr-FR unit (km/h, long)", s);

	// dates: 2026-10-02 12:34:56 UTC
	UErrorCode ce = U_ZERO_ERROR;
	UCalendar *cal = ucal_open (u"UTC", -1, "en", UCAL_GREGORIAN, &ce);
	ucal_clear (cal);
	ucal_setDateTime (cal, 2026, UCAL_OCTOBER, 2, 12, 34, 56, &ce);
	UDate when = ucal_getMillis (cal, &ce);
	ucal_close (cal);
	check (U_SUCCESS (ce) && when == 1790944496000.0, "calendar: 2026-10-02 12:34:56 UTC", std::to_string ((long long) when));
	s = date ("en-US", "yMMMMdjm", when, "UTC");
	check (s == "October 2, 2026 at 12:34 PM", "date: en-US", s);
	s = date ("fr-FR", "yMMMMdjm", when, "Europe/Paris");
	check (s == "2 octobre 2026 à 14:34", "date: fr-FR, Europe/Paris (CEST)", s);
	s = date ("de-DE", "yMMMMEEEEd", when, "UTC");
	check (s == "Freitag, 2. Oktober 2026", "date: de-DE with the weekday", s);
	s = date ("ja-JP", "yMMMMdjm", when, "Asia/Tokyo");
	check (s == "2026年10月2日 21:34", "date: ja-JP, Asia/Tokyo", s);
	s = date ("ar-EG", "yMMMMd", when, "UTC");
	check (contains (s, "أكتوبر"), "date: ar-EG", s);
	s = date ("en-US", "jmz", when, "America/New_York");
	check (s == "8:34 AM EDT", "time zone: America/New_York (EDT)", s);
	{
		UErrorCode te = U_ZERO_ERROR;
		UCalendar *c = ucal_open (u"Australia/Sydney", -1, "en", UCAL_GREGORIAN, &te);
		ucal_setMillis (c, when, &te);
		int32_t off = (ucal_get (c, UCAL_ZONE_OFFSET, &te) + ucal_get (c, UCAL_DST_OFFSET, &te)) / 60000;
		ucal_close (c);
		check (U_SUCCESS (te) && off == 600, "time zone: Australia/Sydney +10:00 (zoneinfo64)", std::to_string (off));
	}
	{
		UErrorCode le = U_ZERO_ERROR;
		UListFormatter *lf = ulistfmt_open ("en", &le);
		const UChar *items[] = { u"red", u"green", u"blue" };
		UChar buf[64];
		int32_t n = ulistfmt_format (lf, items, nullptr, 3, buf, 64, &le);
		ulistfmt_close (lf);
		s = u8 (buf, n);
		check (s == "red, green, and blue", "list format: en (Intl.ListFormat)", s);
	}
	{
		UErrorCode re = U_ZERO_ERROR;
		URelativeDateTimeFormatter *rf = ureldatefmt_open ("de", nullptr, UDAT_STYLE_LONG, UDISPCTX_CAPITALIZATION_NONE, &re);
		UChar buf[64];
		int32_t n = ureldatefmt_format (rf, -3, UDAT_REL_UNIT_DAY, buf, 64, &re);
		ureldatefmt_close (rf);
		s = u8 (buf, n);
		check (s == "vor 3 Tagen", "relative time: de (Intl.RelativeTimeFormat)", s);
	}

	// plurals: Polish few / many, Arabic two
	{
		UErrorCode pe = U_ZERO_ERROR;
		UPluralRules *pl = uplrules_open ("pl", &pe);
		UChar k1[16], k2[16], k3[16];
		uplrules_select (pl, 1, k1, 16, &pe);
		uplrules_select (pl, 3, k2, 16, &pe);
		uplrules_select (pl, 5, k3, 16, &pe);
		uplrules_close (pl);
		s = u8 (k1) + " " + u8 (k2) + " " + u8 (k3);
		check (s == "one few many", "plural rules: pl 1 / 3 / 5", s);
	}

	// display names and locales
	{
		UErrorCode le = U_ZERO_ERROR;
		UChar buf[64];
		int32_t n = uloc_getDisplayName ("zh-Hant-TW", "fr", buf, 64, &le);
		s = u8 (buf, n);
		check (s == "chinois (traditionnel, Taïwan)", "display name: zh-Hant-TW in French (Intl.DisplayNames)", s);
		char full[64];
		le = U_ZERO_ERROR;
		uloc_addLikelySubtags ("sr", full, sizeof full, &le);
		check (strcmp (full, "sr_Cyrl_RS") == 0, "likely subtags: sr -> sr_Cyrl_RS (Intl.Locale)", full);
	}

	// IDNA (WTF's URLParser: UTS 46, non-transitional)
	{
		UErrorCode ie = U_ZERO_ERROR;
		UIDNA *idna = uidna_openUTS46 (UIDNA_CHECK_BIDI | UIDNA_CHECK_CONTEXTJ | UIDNA_NONTRANSITIONAL_TO_UNICODE | UIDNA_NONTRANSITIONAL_TO_ASCII, &ie);
		UIDNAInfo info = UIDNA_INFO_INITIALIZER;
		UChar out[128];
		int32_t n = uidna_nameToASCII (idna, u"Bücher.Straße.de", -1, out, 128, &info, &ie);
		uidna_close (idna);
		s = u8 (out, n);
		check (U_SUCCESS (ie) && info.errors == 0 && s == "xn--bcher-kva.xn--strae-oqa.de", "IDNA UTS 46: Bücher.Straße.de", s);
	}

	// normalization, case mapping
	{
		UErrorCode ne = U_ZERO_ERROR;
		const UNormalizer2 *nfc = unorm2_getNFCInstance (&ne);
		UChar out[16];
		int32_t n = unorm2_normalize (nfc, u"é", -1, out, 16, &ne);
		check (U_SUCCESS (ne) && n == 1 && out[0] == 0xE9, "NFC: e + U+0301 -> é");
		UChar up[32];
		n = u_strToUpper (up, 32, u"straße", -1, "de", &ne);
		s = u8 (up, n);
		check (s == "STRASSE", "case: de upper", s);
		n = u_strToLower (up, 32, u"İSTANBUL", -1, "tr", &ne);
		s = u8 (up, n);
		check (s == "istanbul", "case: tr lower (dotted İ)", s);
	}

	printf ("icutest: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail;
}
