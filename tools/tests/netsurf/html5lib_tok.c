/*
 * tools/tests/netsurf/html5lib_tok.c -- hubbub's tokeniser driven alone, for the html5lib
 * tokenizer tests (html5lib_tok.py reads the .test JSON files, feeds this program and compares).
 *
 * stdin: records "<state>\n<last start tag>\n<byte length>\n<bytes>\n"; stdout: one line per
 * record, the tokens as a JSON array (html5lib's format; character tokens not merged).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hubbub/hubbub.h>
#include <hubbub/parser.h>

static void jstr(const uint8_t *p, size_t n)
{
	putchar('"');
	for (size_t i = 0; i < n; i++) {
		unsigned c = p[i];
		if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
		else if (c < 0x20) printf("\\u%04x", c);
		else putchar(c);
	}
	putchar('"');
}

static int first;

static hubbub_error token_handler(const hubbub_token *t, void *pw)
{
	(void) pw;
	if (t->type == HUBBUB_TOKEN_EOF)
		return HUBBUB_OK;
	if (!first) putchar(',');
	first = 0;
	switch (t->type) {
	case HUBBUB_TOKEN_DOCTYPE:
		printf("[\"DOCTYPE\",");
		if (t->data.doctype.name.len == 0 && t->data.doctype.name.ptr == NULL)
			printf("null");
		else
			jstr(t->data.doctype.name.ptr, t->data.doctype.name.len);
		putchar(',');
		if (t->data.doctype.public_missing) printf("null");
		else jstr(t->data.doctype.public_id.ptr, t->data.doctype.public_id.len);
		putchar(',');
		if (t->data.doctype.system_missing) printf("null");
		else jstr(t->data.doctype.system_id.ptr, t->data.doctype.system_id.len);
		printf(",%s]", t->data.doctype.force_quirks ? "false" : "true");
		break;
	case HUBBUB_TOKEN_START_TAG:
		printf("[\"StartTag\",");
		jstr(t->data.tag.name.ptr, t->data.tag.name.len);
		printf(",{");
		for (uint32_t i = 0; i < t->data.tag.n_attributes; i++) {
			if (i) putchar(',');
			jstr(t->data.tag.attributes[i].name.ptr, t->data.tag.attributes[i].name.len);
			putchar(':');
			jstr(t->data.tag.attributes[i].value.ptr, t->data.tag.attributes[i].value.len);
		}
		printf("}%s]", t->data.tag.self_closing ? ",true" : "");
		break;
	case HUBBUB_TOKEN_END_TAG:
		printf("[\"EndTag\",");
		jstr(t->data.tag.name.ptr, t->data.tag.name.len);
		printf("]");
		break;
	case HUBBUB_TOKEN_COMMENT:
		printf("[\"Comment\",");
		jstr(t->data.comment.ptr, t->data.comment.len);
		printf("]");
		break;
	case HUBBUB_TOKEN_CHARACTER:
		printf("[\"Character\",");
		jstr(t->data.character.ptr, t->data.character.len);
		printf("]");
		break;
	default:
		break;
	}
	return HUBBUB_OK;
}

static int read_line(char *buf, size_t n)
{
	if (!fgets(buf, n, stdin))
		return 0;
	buf[strcspn(buf, "\n")] = 0;
	return 1;
}

int main(void)
{
	char state[64], last[256], lenl[32];

	while (read_line(state, sizeof state) && read_line(last, sizeof last) &&
			read_line(lenl, sizeof lenl)) {
		size_t len = strtoul(lenl, NULL, 10);
		uint8_t *data = malloc(len + 1);
		hubbub_parser *p = NULL;
		hubbub_parser_optparams o;

		if (fread(data, 1, len, stdin) != len)
			return 1;
		getchar();

		hubbub_parser_create("UTF-8", false, &p);
		o.token_handler.handler = token_handler;
		o.token_handler.pw = NULL;
		hubbub_parser_setopt(p, HUBBUB_PARSER_TOKEN_HANDLER, &o);
/* Onyx: the tokeniser's initial state and last start tag (the tests' own) */
		{
			hubbub_content_model m = HUBBUB_CONTENT_MODEL_PCDATA;
			if (!strcmp(state, "RCDATA state")) m = HUBBUB_CONTENT_MODEL_RCDATA;
			else if (!strcmp(state, "RAWTEXT state")) m = HUBBUB_CONTENT_MODEL_CDATA;
			else if (!strcmp(state, "Script data state")) m = HUBBUB_CONTENT_MODEL_SCRIPTDATA;
			else if (!strcmp(state, "PLAINTEXT state")) m = HUBBUB_CONTENT_MODEL_PLAINTEXT;
			else if (!strcmp(state, "CDATA section state"))
				m = HUBBUB_CONTENT_MODEL_CDATA_SECTION;
			o.content_model.model = m;
			hubbub_parser_setopt(p, HUBBUB_PARSER_CONTENT_MODEL, &o);
			o.last_start_tag = last[0] ? last : NULL;
			hubbub_parser_setopt(p, HUBBUB_PARSER_LAST_START_TAG, &o);
		}

		first = 1;
		putchar('[');
		hubbub_parser_parse_chunk(p, data, len);
		hubbub_parser_completed(p);
		printf("]\n");
		fflush(stdout);
		hubbub_parser_destroy(p);
		free(data);
	}
	return 0;
}
