/*
 * uchar.c -- C11 <uchar.h>: mbrtoc16, c16rtomb, mbrtoc32, c32rtomb, mbrtoc8, c8rtomb
 * (libonyxposix, docs/03 §5.4). newlib has none of them. Onyx's multibyte encoding is UTF-8 in
 * every locale, so these are plain UTF-8 <-> UTF-16 / UTF-32 converters, restartable through the
 * mbstate_t (newlib's {int __count; union {wint_t __wch; unsigned char __wchb[4];} __value}):
 *  - decoding: __count = continuation bytes still expected (1..3), __wch = the bits so far,
 *    __wchb is not used; __count = -1: a UTF-16 low surrogate (in __wch) is pending, returned by
 *    the next mbrtoc16 call with (size_t)-3;
 *  - c16rtomb: __count = -2, __wch = a high surrogate waiting for its low half;
 *  - mbrtoc8: __count = -3, __wchb[0] = bytes of the character still to give, __wchb[1..3] them.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <uchar.h>
#include <errno.h>
#include <string.h>

#define ILSEQ ((size_t) -1)
#define INCOMPLETE ((size_t) -2)
#define PENDING ((size_t) -3)

static mbstate_t s_mbrtoc16, s_c16rtomb, s_mbrtoc32, s_c32rtomb, s_mbrtoc8;

static size_t bad (mbstate_t *ps)
{
	memset (ps, 0, sizeof *ps);
	errno = EILSEQ;
	return ILSEQ;
}

/* UTF-8 -> one code point; the mbrtowc contract (0 for a NUL, -2 incomplete, -1 invalid). */
static size_t decode (char32_t *pc, const char *s, size_t n, mbstate_t *ps)
{
	size_t i = 0;
	unsigned need = ps->__count > 0 ? (unsigned) ps->__count : 0;
	char32_t c = need ? (char32_t) ps->__value.__wch : 0;

	if (ps->__count < 0)		/* a state of the other converters: reset */
		need = 0, c = 0;
	if (n == 0)
		return INCOMPLETE;
	if (!need) {
		unsigned char b = (unsigned char) s[i++];
		if (b < 0x80) {
			if (pc)
				*pc = b;
			memset (ps, 0, sizeof *ps);
			return b ? 1 : 0;
		}
		if (b < 0xC2 || b > 0xF4)
			return bad (ps);
		need = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
		c = b & (0x3F >> need);
		/* the second byte's range (no overlongs, no surrogates, nothing above U+10FFFF) is
		   checked below with the lead byte folded into c */
		c |= (char32_t) b << 24;	/* the lead byte, kept until the second byte */
	}
	for (; i < n && need; i++) {
		unsigned char b = (unsigned char) s[i];
		if ((b & 0xC0) != 0x80)
			return bad (ps);
		if (c >> 24) {			/* the second byte: check against the lead byte */
			unsigned lead = c >> 24;
			if ((lead == 0xE0 && b < 0xA0) || (lead == 0xED && b > 0x9F) ||
			    (lead == 0xF0 && b < 0x90) || (lead == 0xF4 && b > 0x8F))
				return bad (ps);
			c &= 0xFFFFFF;
		}
		c = (c << 6) | (b & 0x3F);
		need--;
	}
	if (need) {
		ps->__count = (int) need;
		ps->__value.__wch = c;
		return INCOMPLETE;
	}
	if (pc)
		*pc = c;
	memset (ps, 0, sizeof *ps);
	return c ? i : 0;
}

size_t mbrtoc32 (char32_t *restrict pc32, const char *restrict s, size_t n, mbstate_t *restrict ps)
{
	if (!ps)
		ps = &s_mbrtoc32;
	if (!s)
		return decode (0, "", 1, ps);
	return decode (pc32, s, n, ps);
}

size_t mbrtoc16 (char16_t *restrict pc16, const char *restrict s, size_t n, mbstate_t *restrict ps)
{
	char32_t c;
	size_t r;

	if (!ps)
		ps = &s_mbrtoc16;
	if (ps->__count == -1) {	/* the low surrogate of the last character */
		if (pc16)
			*pc16 = (char16_t) ps->__value.__wch;
		memset (ps, 0, sizeof *ps);
		return PENDING;
	}
	if (!s) {
		s = "";
		n = 1;
		pc16 = 0;
	}
	r = decode (&c, s, n, ps);
	if (r == ILSEQ || r == INCOMPLETE)
		return r;
	if (c >= 0x10000) {
		c -= 0x10000;
		if (pc16)
			*pc16 = (char16_t) (0xD800 + (c >> 10));
		ps->__count = -1;
		ps->__value.__wch = 0xDC00 + (c & 0x3FF);
	} else if (pc16)
		*pc16 = (char16_t) c;
	return r;
}

/* one code point -> UTF-8 (s has room for 4 bytes, MB_LEN_MAX) */
static size_t encode (char *s, char32_t c, mbstate_t *ps)
{
	if (c >= 0x110000 || (c >= 0xD800 && c < 0xE000))
		return bad (ps);
	memset (ps, 0, sizeof *ps);
	if (c < 0x80) {
		s[0] = (char) c;
		return 1;
	}
	if (c < 0x800) {
		s[0] = (char) (0xC0 | (c >> 6));
		s[1] = (char) (0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000) {
		s[0] = (char) (0xE0 | (c >> 12));
		s[1] = (char) (0x80 | ((c >> 6) & 0x3F));
		s[2] = (char) (0x80 | (c & 0x3F));
		return 3;
	}
	s[0] = (char) (0xF0 | (c >> 18));
	s[1] = (char) (0x80 | ((c >> 12) & 0x3F));
	s[2] = (char) (0x80 | ((c >> 6) & 0x3F));
	s[3] = (char) (0x80 | (c & 0x3F));
	return 4;
}

size_t c32rtomb (char *restrict s, char32_t c32, mbstate_t *restrict ps)
{
	char buf[4];

	if (!ps)
		ps = &s_c32rtomb;
	if (!s)
		return encode (buf, 0, ps);
	return encode (s, c32, ps);
}

size_t c16rtomb (char *restrict s, char16_t c16, mbstate_t *restrict ps)
{
	char buf[4];

	if (!ps)
		ps = &s_c16rtomb;
	if (!s) {			/* reset; an unpaired high surrogate is an error */
		int pending = ps->__count == -2;
		memset (ps, 0, sizeof *ps);
		if (pending) {
			errno = EILSEQ;
			return ILSEQ;
		}
		return 1;
	}
	if (ps->__count == -2) {
		char32_t hi = (char32_t) ps->__value.__wch;
		if (c16 < 0xDC00 || c16 > 0xDFFF)
			return bad (ps);
		return encode (s, 0x10000 + ((hi - 0xD800) << 10) + (c16 - 0xDC00), ps);
	}
	if (c16 >= 0xD800 && c16 <= 0xDBFF) {
		ps->__count = -2;
		ps->__value.__wch = c16;
		return 0;
	}
	if (c16 >= 0xDC00 && c16 <= 0xDFFF)
		return bad (ps);
	return encode (s ? s : buf, c16, ps);
}

/* C23 / C++20: UTF-8 is the multibyte encoding, so these copy, one code unit per call */
size_t mbrtoc8 (unsigned char *restrict pc8, const char *restrict s, size_t n, mbstate_t *restrict ps)
{
	char32_t c;
	size_t r;

	if (!ps)
		ps = &s_mbrtoc8;
	if (ps->__count == -3) {
		unsigned left = ps->__value.__wchb[0], total = left >> 4;
		left &= 15;
		if (pc8)
			*pc8 = ps->__value.__wchb[1 + total - left];
		if (--left)
			ps->__value.__wchb[0] = (unsigned char) ((total << 4) | left);
		else
			memset (ps, 0, sizeof *ps);
		return PENDING;
	}
	if (!s) {
		s = "";
		n = 1;
		pc8 = 0;
	}
	r = decode (&c, s, n, ps);
	if (r == ILSEQ || r == INCOMPLETE)
		return r;
	{
		char u[4];
		mbstate_t tmp;
		size_t len;
		memset (&tmp, 0, sizeof tmp);
		len = encode (u, c, &tmp);
		if (pc8)
			*pc8 = (unsigned char) u[0];
		if (len > 1) {
			ps->__count = -3;
			ps->__value.__wchb[0] = (unsigned char) (((len - 1) << 4) | (len - 1));
			memcpy (&ps->__value.__wchb[1], u + 1, len - 1);
		}
	}
	return r;
}

size_t c8rtomb (char *restrict s, unsigned char c8, mbstate_t *restrict ps)
{
	static mbstate_t s_c8rtomb;
	char32_t c;
	size_t r;

	if (!ps)
		ps = &s_c8rtomb;
	if (!s) {
		memset (ps, 0, sizeof *ps);
		return 1;
	}
	r = decode (&c, (const char *) &c8, 1, ps);
	if (r == ILSEQ)
		return r;
	if (r == INCOMPLETE)
		return 0;		/* stored: no bytes written yet */
	return encode (s, c, ps);
}
