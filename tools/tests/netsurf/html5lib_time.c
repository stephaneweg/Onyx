/*
 * tools/tests/netsurf/html5lib_time.c -- the time NetSurf's HTML parser (hubbub + the libdom
 * binding) takes over a page, fed in 32 KB chunks as the fetcher does.
 *
 *   html5lib_time <file.html> [runs]     -> the best and the mean time of a parse, the nodes
 */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static long count(dom_node *n)
{
	long c = 1;
	dom_node *k = NULL, *next;
	dom_node_get_first_child(n, &k);
	while (k != NULL) {
		c += count(k);
		next = NULL;
		dom_node_get_next_sibling(k, &next);
		dom_node_unref(k);
		k = next;
	}
	return c;
}

int main(int argc, char **argv)
{
	FILE *f;
	size_t len;
	uint8_t *data;
	int runs = argc > 2 ? atoi(argv[2]) : 20;
	double best = 1e30, sum = 0;
	long nodes = 0;

	if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
		fprintf(stderr, "usage: html5lib_time file.html [runs]\n");
		return 1;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc(len);
	len = fread(data, 1, len, f);
	fclose(f);

	for (int r = 0; r < runs; r++) {
		dom_hubbub_parser_params params;
		dom_hubbub_parser *parser = NULL;
		dom_document *doc = NULL;
		double t0 = now(), t;

		memset(&params, 0, sizeof(params));
		params.enc = NULL;
		params.fix_enc = true;
		params.enable_script = true;
		dom_hubbub_parser_create(&params, &parser, &doc);
		for (size_t o = 0; o < len; o += 32768)
			dom_hubbub_parser_parse_chunk(parser, data + o,
					len - o < 32768 ? len - o : 32768);
		dom_hubbub_parser_completed(parser);
		t = now() - t0;
		if (r == 0)
			nodes = count((dom_node *) doc);
		dom_hubbub_parser_destroy(parser);
		dom_node_unref(doc);
		if (t < best) best = t;
		sum += t;
	}
	printf("%s: %zu bytes, %ld nodes, best %.2f ms, mean %.2f ms over %d runs\n",
			argv[1], len, nodes, best, sum / runs, runs);
	return 0;
}
