/*
 * This file is part of Hubbub.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 * Copyright 2008 Andrew Sidwell <takkaria@netsurf-browser.org>
 *
 * Onyx: rewritten after the current HTML standard's tokenization (WHATWG, section 13.2.5):
 * every state of the standard (the script data escaped / double escaped states, the comment
 * "<!--" states, the doctype keyword states, CDATA sections, the character reference states),
 * the whole named character references table (2231 names: onyx_entities.inc), CR / CRLF made
 * LF, duplicate attributes dropped. The text runs are handed to the tree builder straight from
 * the input stream's buffer (no copy); the other strings of a token are gathered in one buffer.
 * The interface (hubbub_tokeniser_*, the token handler, pausing, the inserted chunks of
 * document.write) is the one the old tokeniser had.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <parserutils/charset/utf8.h>

#include "utils/parserutilserror.h"
#include "utils/utils.h"

#include "hubbub/errors.h"
#include "tokeniser/tokeniser.h"

#include "tokeniser/onyx_entities.inc"

/** The tokeniser's states (the standard's names) */
typedef enum hubbub_tokeniser_state {
	S_DATA,
	S_RCDATA,
	S_RAWTEXT,
	S_SCRIPT_DATA,
	S_PLAINTEXT,
	S_TAG_OPEN,
	S_END_TAG_OPEN,
	S_TAG_NAME,
	S_RCDATA_LT,
	S_RCDATA_END_TAG_OPEN,
	S_RCDATA_END_TAG_NAME,
	S_RAWTEXT_LT,
	S_RAWTEXT_END_TAG_OPEN,
	S_RAWTEXT_END_TAG_NAME,
	S_SCRIPT_LT,
	S_SCRIPT_END_TAG_OPEN,
	S_SCRIPT_END_TAG_NAME,
	S_SCRIPT_ESCAPE_START,
	S_SCRIPT_ESCAPE_START_DASH,
	S_SCRIPT_ESCAPED,
	S_SCRIPT_ESCAPED_DASH,
	S_SCRIPT_ESCAPED_DASH_DASH,
	S_SCRIPT_ESCAPED_LT,
	S_SCRIPT_ESCAPED_END_TAG_OPEN,
	S_SCRIPT_ESCAPED_END_TAG_NAME,
	S_SCRIPT_DOUBLE_ESCAPE_START,
	S_SCRIPT_DOUBLE_ESCAPED,
	S_SCRIPT_DOUBLE_ESCAPED_DASH,
	S_SCRIPT_DOUBLE_ESCAPED_DASH_DASH,
	S_SCRIPT_DOUBLE_ESCAPED_LT,
	S_SCRIPT_DOUBLE_ESCAPE_END,
	S_BEFORE_ATTR_NAME,
	S_ATTR_NAME,
	S_AFTER_ATTR_NAME,
	S_BEFORE_ATTR_VALUE,
	S_ATTR_VALUE_DQ,
	S_ATTR_VALUE_SQ,
	S_ATTR_VALUE_UQ,
	S_AFTER_ATTR_VALUE_Q,
	S_SELF_CLOSING_START_TAG,
	S_BOGUS_COMMENT,
	S_MARKUP_DECLARATION_OPEN,
	S_COMMENT_START,
	S_COMMENT_START_DASH,
	S_COMMENT,
	S_COMMENT_LT,
	S_COMMENT_LT_BANG,
	S_COMMENT_LT_BANG_DASH,
	S_COMMENT_LT_BANG_DASH_DASH,
	S_COMMENT_END_DASH,
	S_COMMENT_END,
	S_COMMENT_END_BANG,
	S_DOCTYPE,
	S_BEFORE_DOCTYPE_NAME,
	S_DOCTYPE_NAME,
	S_AFTER_DOCTYPE_NAME,
	S_AFTER_DOCTYPE_PUBLIC_KW,
	S_BEFORE_DOCTYPE_PUBLIC_ID,
	S_DOCTYPE_PUBLIC_ID_DQ,
	S_DOCTYPE_PUBLIC_ID_SQ,
	S_AFTER_DOCTYPE_PUBLIC_ID,
	S_BETWEEN_DOCTYPE_IDS,
	S_AFTER_DOCTYPE_SYSTEM_KW,
	S_BEFORE_DOCTYPE_SYSTEM_ID,
	S_DOCTYPE_SYSTEM_ID_DQ,
	S_DOCTYPE_SYSTEM_ID_SQ,
	S_AFTER_DOCTYPE_SYSTEM_ID,
	S_BOGUS_DOCTYPE,
	S_CDATA_SECTION,
	S_CDATA_SECTION_BRACKET,
	S_CDATA_SECTION_END,
	S_CHARREF,
	S_NAMED_CHARREF,
	S_NUMERIC_CHARREF,
	S_HEX_CHARREF_START,
	S_DEC_CHARREF_START,
	S_HEX_CHARREF,
	S_DEC_CHARREF,
	S_NUMERIC_CHARREF_END,
	S_DONE
} hubbub_tokeniser_state;

#define EOFCH 0xFFFFFFFFu

/** A string of the token being built: an offset and a length in tok->tb */
typedef struct tstr {
	size_t off;
	size_t len;
} tstr;

typedef struct tattr {
	tstr name;
	tstr value;
} tattr;

struct hubbub_tokeniser {
	hubbub_tokeniser_state state;
	hubbub_tokeniser_state return_state;	/**< after a character reference */
	bool process_cdata_section;	/**< the adjusted current node is foreign */
	bool paused;

	parserutils_inputstream *input;
	parserutils_buffer *insert_buf;	/**< document.write while paused / in a token */

	/* the token being built: its strings in tb */
	uint8_t *tb;
	size_t tb_len, tb_cap;
	hubbub_token_type tag_type;
	tstr tag_name;
	bool self_closing;
	tattr *attrs;
	uint32_t n_attrs, attrs_cap;
	bool attr_dup;			/**< the current attribute is a duplicate */
	hubbub_attribute *out_attrs;
	uint32_t out_cap;
	tstr comment;
	tstr dt_name, dt_public, dt_system;
	bool dt_name_set, dt_public_set, dt_system_set, dt_force_quirks;

	/* the standard's "temporary buffer" (end tag names in raw text, escapes, references) */
	uint8_t *tmp;
	size_t tmp_len, tmp_cap;

	uint8_t last_start[64];		/**< the last start tag's name */
	size_t last_start_len;

	uint32_t charref;		/**< numeric character reference */
	uint8_t charref_x;		/**< its 'x' or 'X' (hexadecimal) */

	hubbub_token_handler token_handler;
	void *token_pw;
	hubbub_error_handler error_handler;
	void *error_pw;
};

/* ---- buffers ---------------------------------------------------------------------- */

static bool grow(uint8_t **p, size_t *cap, size_t need)
{
	size_t n = *cap ? *cap : 256;
	uint8_t *q;
	while (n < need)
		n *= 2;
	q = realloc(*p, n);
	if (q == NULL)
		return false;
	*p = q;
	*cap = n;
	return true;
}

static inline bool tb_put(hubbub_tokeniser *t, const uint8_t *s, size_t n)
{
	if (t->tb_len + n > t->tb_cap && !grow(&t->tb, &t->tb_cap, t->tb_len + n))
		return false;
	memcpy(t->tb + t->tb_len, s, n);
	t->tb_len += n;
	return true;
}

static inline size_t utf8_encode(uint32_t c, uint8_t *b)
{
	if (c < 0x80) { b[0] = c; return 1; }
	if (c < 0x800) { b[0] = 0xC0 | (c >> 6); b[1] = 0x80 | (c & 0x3F); return 2; }
	if (c < 0x10000) {
		b[0] = 0xE0 | (c >> 12); b[1] = 0x80 | ((c >> 6) & 0x3F);
		b[2] = 0x80 | (c & 0x3F); return 3;
	}
	b[0] = 0xF0 | (c >> 18); b[1] = 0x80 | ((c >> 12) & 0x3F);
	b[2] = 0x80 | ((c >> 6) & 0x3F); b[3] = 0x80 | (c & 0x3F);
	return 4;
}

/* append a code point to a token string (which must be the last one in tb) */
static inline bool str_putc(hubbub_tokeniser *t, tstr *s, uint32_t c)
{
	uint8_t b[4];
	size_t n = utf8_encode(c, b);
	if (!tb_put(t, b, n))
		return false;
	s->len += n;
	return true;
}

static inline bool str_put(hubbub_tokeniser *t, tstr *s, const uint8_t *p, size_t n)
{
	if (!tb_put(t, p, n))
		return false;
	s->len += n;
	return true;
}

static inline void str_start(hubbub_tokeniser *t, tstr *s)
{
	s->off = t->tb_len;
	s->len = 0;
}

static inline bool tmp_putc(hubbub_tokeniser *t, uint32_t c)
{
	uint8_t b[4];
	size_t n = utf8_encode(c, b);
	if (t->tmp_len + n > t->tmp_cap && !grow(&t->tmp, &t->tmp_cap, t->tmp_len + n))
		return false;
	memcpy(t->tmp + t->tmp_len, b, n);
	t->tmp_len += n;
	return true;
}

/* ---- the input -------------------------------------------------------------------- */

/* the bytes available after the cursor (at least one), or NEEDDATA / EOF */
static inline parserutils_error avail(hubbub_tokeniser *t, size_t off)
{
	const uint8_t *p;
	size_t l;
	parserutils_error e;
	while (t->input->utf8->length - t->input->cursor <= off) {
		e = parserutils_inputstream_peek_slow(t->input,
				t->input->utf8->length - t->input->cursor, &p, &l);
		if (e != PARSERUTILS_OK)
			return e;
	}
	return PARSERUTILS_OK;
}

static inline const uint8_t *cur(hubbub_tokeniser *t)
{
	return t->input->utf8->data + t->input->cursor;
}

static inline void advance(hubbub_tokeniser *t, size_t n)
{
	t->input->cursor += n;
}

static inline uint32_t utf8_decode(const uint8_t *p, size_t *len)
{
	uint8_t c = p[0];
	if (c < 0x80) { *len = 1; return c; }
	if (c < 0xE0) { *len = 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F); }
	if (c < 0xF0) {
		*len = 3;
		return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
	}
	*len = 4;
	return ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
}

/**
 * The next input character (not consumed): *c, its byte length *len. A CR followed by a LF
 * is dropped (consumed at once), a lone CR reads as LF. PARSERUTILS_OK, _NEEDDATA (more to
 * come: stop here), _EOF (*c = EOFCH), or an error.
 */
static inline parserutils_error next_char(hubbub_tokeniser *t, uint32_t *c, size_t *len)
{
	parserutils_error e;
	const uint8_t *p;
	size_t need;

	if (t->input->cursor >= t->input->utf8->length) {
		e = avail(t, 0);
		if (e == PARSERUTILS_EOF) {
			*c = EOFCH;
			*len = 0;
			return PARSERUTILS_OK;
		}
		if (e != PARSERUTILS_OK)
			return e;
	}
	p = cur(t);
	if (p[0] < 0x80) {
		if (p[0] == '\r') {
			e = avail(t, 1);
			if (e == PARSERUTILS_OK) {
				p = cur(t);
				if (p[1] == '\n')
					advance(t, 1);
			} else if (e != PARSERUTILS_EOF) {
				return e;
			}
			*c = '\n';
			*len = 1;
			return PARSERUTILS_OK;
		}
		*c = p[0];
		*len = 1;
		return PARSERUTILS_OK;
	}
	need = p[0] < 0xE0 ? 2 : p[0] < 0xF0 ? 3 : 4;
	if (t->input->utf8->length - t->input->cursor < need) {
		e = avail(t, need - 1);
		if (e == PARSERUTILS_EOF) {
			*c = 0xFFFD;
			*len = t->input->utf8->length - t->input->cursor;
			return PARSERUTILS_OK;
		}
		if (e != PARSERUTILS_OK)
			return e;
		p = cur(t);
	}
	*c = utf8_decode(p, len);
	return PARSERUTILS_OK;
}

/** the byte at offset off after the cursor (ASCII look-ahead): OK, NEEDDATA or EOF */
static inline parserutils_error peek_byte(hubbub_tokeniser *t, size_t off, uint8_t *b)
{
	parserutils_error e = avail(t, off);
	if (e != PARSERUTILS_OK)
		return e;
	*b = cur(t)[off];
	return PARSERUTILS_OK;
}

/** do the next n bytes match s (ASCII, case-insensitive if ci)? 1 yes, 0 no, -1 need data */
static int lookahead(hubbub_tokeniser *t, size_t off, const char *s, size_t n, bool ci)
{
	for (size_t i = 0; i < n; i++) {
		uint8_t b;
		parserutils_error e = peek_byte(t, off + i, &b);
		if (e == PARSERUTILS_NEEDDATA)
			return -1;
		if (e != PARSERUTILS_OK)
			return 0;
		if (ci && b >= 'A' && b <= 'Z')
			b += 32;
		if (b != (uint8_t) s[i])
			return 0;
	}
	return 1;
}

/* ---- emitting tokens -------------------------------------------------------------- */

static hubbub_error emit(hubbub_tokeniser *t, const hubbub_token *token)
{
	hubbub_error err = HUBBUB_OK;

	if (t->token_handler)
		err = t->token_handler(token, t->token_pw);

	/* document.write from the handler: in at the cursor (after this token) */
	if (t->insert_buf->length > 0 && err != HUBBUB_PAUSED) {
		parserutils_inputstream_insert(t->input, t->insert_buf->data,
				t->insert_buf->length);
		parserutils_buffer_discard(t->insert_buf, 0, t->insert_buf->length);
	}
	if (err == HUBBUB_PAUSED)
		t->paused = true;
	return err;
}

static inline hubbub_error emit_chars(hubbub_tokeniser *t, const uint8_t *p, size_t n)
{
	hubbub_token token;
	if (n == 0)
		return HUBBUB_OK;
	token.type = HUBBUB_TOKEN_CHARACTER;
	token.data.character.ptr = p;
	token.data.character.len = n;
	return emit(t, &token);
}

static inline hubbub_error emit_cp(hubbub_tokeniser *t, uint32_t c)
{
	uint8_t b[4];
	size_t n = utf8_encode(c, b);
	return emit_chars(t, b, n);
}

/* emit the n bytes at the cursor as characters, and consume them */
static inline hubbub_error emit_run(hubbub_tokeniser *t, size_t n)
{
	hubbub_token token;
	if (n == 0)
		return HUBBUB_OK;
	token.type = HUBBUB_TOKEN_CHARACTER;
	token.data.character.ptr = cur(t);
	token.data.character.len = n;
	advance(t, n);
	return emit(t, &token);
}

static void start_tag(hubbub_tokeniser *t, hubbub_token_type type)
{
	t->tb_len = 0;
	t->tag_type = type;
	str_start(t, &t->tag_name);
	t->self_closing = false;
	t->n_attrs = 0;
	t->attr_dup = false;
}

static void end_attr(hubbub_tokeniser *t)
{
	/* a duplicate attribute is dropped (the first one wins) */
	if (t->attr_dup) {
		t->n_attrs--;
		t->attr_dup = false;
	}
}

static bool start_attr(hubbub_tokeniser *t)
{
	end_attr(t);
	if (t->n_attrs == t->attrs_cap) {
		uint32_t n = t->attrs_cap ? t->attrs_cap * 2 : 16;
		tattr *a = realloc(t->attrs, n * sizeof(tattr));
		if (a == NULL)
			return false;
		t->attrs = a;
		t->attrs_cap = n;
	}
	str_start(t, &t->attrs[t->n_attrs].name);
	t->attrs[t->n_attrs].value.off = 0;
	t->attrs[t->n_attrs].value.len = 0;
	t->n_attrs++;
	return true;
}

/* the attribute's name is complete: a duplicate of an earlier one? */
static void check_attr_name(hubbub_tokeniser *t)
{
	tattr *a = &t->attrs[t->n_attrs - 1];
	for (uint32_t i = 0; i + 1 < t->n_attrs; i++) {
		if (t->attrs[i].name.len == a->name.len &&
				memcmp(t->tb + t->attrs[i].name.off, t->tb + a->name.off,
						a->name.len) == 0) {
			t->attr_dup = true;
			return;
		}
	}
}

static inline void start_value(hubbub_tokeniser *t)
{
	str_start(t, &t->attrs[t->n_attrs - 1].value);
}

static hubbub_error emit_tag(hubbub_tokeniser *t)
{
	hubbub_token token;
	uint32_t i;

	end_attr(t);
	if (t->n_attrs > t->out_cap) {
		hubbub_attribute *a = realloc(t->out_attrs, t->n_attrs * sizeof(*a));
		if (a == NULL)
			return HUBBUB_NOMEM;
		t->out_attrs = a;
		t->out_cap = t->n_attrs;
	}
	for (i = 0; i < t->n_attrs; i++) {
		t->out_attrs[i].ns = HUBBUB_NS_NULL;
		t->out_attrs[i].name.ptr = t->tb + t->attrs[i].name.off;
		t->out_attrs[i].name.len = t->attrs[i].name.len;
		t->out_attrs[i].value.ptr = t->tb + t->attrs[i].value.off;
		t->out_attrs[i].value.len = t->attrs[i].value.len;
	}
	token.type = t->tag_type;
	token.data.tag.ns = HUBBUB_NS_HTML;
	token.data.tag.name.ptr = t->tb + t->tag_name.off;
	token.data.tag.name.len = t->tag_name.len;
	token.data.tag.n_attributes = t->n_attrs;
	token.data.tag.attributes = t->n_attrs ? t->out_attrs : NULL;
	token.data.tag.self_closing = t->self_closing;
	if (t->tag_type == HUBBUB_TOKEN_START_TAG) {
		if (t->tag_name.len < sizeof(t->last_start)) {
			memcpy(t->last_start, t->tb + t->tag_name.off, t->tag_name.len);
			t->last_start_len = t->tag_name.len;
		} else {
			t->last_start_len = 0;
		}
	} else {
		/* an end tag's attributes and self-closing flag are errors, and dropped */
		token.data.tag.n_attributes = 0;
		token.data.tag.attributes = NULL;
		token.data.tag.self_closing = false;
	}
	return emit(t, &token);
}

static hubbub_error emit_comment(hubbub_tokeniser *t)
{
	hubbub_token token;
	token.type = HUBBUB_TOKEN_COMMENT;
	token.data.comment.ptr = t->tb + t->comment.off;
	token.data.comment.len = t->comment.len;
	return emit(t, &token);
}

static void start_doctype(hubbub_tokeniser *t)
{
	t->tb_len = 0;
	t->dt_name_set = t->dt_public_set = t->dt_system_set = false;
	t->dt_force_quirks = false;
	t->dt_name.len = t->dt_public.len = t->dt_system.len = 0;
}

static hubbub_error emit_doctype(hubbub_tokeniser *t)
{
	hubbub_token token;
	token.type = HUBBUB_TOKEN_DOCTYPE;
	token.data.doctype.name.ptr = t->dt_name_set ? t->tb + t->dt_name.off : NULL;
	token.data.doctype.name.len = t->dt_name_set ? t->dt_name.len : 0;
	token.data.doctype.public_missing = !t->dt_public_set;
	token.data.doctype.public_id.ptr = t->dt_public_set ? t->tb + t->dt_public.off : NULL;
	token.data.doctype.public_id.len = t->dt_public_set ? t->dt_public.len : 0;
	token.data.doctype.system_missing = !t->dt_system_set;
	token.data.doctype.system_id.ptr = t->dt_system_set ? t->tb + t->dt_system.off : NULL;
	token.data.doctype.system_id.len = t->dt_system_set ? t->dt_system.len : 0;
	token.data.doctype.force_quirks = t->dt_force_quirks;
	return emit(t, &token);
}

static hubbub_error emit_eof(hubbub_tokeniser *t)
{
	hubbub_token token;
	token.type = HUBBUB_TOKEN_EOF;
	t->state = S_DONE;
	return emit(t, &token);
}

/* ---- the API ---------------------------------------------------------------------- */

hubbub_error hubbub_tokeniser_create(parserutils_inputstream *input,
		hubbub_tokeniser **tokeniser)
{
	parserutils_error perror;
	hubbub_tokeniser *tok;

	if (input == NULL || tokeniser == NULL)
		return HUBBUB_BADPARM;

	tok = calloc(1, sizeof(hubbub_tokeniser));
	if (tok == NULL)
		return HUBBUB_NOMEM;

	perror = parserutils_buffer_create(&tok->insert_buf);
	if (perror != PARSERUTILS_OK) {
		free(tok);
		return hubbub_error_from_parserutils_error(perror);
	}

	tok->state = S_DATA;
	tok->input = input;
	*tokeniser = tok;
	return HUBBUB_OK;
}

hubbub_error hubbub_tokeniser_destroy(hubbub_tokeniser *tokeniser)
{
	if (tokeniser == NULL)
		return HUBBUB_BADPARM;
	parserutils_buffer_destroy(tokeniser->insert_buf);
	free(tokeniser->tb);
	free(tokeniser->tmp);
	free(tokeniser->attrs);
	free(tokeniser->out_attrs);
	free(tokeniser);
	return HUBBUB_OK;
}

hubbub_error hubbub_tokeniser_setopt(hubbub_tokeniser *tokeniser,
		hubbub_tokeniser_opttype type,
		hubbub_tokeniser_optparams *params)
{
	hubbub_error err = HUBBUB_OK;

	if (tokeniser == NULL || params == NULL)
		return HUBBUB_BADPARM;

	switch (type) {
	case HUBBUB_TOKENISER_TOKEN_HANDLER:
		tokeniser->token_handler = params->token_handler.handler;
		tokeniser->token_pw = params->token_handler.pw;
		break;
	case HUBBUB_TOKENISER_ERROR_HANDLER:
		tokeniser->error_handler = params->error_handler.handler;
		tokeniser->error_pw = params->error_handler.pw;
		break;
	case HUBBUB_TOKENISER_CONTENT_MODEL:
		switch (params->content_model.model) {
		case HUBBUB_CONTENT_MODEL_PCDATA: tokeniser->state = S_DATA; break;
		case HUBBUB_CONTENT_MODEL_RCDATA: tokeniser->state = S_RCDATA; break;
		case HUBBUB_CONTENT_MODEL_CDATA: tokeniser->state = S_RAWTEXT; break;
		case HUBBUB_CONTENT_MODEL_PLAINTEXT: tokeniser->state = S_PLAINTEXT; break;
		case HUBBUB_CONTENT_MODEL_SCRIPTDATA: tokeniser->state = S_SCRIPT_DATA; break;
		case HUBBUB_CONTENT_MODEL_CDATA_SECTION: tokeniser->state = S_CDATA_SECTION; break;
		}
		break;
	case HUBBUB_TOKENISER_PROCESS_CDATA:
		tokeniser->process_cdata_section = params->process_cdata;
		break;
	case HUBBUB_TOKENISER_LAST_START_TAG:
		tokeniser->last_start_len = 0;
		if (params->last_start_tag != NULL &&
				strlen(params->last_start_tag) < sizeof(tokeniser->last_start)) {
			tokeniser->last_start_len = strlen(params->last_start_tag);
			memcpy(tokeniser->last_start, params->last_start_tag,
					tokeniser->last_start_len);
		}
		break;
	case HUBBUB_TOKENISER_PAUSE:
		if (params->pause_parse == true) {
			tokeniser->paused = true;
		} else if (tokeniser->paused == true) {
			tokeniser->paused = false;
			/* what document.write gave while paused goes in first */
			if (tokeniser->insert_buf->length > 0) {
				parserutils_inputstream_insert(tokeniser->input,
						tokeniser->insert_buf->data,
						tokeniser->insert_buf->length);
				parserutils_buffer_discard(tokeniser->insert_buf, 0,
						tokeniser->insert_buf->length);
			}
			err = hubbub_tokeniser_run(tokeniser);
		}
		break;
	}

	return err;
}

hubbub_error hubbub_tokeniser_insert_chunk(hubbub_tokeniser *tokeniser,
		const uint8_t *data, size_t len)
{
	parserutils_error perror;

	if (tokeniser == NULL || data == NULL)
		return HUBBUB_BADPARM;

	perror = parserutils_buffer_append(tokeniser->insert_buf, data, len);
	if (perror != PARSERUTILS_OK)
		return hubbub_error_from_parserutils_error(perror);
	return HUBBUB_OK;
}

/* ---- the state machine ------------------------------------------------------------ */

#define IS_WS(c) ((c) == '\t' || (c) == '\n' || (c) == '\f' || (c) == ' ')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_ALPHA(c) (IS_UPPER(c) || IS_LOWER(c))
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_HEX(c) (IS_DIGIT(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))

/* the text states' fast path: the bytes up to a stop character go out as one token */
static const uint8_t stop_data[256] = { ['<'] = 1, ['&'] = 1, ['\r'] = 1, [0] = 1 };
static const uint8_t stop_raw[256] = { ['<'] = 1, ['\r'] = 1, [0] = 1 };
static const uint8_t stop_plain[256] = { ['\r'] = 1, [0] = 1 };
static const uint8_t stop_escaped[256] = { ['<'] = 1, ['-'] = 1, ['\r'] = 1, [0] = 1 };
static const uint8_t stop_cdata[256] = { [']'] = 1, ['\r'] = 1, [0] = 1 };

/**
 * Emit the run of bytes before the next stop character (or the buffer's end) at the cursor.
 * Returns HUBBUB_OK when the cursor is at a stop character or at the buffer's end.
 */
static inline hubbub_error text_run(hubbub_tokeniser *t, const uint8_t *stop)
{
	const uint8_t *p = cur(t), *s = p;
	const uint8_t *end = t->input->utf8->data + t->input->utf8->length;
	while (p < end && !stop[*p])
		p++;
	return emit_run(t, p - s);
}

static bool in_attr(hubbub_tokeniser_state s)
{
	return s == S_ATTR_VALUE_DQ || s == S_ATTR_VALUE_SQ || s == S_ATTR_VALUE_UQ;
}

/* the code points of a character reference: into the attribute value or out as text */
static hubbub_error flush_ref(hubbub_tokeniser *t, const uint8_t *p, size_t n)
{
	if (in_attr(t->return_state))
		return str_put(t, &t->attrs[t->n_attrs - 1].value, p, n) ? HUBBUB_OK : HUBBUB_NOMEM;
	return emit_chars(t, p, n);
}

static hubbub_error flush_ref_cp(hubbub_tokeniser *t, uint32_t c)
{
	uint8_t b[4];
	return flush_ref(t, b, utf8_encode(c, b));
}

/** is the tag name being built the last start tag's (an "appropriate end tag")? */
static inline bool appropriate_end_tag(hubbub_tokeniser *t)
{
	return t->last_start_len != 0 && t->tag_name.len == t->last_start_len &&
			memcmp(t->tb + t->tag_name.off, t->last_start, t->last_start_len) == 0;
}

static const struct onyx_entity *entity_find(const uint8_t *s, size_t n)
{
	size_t lo = 0, hi = ONYX_ENTITY_COUNT;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		const struct onyx_entity *e = &onyx_entities[mid];
		size_t m = e->len < n ? e->len : n;
		int r = memcmp(onyx_entity_names + e->off, s, m);
		if (r == 0)
			r = (int) e->len - (int) n;
		if (r == 0)
			return e;
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

/* the Windows-1252 code points of the numeric references 0x80 - 0x9F */
static const uint16_t c1_table[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

/* the end-tag-name states of RCDATA / RAWTEXT / script data (and script escaped) */
static hubbub_error end_tag_name_char(hubbub_tokeniser *t, uint32_t c, size_t len,
		hubbub_tokeniser_state text_state, bool *done)
{
	hubbub_error err;
	*done = false;
	if ((IS_WS(c) || c == '/' || c == '>') && appropriate_end_tag(t)) {
		advance(t, len);
		if (c == '>') {
			t->state = S_DATA;
			*done = true;
			return emit_tag(t);
		}
		t->state = IS_WS(c) ? S_BEFORE_ATTR_NAME : S_SELF_CLOSING_START_TAG;
		*done = true;
		return HUBBUB_OK;
	}
	if (IS_ALPHA(c)) {
		str_putc(t, &t->tag_name, IS_UPPER(c) ? c + 32 : c);
		tmp_putc(t, c);
		advance(t, len);
		*done = true;
		return HUBBUB_OK;
	}
	/* anything else: "</" and the temporary buffer are text; reconsume */
	t->state = text_state;
	err = emit_chars(t, (const uint8_t *) "</", 2);
	if (err != HUBBUB_OK)
		return err;
	err = emit_chars(t, t->tmp, t->tmp_len);
	return err;
}

hubbub_error hubbub_tokeniser_run(hubbub_tokeniser *t)
{
	hubbub_error err = HUBBUB_OK;
	parserutils_error perr;
	uint32_t c;
	size_t len;

	if (t == NULL)
		return HUBBUB_BADPARM;
	if (t->paused)
		return HUBBUB_PAUSED;

#define NEXT() do { \
		perr = next_char(t, &c, &len); \
		if (perr != PARSERUTILS_OK) \
			goto input_error; \
	} while (0)
#define CHECK(e) do { err = (e); if (err != HUBBUB_OK) return err; } while (0)
#define TAGNAME_PUTC(ch) str_putc(t, &t->tag_name, (ch))

	for (;;) {
		switch (t->state) {

		/* -- the text states ---------------------------------------------- */
		case S_DATA:
			CHECK(text_run(t, stop_data));
			NEXT();
			if (c == '&') {
				advance(t, len);
				t->return_state = S_DATA;
				t->state = S_CHARREF;
			} else if (c == '<') {
				advance(t, len);
				t->state = S_TAG_OPEN;
			} else if (c == 0) {
				/* a NUL goes to the tree builder alone (it drops or replaces it) */
				advance(t, len);
				CHECK(emit_chars(t, (const uint8_t *) "", 1));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				advance(t, len);
				CHECK(emit_cp(t, c));
			}
			break;

		case S_RCDATA:
			CHECK(text_run(t, stop_data));
			NEXT();
			if (c == '&') {
				advance(t, len);
				t->return_state = S_RCDATA;
				t->state = S_CHARREF;
			} else if (c == '<') {
				advance(t, len);
				t->state = S_RCDATA_LT;
			} else if (c == 0) {
				advance(t, len);
				CHECK(emit_cp(t, 0xFFFD));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				advance(t, len);
				CHECK(emit_cp(t, c));
			}
			break;

		case S_RAWTEXT:
		case S_SCRIPT_DATA:
			CHECK(text_run(t, stop_raw));
			NEXT();
			if (c == '<') {
				advance(t, len);
				t->state = t->state == S_RAWTEXT ? S_RAWTEXT_LT : S_SCRIPT_LT;
			} else if (c == 0) {
				advance(t, len);
				CHECK(emit_cp(t, 0xFFFD));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				advance(t, len);
				CHECK(emit_cp(t, c));
			}
			break;

		case S_PLAINTEXT:
			CHECK(text_run(t, stop_plain));
			NEXT();
			if (c == 0) {
				advance(t, len);
				CHECK(emit_cp(t, 0xFFFD));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				advance(t, len);
				CHECK(emit_cp(t, c));
			}
			break;

		/* -- tags ------------------------------------------------------------- */
		case S_TAG_OPEN:
			NEXT();
			if (c == '!') {
				advance(t, len);
				t->state = S_MARKUP_DECLARATION_OPEN;
			} else if (c == '/') {
				advance(t, len);
				t->state = S_END_TAG_OPEN;
			} else if (IS_ALPHA(c)) {
				start_tag(t, HUBBUB_TOKEN_START_TAG);
				t->state = S_TAG_NAME;
			} else if (c == '?') {
				t->tb_len = 0;
				str_start(t, &t->comment);
				t->state = S_BOGUS_COMMENT;
			} else if (c == EOFCH) {
				CHECK(emit_chars(t, (const uint8_t *) "<", 1));
				return emit_eof(t);
			} else {
				t->state = S_DATA;
				CHECK(emit_chars(t, (const uint8_t *) "<", 1));
			}
			break;

		case S_END_TAG_OPEN:
			NEXT();
			if (IS_ALPHA(c)) {
				start_tag(t, HUBBUB_TOKEN_END_TAG);
				t->state = S_TAG_NAME;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
			} else if (c == EOFCH) {
				CHECK(emit_chars(t, (const uint8_t *) "</", 2));
				return emit_eof(t);
			} else {
				t->tb_len = 0;
				str_start(t, &t->comment);
				t->state = S_BOGUS_COMMENT;
			}
			break;

		case S_TAG_NAME: {
			/* fast path: the plain ASCII name characters */
			const uint8_t *p = cur(t), *end = t->input->utf8->data + t->input->utf8->length;
			while (p < end) {
				uint8_t b = *p;
				if (IS_LOWER(b) || IS_DIGIT(b) || b == '-' || b == ':' || b == '_')
					;
				else if (IS_UPPER(b))
					;
				else
					break;
				p++;
			}
			if (p > cur(t)) {
				size_t n = p - cur(t);
				size_t o = t->tb_len;
				str_put(t, &t->tag_name, cur(t), n);
				for (size_t i = o; i < o + n; i++)
					if (IS_UPPER(t->tb[i]))
						t->tb[i] += 32;
				advance(t, n);
			}
			NEXT();
			advance(t, len);
			if (IS_WS(c)) {
				t->state = S_BEFORE_ATTR_NAME;
			} else if (c == '/') {
				t->state = S_SELF_CLOSING_START_TAG;
			} else if (c == '>') {
				t->state = S_DATA;
				CHECK(emit_tag(t));
			} else if (c == 0) {
				TAGNAME_PUTC(0xFFFD);
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				TAGNAME_PUTC(IS_UPPER(c) ? c + 32 : c);
			}
			break;
		}

		case S_RCDATA_LT:
		case S_RAWTEXT_LT:
			NEXT();
			if (c == '/') {
				advance(t, len);
				t->tmp_len = 0;
				t->state = t->state == S_RCDATA_LT ? S_RCDATA_END_TAG_OPEN :
						S_RAWTEXT_END_TAG_OPEN;
			} else {
				t->state = t->state == S_RCDATA_LT ? S_RCDATA : S_RAWTEXT;
				CHECK(emit_chars(t, (const uint8_t *) "<", 1));
			}
			break;

		case S_RCDATA_END_TAG_OPEN:
		case S_RAWTEXT_END_TAG_OPEN:
		case S_SCRIPT_END_TAG_OPEN:
		case S_SCRIPT_ESCAPED_END_TAG_OPEN:
			NEXT();
			if (IS_ALPHA(c)) {
				start_tag(t, HUBBUB_TOKEN_END_TAG);
				t->state = t->state == S_RCDATA_END_TAG_OPEN ? S_RCDATA_END_TAG_NAME :
					t->state == S_RAWTEXT_END_TAG_OPEN ? S_RAWTEXT_END_TAG_NAME :
					t->state == S_SCRIPT_END_TAG_OPEN ? S_SCRIPT_END_TAG_NAME :
					S_SCRIPT_ESCAPED_END_TAG_NAME;
			} else {
				t->state = t->state == S_RCDATA_END_TAG_OPEN ? S_RCDATA :
					t->state == S_RAWTEXT_END_TAG_OPEN ? S_RAWTEXT :
					t->state == S_SCRIPT_END_TAG_OPEN ? S_SCRIPT_DATA :
					S_SCRIPT_ESCAPED;
				CHECK(emit_chars(t, (const uint8_t *) "</", 2));
			}
			break;

		case S_RCDATA_END_TAG_NAME:
		case S_RAWTEXT_END_TAG_NAME:
		case S_SCRIPT_END_TAG_NAME:
		case S_SCRIPT_ESCAPED_END_TAG_NAME: {
			bool done;
			hubbub_tokeniser_state back =
				t->state == S_RCDATA_END_TAG_NAME ? S_RCDATA :
				t->state == S_RAWTEXT_END_TAG_NAME ? S_RAWTEXT :
				t->state == S_SCRIPT_END_TAG_NAME ? S_SCRIPT_DATA : S_SCRIPT_ESCAPED;
			NEXT();
			CHECK(end_tag_name_char(t, c, len, back, &done));
			break;
		}

		/* -- script data ---------------------------------------------------- */
		case S_SCRIPT_LT:
			NEXT();
			if (c == '/') {
				advance(t, len);
				t->tmp_len = 0;
				t->state = S_SCRIPT_END_TAG_OPEN;
			} else if (c == '!') {
				advance(t, len);
				t->state = S_SCRIPT_ESCAPE_START;
				CHECK(emit_chars(t, (const uint8_t *) "<!", 2));
			} else {
				t->state = S_SCRIPT_DATA;
				CHECK(emit_chars(t, (const uint8_t *) "<", 1));
			}
			break;

		case S_SCRIPT_ESCAPE_START:
		case S_SCRIPT_ESCAPE_START_DASH:
			NEXT();
			if (c == '-') {
				advance(t, len);
				t->state = t->state == S_SCRIPT_ESCAPE_START ?
						S_SCRIPT_ESCAPE_START_DASH : S_SCRIPT_ESCAPED_DASH_DASH;
				CHECK(emit_chars(t, (const uint8_t *) "-", 1));
			} else {
				t->state = S_SCRIPT_DATA;
			}
			break;

		case S_SCRIPT_ESCAPED:
			CHECK(text_run(t, stop_escaped));
			NEXT();
			advance(t, len);
			if (c == '-') {
				t->state = S_SCRIPT_ESCAPED_DASH;
				CHECK(emit_chars(t, (const uint8_t *) "-", 1));
			} else if (c == '<') {
				t->state = S_SCRIPT_ESCAPED_LT;
			} else if (c == 0) {
				CHECK(emit_cp(t, 0xFFFD));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				CHECK(emit_cp(t, c));
			}
			break;

		case S_SCRIPT_ESCAPED_DASH:
		case S_SCRIPT_ESCAPED_DASH_DASH:
			NEXT();
			advance(t, len);
			if (c == '-') {
				t->state = S_SCRIPT_ESCAPED_DASH_DASH;
				CHECK(emit_chars(t, (const uint8_t *) "-", 1));
			} else if (c == '<') {
				t->state = S_SCRIPT_ESCAPED_LT;
			} else if (c == '>' && t->state == S_SCRIPT_ESCAPED_DASH_DASH) {
				t->state = S_SCRIPT_DATA;
				CHECK(emit_chars(t, (const uint8_t *) ">", 1));
			} else if (c == 0) {
				t->state = S_SCRIPT_ESCAPED;
				CHECK(emit_cp(t, 0xFFFD));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				t->state = S_SCRIPT_ESCAPED;
				CHECK(emit_cp(t, c));
			}
			break;

		case S_SCRIPT_ESCAPED_LT:
			NEXT();
			if (c == '/') {
				advance(t, len);
				t->tmp_len = 0;
				t->state = S_SCRIPT_ESCAPED_END_TAG_OPEN;
			} else if (IS_ALPHA(c)) {
				t->tmp_len = 0;
				t->state = S_SCRIPT_DOUBLE_ESCAPE_START;
				CHECK(emit_chars(t, (const uint8_t *) "<", 1));
			} else {
				t->state = S_SCRIPT_ESCAPED;
				CHECK(emit_chars(t, (const uint8_t *) "<", 1));
			}
			break;

		case S_SCRIPT_DOUBLE_ESCAPE_START:
		case S_SCRIPT_DOUBLE_ESCAPE_END:
			NEXT();
			if (IS_WS(c) || c == '/' || c == '>') {
				bool is_script = t->tmp_len == 6 && memcmp(t->tmp, "script", 6) == 0;
				advance(t, len);
				if (t->state == S_SCRIPT_DOUBLE_ESCAPE_START)
					t->state = is_script ? S_SCRIPT_DOUBLE_ESCAPED : S_SCRIPT_ESCAPED;
				else
					t->state = is_script ? S_SCRIPT_ESCAPED : S_SCRIPT_DOUBLE_ESCAPED;
				CHECK(emit_cp(t, c));
			} else if (IS_ALPHA(c)) {
				advance(t, len);
				tmp_putc(t, IS_UPPER(c) ? c + 32 : c);
				CHECK(emit_cp(t, c));
			} else {
				t->state = t->state == S_SCRIPT_DOUBLE_ESCAPE_START ? S_SCRIPT_ESCAPED :
						S_SCRIPT_DOUBLE_ESCAPED;
			}
			break;

		case S_SCRIPT_DOUBLE_ESCAPED:
			CHECK(text_run(t, stop_escaped));
			NEXT();
			advance(t, len);
			if (c == '-') {
				t->state = S_SCRIPT_DOUBLE_ESCAPED_DASH;
				CHECK(emit_chars(t, (const uint8_t *) "-", 1));
			} else if (c == '<') {
				t->state = S_SCRIPT_DOUBLE_ESCAPED_LT;
				CHECK(emit_chars(t, (const uint8_t *) "<", 1));
			} else if (c == 0) {
				CHECK(emit_cp(t, 0xFFFD));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				CHECK(emit_cp(t, c));
			}
			break;

		case S_SCRIPT_DOUBLE_ESCAPED_DASH:
		case S_SCRIPT_DOUBLE_ESCAPED_DASH_DASH:
			NEXT();
			advance(t, len);
			if (c == '-') {
				t->state = S_SCRIPT_DOUBLE_ESCAPED_DASH_DASH;
				CHECK(emit_chars(t, (const uint8_t *) "-", 1));
			} else if (c == '<') {
				t->state = S_SCRIPT_DOUBLE_ESCAPED_LT;
				CHECK(emit_chars(t, (const uint8_t *) "<", 1));
			} else if (c == '>' && t->state == S_SCRIPT_DOUBLE_ESCAPED_DASH_DASH) {
				t->state = S_SCRIPT_DATA;
				CHECK(emit_chars(t, (const uint8_t *) ">", 1));
			} else if (c == 0) {
				t->state = S_SCRIPT_DOUBLE_ESCAPED;
				CHECK(emit_cp(t, 0xFFFD));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				t->state = S_SCRIPT_DOUBLE_ESCAPED;
				CHECK(emit_cp(t, c));
			}
			break;

		case S_SCRIPT_DOUBLE_ESCAPED_LT:
			NEXT();
			if (c == '/') {
				advance(t, len);
				t->tmp_len = 0;
				t->state = S_SCRIPT_DOUBLE_ESCAPE_END;
				CHECK(emit_chars(t, (const uint8_t *) "/", 1));
			} else {
				t->state = S_SCRIPT_DOUBLE_ESCAPED;
			}
			break;

		/* -- attributes ------------------------------------------------------- */
		case S_BEFORE_ATTR_NAME:
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
			} else if (c == '/' || c == '>' || c == EOFCH) {
				t->state = S_AFTER_ATTR_NAME;
			} else if (c == '=') {
				advance(t, len);
				if (!start_attr(t))
					return HUBBUB_NOMEM;
				str_putc(t, &t->attrs[t->n_attrs - 1].name, c);
				t->state = S_ATTR_NAME;
			} else {
				if (!start_attr(t))
					return HUBBUB_NOMEM;
				t->state = S_ATTR_NAME;
			}
			break;

		case S_ATTR_NAME: {
			tstr *name = &t->attrs[t->n_attrs - 1].name;
			const uint8_t *p = cur(t), *end = t->input->utf8->data + t->input->utf8->length;
			while (p < end && (IS_LOWER(*p) || IS_DIGIT(*p) || *p == '-' || *p == '_' ||
					*p == ':'))
				p++;
			if (p > cur(t)) {
				str_put(t, name, cur(t), p - cur(t));
				advance(t, p - cur(t));
			}
			NEXT();
			if (IS_WS(c) || c == '/' || c == '>' || c == EOFCH) {
				check_attr_name(t);
				t->state = S_AFTER_ATTR_NAME;
			} else if (c == '=') {
				advance(t, len);
				check_attr_name(t);
				t->state = S_BEFORE_ATTR_VALUE;
			} else {
				advance(t, len);
				if (c == 0)
					c = 0xFFFD;
				else if (IS_UPPER(c))
					c += 32;
				str_putc(t, name, c);
			}
			break;
		}

		case S_AFTER_ATTR_NAME:
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
			} else if (c == '/') {
				advance(t, len);
				t->state = S_SELF_CLOSING_START_TAG;
			} else if (c == '=') {
				advance(t, len);
				t->state = S_BEFORE_ATTR_VALUE;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_tag(t));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				if (!start_attr(t))
					return HUBBUB_NOMEM;
				t->state = S_ATTR_NAME;
			}
			break;

		case S_BEFORE_ATTR_VALUE:
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
			} else if (c == '"') {
				advance(t, len);
				start_value(t);
				t->state = S_ATTR_VALUE_DQ;
			} else if (c == '\'') {
				advance(t, len);
				start_value(t);
				t->state = S_ATTR_VALUE_SQ;
			} else if (c == '>') {
				advance(t, len);
				start_value(t);
				t->state = S_DATA;
				CHECK(emit_tag(t));
			} else {
				start_value(t);
				t->state = S_ATTR_VALUE_UQ;
			}
			break;

		case S_ATTR_VALUE_DQ:
		case S_ATTR_VALUE_SQ: {
			tstr *v = &t->attrs[t->n_attrs - 1].value;
			uint8_t q = t->state == S_ATTR_VALUE_DQ ? '"' : '\'';
			const uint8_t *p = cur(t), *end = t->input->utf8->data + t->input->utf8->length;
			while (p < end && *p != q && *p != '&' && *p != '\r' && *p != 0)
				p++;
			if (p > cur(t)) {
				if (!str_put(t, v, cur(t), p - cur(t)))
					return HUBBUB_NOMEM;
				advance(t, p - cur(t));
			}
			NEXT();
			if (c == q) {
				advance(t, len);
				t->state = S_AFTER_ATTR_VALUE_Q;
			} else if (c == '&') {
				advance(t, len);
				t->return_state = t->state;
				t->state = S_CHARREF;
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				advance(t, len);
				str_putc(t, v, c == 0 ? 0xFFFD : c);
			}
			break;
		}

		case S_ATTR_VALUE_UQ: {
			tstr *v = &t->attrs[t->n_attrs - 1].value;
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
				t->state = S_BEFORE_ATTR_NAME;
			} else if (c == '&') {
				advance(t, len);
				t->return_state = S_ATTR_VALUE_UQ;
				t->state = S_CHARREF;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_tag(t));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				advance(t, len);
				str_putc(t, v, c == 0 ? 0xFFFD : c);
			}
			break;
		}

		case S_AFTER_ATTR_VALUE_Q:
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
				t->state = S_BEFORE_ATTR_NAME;
			} else if (c == '/') {
				advance(t, len);
				t->state = S_SELF_CLOSING_START_TAG;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_tag(t));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				t->state = S_BEFORE_ATTR_NAME;
			}
			break;

		case S_SELF_CLOSING_START_TAG:
			NEXT();
			if (c == '>') {
				advance(t, len);
				t->self_closing = true;
				t->state = S_DATA;
				CHECK(emit_tag(t));
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				t->state = S_BEFORE_ATTR_NAME;
			}
			break;

		/* -- comments --------------------------------------------------------- */
		case S_BOGUS_COMMENT:
			NEXT();
			advance(t, len);
			if (c == '>') {
				t->state = S_DATA;
				CHECK(emit_comment(t));
			} else if (c == EOFCH) {
				CHECK(emit_comment(t));
				return emit_eof(t);
			} else {
				str_putc(t, &t->comment, c == 0 ? 0xFFFD : c);
			}
			break;

		case S_MARKUP_DECLARATION_OPEN: {
			int m = lookahead(t, 0, "--", 2, false);
			if (m < 0)
				return HUBBUB_OK;
			if (m) {
				advance(t, 2);
				t->tb_len = 0;
				str_start(t, &t->comment);
				t->state = S_COMMENT_START;
				break;
			}
			m = lookahead(t, 0, "doctype", 7, true);
			if (m < 0)
				return HUBBUB_OK;
			if (m) {
				advance(t, 7);
				t->state = S_DOCTYPE;
				break;
			}
			m = lookahead(t, 0, "[CDATA[", 7, false);
			if (m < 0)
				return HUBBUB_OK;
			t->tb_len = 0;
			str_start(t, &t->comment);
			if (m && t->process_cdata_section) {
				advance(t, 7);
				t->state = S_CDATA_SECTION;
			} else if (m) {
				/* a bogus comment "[CDATA[..." */
				advance(t, 7);
				str_put(t, &t->comment, (const uint8_t *) "[CDATA[", 7);
				t->state = S_BOGUS_COMMENT;
			} else {
				t->state = S_BOGUS_COMMENT;
			}
			break;
		}

		case S_COMMENT_START:
			NEXT();
			if (c == '-') {
				advance(t, len);
				t->state = S_COMMENT_START_DASH;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_comment(t));
			} else {
				t->state = S_COMMENT;
			}
			break;

		case S_COMMENT_START_DASH:
			NEXT();
			if (c == '-') {
				advance(t, len);
				t->state = S_COMMENT_END;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_comment(t));
			} else if (c == EOFCH) {
				CHECK(emit_comment(t));
				return emit_eof(t);
			} else {
				str_putc(t, &t->comment, '-');
				t->state = S_COMMENT;
			}
			break;

		case S_COMMENT: {
			const uint8_t *p = cur(t), *end = t->input->utf8->data + t->input->utf8->length;
			while (p < end && *p != '<' && *p != '-' && *p != '\r' && *p != 0)
				p++;
			if (p > cur(t)) {
				if (!str_put(t, &t->comment, cur(t), p - cur(t)))
					return HUBBUB_NOMEM;
				advance(t, p - cur(t));
			}
			NEXT();
			advance(t, len);
			if (c == '<') {
				str_putc(t, &t->comment, c);
				t->state = S_COMMENT_LT;
			} else if (c == '-') {
				t->state = S_COMMENT_END_DASH;
			} else if (c == EOFCH) {
				CHECK(emit_comment(t));
				return emit_eof(t);
			} else {
				str_putc(t, &t->comment, c == 0 ? 0xFFFD : c);
			}
			break;
		}

		case S_COMMENT_LT:
			NEXT();
			if (c == '!') {
				advance(t, len);
				str_putc(t, &t->comment, c);
				t->state = S_COMMENT_LT_BANG;
			} else if (c == '<') {
				advance(t, len);
				str_putc(t, &t->comment, c);
			} else {
				t->state = S_COMMENT;
			}
			break;

		case S_COMMENT_LT_BANG:
			NEXT();
			if (c == '-') {
				advance(t, len);
				t->state = S_COMMENT_LT_BANG_DASH;
			} else {
				t->state = S_COMMENT;
			}
			break;

		case S_COMMENT_LT_BANG_DASH:
			NEXT();
			if (c == '-') {
				advance(t, len);
				t->state = S_COMMENT_LT_BANG_DASH_DASH;
			} else {
				t->state = S_COMMENT_END_DASH;
			}
			break;

		case S_COMMENT_LT_BANG_DASH_DASH:
			NEXT();
			/* '>' or EOF: the comment's end; anything else: a nested comment (an error) */
			t->state = S_COMMENT_END;
			break;

		case S_COMMENT_END_DASH:
			NEXT();
			if (c == '-') {
				advance(t, len);
				t->state = S_COMMENT_END;
			} else if (c == EOFCH) {
				CHECK(emit_comment(t));
				return emit_eof(t);
			} else {
				str_putc(t, &t->comment, '-');
				t->state = S_COMMENT;
			}
			break;

		case S_COMMENT_END:
			NEXT();
			if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_comment(t));
			} else if (c == '!') {
				advance(t, len);
				t->state = S_COMMENT_END_BANG;
			} else if (c == '-') {
				advance(t, len);
				str_putc(t, &t->comment, '-');
			} else if (c == EOFCH) {
				CHECK(emit_comment(t));
				return emit_eof(t);
			} else {
				str_put(t, &t->comment, (const uint8_t *) "--", 2);
				t->state = S_COMMENT;
			}
			break;

		case S_COMMENT_END_BANG:
			NEXT();
			if (c == '-') {
				advance(t, len);
				str_put(t, &t->comment, (const uint8_t *) "--!", 3);
				t->state = S_COMMENT_END_DASH;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_comment(t));
			} else if (c == EOFCH) {
				CHECK(emit_comment(t));
				return emit_eof(t);
			} else {
				str_put(t, &t->comment, (const uint8_t *) "--!", 3);
				t->state = S_COMMENT;
			}
			break;

		/* -- doctypes --------------------------------------------------------- */
		case S_DOCTYPE:
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
				t->state = S_BEFORE_DOCTYPE_NAME;
			} else if (c == '>') {
				t->state = S_BEFORE_DOCTYPE_NAME;
			} else if (c == EOFCH) {
				start_doctype(t);
				t->dt_force_quirks = true;
				CHECK(emit_doctype(t));
				return emit_eof(t);
			} else {
				t->state = S_BEFORE_DOCTYPE_NAME;
			}
			break;

		case S_BEFORE_DOCTYPE_NAME:
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
			} else if (c == '>') {
				advance(t, len);
				start_doctype(t);
				t->dt_force_quirks = true;
				t->state = S_DATA;
				CHECK(emit_doctype(t));
			} else if (c == EOFCH) {
				start_doctype(t);
				t->dt_force_quirks = true;
				CHECK(emit_doctype(t));
				return emit_eof(t);
			} else {
				advance(t, len);
				start_doctype(t);
				t->dt_name_set = true;
				str_start(t, &t->dt_name);
				str_putc(t, &t->dt_name, c == 0 ? 0xFFFD : IS_UPPER(c) ? c + 32 : c);
				t->state = S_DOCTYPE_NAME;
			}
			break;

		case S_DOCTYPE_NAME:
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
				t->state = S_AFTER_DOCTYPE_NAME;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_doctype(t));
			} else if (c == EOFCH) {
				t->dt_force_quirks = true;
				CHECK(emit_doctype(t));
				return emit_eof(t);
			} else {
				advance(t, len);
				str_putc(t, &t->dt_name, c == 0 ? 0xFFFD : IS_UPPER(c) ? c + 32 : c);
			}
			break;

		case S_AFTER_DOCTYPE_NAME: {
			int m;
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
				break;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_doctype(t));
				break;
			} else if (c == EOFCH) {
				t->dt_force_quirks = true;
				CHECK(emit_doctype(t));
				return emit_eof(t);
			}
			m = lookahead(t, 0, "public", 6, true);
			if (m < 0)
				return HUBBUB_OK;
			if (m) {
				advance(t, 6);
				t->state = S_AFTER_DOCTYPE_PUBLIC_KW;
				break;
			}
			m = lookahead(t, 0, "system", 6, true);
			if (m < 0)
				return HUBBUB_OK;
			if (m) {
				advance(t, 6);
				t->state = S_AFTER_DOCTYPE_SYSTEM_KW;
				break;
			}
			t->dt_force_quirks = true;
			t->state = S_BOGUS_DOCTYPE;
			break;
		}

		case S_AFTER_DOCTYPE_PUBLIC_KW:
		case S_BEFORE_DOCTYPE_PUBLIC_ID:
		case S_AFTER_DOCTYPE_SYSTEM_KW:
		case S_BEFORE_DOCTYPE_SYSTEM_ID: {
			bool pub = t->state == S_AFTER_DOCTYPE_PUBLIC_KW ||
					t->state == S_BEFORE_DOCTYPE_PUBLIC_ID;
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
				t->state = pub ? S_BEFORE_DOCTYPE_PUBLIC_ID : S_BEFORE_DOCTYPE_SYSTEM_ID;
			} else if (c == '"' || c == '\'') {
				advance(t, len);
				if (pub) {
					t->dt_public_set = true;
					str_start(t, &t->dt_public);
					t->state = c == '"' ? S_DOCTYPE_PUBLIC_ID_DQ : S_DOCTYPE_PUBLIC_ID_SQ;
				} else {
					t->dt_system_set = true;
					str_start(t, &t->dt_system);
					t->state = c == '"' ? S_DOCTYPE_SYSTEM_ID_DQ : S_DOCTYPE_SYSTEM_ID_SQ;
				}
			} else if (c == '>') {
				advance(t, len);
				t->dt_force_quirks = true;
				t->state = S_DATA;
				CHECK(emit_doctype(t));
			} else if (c == EOFCH) {
				t->dt_force_quirks = true;
				CHECK(emit_doctype(t));
				return emit_eof(t);
			} else {
				t->dt_force_quirks = true;
				t->state = S_BOGUS_DOCTYPE;
			}
			break;
		}

		case S_DOCTYPE_PUBLIC_ID_DQ:
		case S_DOCTYPE_PUBLIC_ID_SQ:
		case S_DOCTYPE_SYSTEM_ID_DQ:
		case S_DOCTYPE_SYSTEM_ID_SQ: {
			bool pub = t->state == S_DOCTYPE_PUBLIC_ID_DQ || t->state == S_DOCTYPE_PUBLIC_ID_SQ;
			uint32_t q = (t->state == S_DOCTYPE_PUBLIC_ID_DQ ||
					t->state == S_DOCTYPE_SYSTEM_ID_DQ) ? '"' : '\'';
			tstr *s = pub ? &t->dt_public : &t->dt_system;
			NEXT();
			if (c == q) {
				advance(t, len);
				t->state = pub ? S_AFTER_DOCTYPE_PUBLIC_ID : S_AFTER_DOCTYPE_SYSTEM_ID;
			} else if (c == '>') {
				advance(t, len);
				t->dt_force_quirks = true;
				t->state = S_DATA;
				CHECK(emit_doctype(t));
			} else if (c == EOFCH) {
				t->dt_force_quirks = true;
				CHECK(emit_doctype(t));
				return emit_eof(t);
			} else {
				advance(t, len);
				str_putc(t, s, c == 0 ? 0xFFFD : c);
			}
			break;
		}

		case S_AFTER_DOCTYPE_PUBLIC_ID:
		case S_BETWEEN_DOCTYPE_IDS:
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
				t->state = S_BETWEEN_DOCTYPE_IDS;
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_doctype(t));
			} else if (c == '"' || c == '\'') {
				advance(t, len);
				t->dt_system_set = true;
				str_start(t, &t->dt_system);
				t->state = c == '"' ? S_DOCTYPE_SYSTEM_ID_DQ : S_DOCTYPE_SYSTEM_ID_SQ;
			} else if (c == EOFCH) {
				t->dt_force_quirks = true;
				CHECK(emit_doctype(t));
				return emit_eof(t);
			} else {
				t->dt_force_quirks = true;
				t->state = S_BOGUS_DOCTYPE;
			}
			break;

		case S_AFTER_DOCTYPE_SYSTEM_ID:
			NEXT();
			if (IS_WS(c)) {
				advance(t, len);
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
				CHECK(emit_doctype(t));
			} else if (c == EOFCH) {
				t->dt_force_quirks = true;
				CHECK(emit_doctype(t));
				return emit_eof(t);
			} else {
				/* not a force-quirks error */
				t->state = S_BOGUS_DOCTYPE;
			}
			break;

		case S_BOGUS_DOCTYPE:
			NEXT();
			advance(t, len);
			if (c == '>') {
				t->state = S_DATA;
				CHECK(emit_doctype(t));
			} else if (c == EOFCH) {
				CHECK(emit_doctype(t));
				return emit_eof(t);
			}
			break;

		/* -- CDATA sections ----------------------------------------------------- */
		case S_CDATA_SECTION:
			CHECK(text_run(t, stop_cdata));
			NEXT();
			if (c == ']') {
				advance(t, len);
				t->state = S_CDATA_SECTION_BRACKET;
			} else if (c == EOFCH) {
				return emit_eof(t);
			} else {
				/* a NUL stays itself here (the tree builder's foreign content replaces it) */
				advance(t, len);
				CHECK(emit_cp(t, c));
			}
			break;

		case S_CDATA_SECTION_BRACKET:
			NEXT();
			if (c == ']') {
				advance(t, len);
				t->state = S_CDATA_SECTION_END;
			} else {
				t->state = S_CDATA_SECTION;
				CHECK(emit_chars(t, (const uint8_t *) "]", 1));
			}
			break;

		case S_CDATA_SECTION_END:
			NEXT();
			if (c == ']') {
				advance(t, len);
				CHECK(emit_chars(t, (const uint8_t *) "]", 1));
			} else if (c == '>') {
				advance(t, len);
				t->state = S_DATA;
			} else {
				t->state = S_CDATA_SECTION;
				CHECK(emit_chars(t, (const uint8_t *) "]]", 2));
			}
			break;

		/* -- character references --------------------------------------------- */
		case S_CHARREF:
			NEXT();
			if (IS_ALNUM(c)) {
				t->state = S_NAMED_CHARREF;
			} else if (c == '#') {
				advance(t, len);
				t->state = S_NUMERIC_CHARREF;
			} else {
				t->state = t->return_state;
				CHECK(flush_ref(t, (const uint8_t *) "&", 1));
			}
			break;

		case S_NAMED_CHARREF: {
			/* the longest name of the table that the input starts with */
			uint8_t name[ONYX_ENTITY_MAXLEN + 2];
			size_t n = 0, k;
			bool semi = false;
			uint8_t b = 0, after;
			const struct onyx_entity *e = NULL;
			parserutils_error pe;

			for (;;) {
				pe = peek_byte(t, n, &b);
				if (pe == PARSERUTILS_NEEDDATA)
					return HUBBUB_OK;
				if (pe != PARSERUTILS_OK || !IS_ALNUM(b) || n == ONYX_ENTITY_MAXLEN)
					break;
				name[n++] = b;
			}
			if (pe == PARSERUTILS_OK && b == ';') {
				name[n] = ';';
				e = entity_find(name, n + 1);
				if (e != NULL)
					semi = true;
			}
			k = n + 1;
			if (e == NULL) {
				for (k = n < ONYX_ENTITY_LEGACY_MAXLEN ? n : ONYX_ENTITY_LEGACY_MAXLEN;
						k > 0; k--) {
					e = entity_find(name, k);
					if (e != NULL)
						break;
				}
			}
			if (e == NULL) {
				/* not a reference: "&", then the name as usual (the ambiguous
				 * ampersand state's behaviour) */
				t->state = t->return_state;
				CHECK(flush_ref(t, (const uint8_t *) "&", 1));
				break;
			}
			after = k < n ? name[k] : (pe == PARSERUTILS_OK ? b : 0);
			if (!semi && in_attr(t->return_state) && (after == '=' || IS_ALNUM(after))) {
				/* in an attribute, "&amp=" or "&ampx" stays as written */
				t->state = t->return_state;
				CHECK(flush_ref(t, (const uint8_t *) "&", 1));
				break;
			}
			advance(t, k);
			t->state = t->return_state;
			CHECK(flush_ref_cp(t, e->cp1));
			if (e->cp2)
				CHECK(flush_ref_cp(t, e->cp2));
			break;
		}

		case S_NUMERIC_CHARREF:
			t->charref = 0;
			NEXT();
			if (c == 'x' || c == 'X') {
				advance(t, len);
				t->charref_x = (uint8_t) c;
				t->state = S_HEX_CHARREF_START;
			} else {
				t->state = S_DEC_CHARREF_START;
			}
			break;

		case S_HEX_CHARREF_START:
		case S_DEC_CHARREF_START: {
			bool hex = t->state == S_HEX_CHARREF_START;
			NEXT();
			if (hex ? IS_HEX(c) : IS_DIGIT(c)) {
				t->state = hex ? S_HEX_CHARREF : S_DEC_CHARREF;
			} else {
				/* "&#" / "&#x" without digits: as written */
				uint8_t b[3] = { '&', '#', 'x' };
				t->state = t->return_state;
				if (hex)
					b[2] = t->charref_x;	/* the x or X as it was */
				CHECK(flush_ref(t, b, hex ? 3 : 2));
			}
			break;
		}

		case S_HEX_CHARREF:
		case S_DEC_CHARREF:
			NEXT();
			if (IS_DIGIT(c) || (t->state == S_HEX_CHARREF && IS_HEX(c))) {
				uint32_t d = IS_DIGIT(c) ? c - '0' : (c | 0x20) - 'a' + 10;
				advance(t, len);
				if (t->charref <= 0x10FFFF)
					t->charref = t->charref * (t->state == S_HEX_CHARREF ? 16 : 10) + d;
			} else {
				if (c == ';')
					advance(t, len);
				t->state = S_NUMERIC_CHARREF_END;
			}
			break;

		case S_NUMERIC_CHARREF_END: {
			uint32_t v = t->charref;
			if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
				v = 0xFFFD;
			else if (v >= 0x80 && v <= 0x9F)
				v = c1_table[v - 0x80];
			t->state = t->return_state;
			CHECK(flush_ref_cp(t, v));
			break;
		}

		case S_DONE:
			return HUBBUB_OK;
		}
	}

input_error:
	if (perr == PARSERUTILS_NEEDDATA)
		return HUBBUB_OK;
	return hubbub_error_from_parserutils_error(perr);

#undef NEXT
#undef CHECK
#undef TAGNAME_PUTC
}
