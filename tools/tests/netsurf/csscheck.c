/*
 * tools/tests/netsurf/csscheck.c -- what libcss keeps, line by line (the PC bench): each line
 * of the file is a declaration or a sheet and whether libcss must keep it --
 *
 *   + prop: value        kept (an inline style: the declaration's bytecode is not empty)
 *   - prop: value        dropped
 *   S+ sheet text        a sheet with at least one rule kept (a selector, an at-rule)
 *   S- sheet text        no rule kept
 *   # comment
 *
 * as CSS.supports() / element.style / the CSSOM ask it (NetSurf's nscss_text_kept). Prints
 * the lines that fail; exit status 1 if any does. With -t: the time to parse the file N times.
 *
 *   make -f tools/tests/netsurf/host.mk css-check
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libcss/libcss.h>

static css_error resolve(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
	(void) pw;
	(void) base;
	*abs = lwc_string_ref(rel);
	return CSS_OK;
}

static int kept(const char *text, int inline_style, uint32_t *rules, uint32_t *words)
{
	css_stylesheet_params params;
	css_stylesheet *sheet;
	css_error error;
	int ok = 0;

	memset(&params, 0, sizeof params);
	params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	params.level = CSS_LEVEL_DEFAULT;
	params.charset = "UTF-8";
	params.url = "about:blank";
	params.inline_style = inline_style;
	params.resolve = resolve;
	if (css_stylesheet_create(&params, &sheet) != CSS_OK)
		return 0;
	error = css_stylesheet_append_data(sheet, (const uint8_t *) text, strlen(text));
	if ((error == CSS_OK || error == CSS_NEEDDATA) && css_stylesheet_data_done(sheet) == CSS_OK &&
			css_stylesheet_onyx_kept(sheet, rules, words) == CSS_OK)
		ok = 1;
	css_stylesheet_destroy(sheet);
	return ok;
}

int main(int argc, char **argv)
{
	char line[8192];
	int fails = 0, n = 0, reps = 1, r;
	FILE *f;
	clock_t t0;

	if (argc > 3 && strcmp(argv[1], "-b") == 0) {
		/* -b N sheet.css...: the time to parse each sheet N times (a parse speed bench) */
		int k;
		reps = atoi(argv[2]);
		for (k = 3; k < argc; k++) {
			char *buf;
			long size;
			uint32_t rules = 0, words = 0;
			f = fopen(argv[k], "rb");
			if (f == NULL)
				continue;
			fseek(f, 0, SEEK_END);
			size = ftell(f);
			rewind(f);
			buf = malloc(size + 1);
			if (buf == NULL || fread(buf, 1, size, f) != (size_t) size) {
				fclose(f);
				free(buf);
				continue;
			}
			buf[size] = '\0';
			fclose(f);
			t0 = clock();
			for (r = 0; r < reps; r++)
				kept(buf, 0, &rules, &words);
			printf("%s: %ld bytes, %u rules, %u words: %.2f ms per parse\n", argv[k],
					size, rules, words,
					1000.0 * (double) (clock() - t0) / CLOCKS_PER_SEC / reps);
			free(buf);
		}
		return 0;
	}
	if (argc > 2 && strcmp(argv[1], "-t") == 0) {
		reps = atoi(argv[2]);
		argv += 2;
		argc -= 2;
	}
	if (argc < 2 || (f = fopen(argv[1], "r")) == NULL) {
		fprintf(stderr, "usage: csscheck [-t N] <file>\n");
		return 2;
	}
	t0 = clock();
	for (r = 0; r < reps; r++) {
		rewind(f);
		while (fgets(line, sizeof line, f) != NULL) {
			size_t len = strlen(line);
			int sheet = 0, want, got;
			const char *text;
			uint32_t rules = 0, words = 0;

			while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
				line[--len] = '\0';
			if (len == 0 || line[0] == '#')
				continue;
			text = line;
			if (text[0] == 'S') {
				sheet = 1;
				text++;
			}
			if (text[0] != '+' && text[0] != '-')
				continue;
			want = text[0] == '+';
			text++;
			while (*text == ' ')
				text++;
			if (!kept(text, !sheet, &rules, &words))
				got = 0;
			else
				got = sheet ? rules > 0 : words > 0;
			if (r == 0) {
				n++;
				if (got != want) {
					printf("FAIL %s%c %s\n", sheet ? "S" : "", want ? '+' : '-', text);
					fails++;
				}
			}
		}
	}
	if (reps > 1)
		printf("%d x %d lines: %.1f ms\n", reps, n,
				1000.0 * (double) (clock() - t0) / CLOCKS_PER_SEC);
	printf("csscheck: %d of %d lines as expected\n", n - fails, n);
	return fails ? 1 : 0;
}
