/*
 * nstest -- a console smoke test for the NetSurf "bricks" on Onyx.
 *
 * NetSurf hangs on hardware with no window; before debugging the browser itself we need to
 * know whether the cross-built library stack (libwapcaplet / libparserutils / libcss /
 * libdom+hubbub / zlib) and the platform shims (iconv / dirent / stat) actually WORK at
 * run time, not just link. This exercises each in turn, printing a line (flushed) before
 * AND after every step, so on the Pi the LAST line printed pinpoints the brick that hangs
 * or crashes. It is a /bin console tool -- run `nstest` in the terminal and read the output.
 *
 * Build + stage:  make -f user/netsurf/netsurf-app.mk nstest   (-> sdcard/bin/nstest.elf)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <iconv.h>
#include <dirent.h>
#include <sys/stat.h>

#include <libwapcaplet/libwapcaplet.h>
#include <zlib.h>
#include <libcss/libcss.h>
#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>

static void step (const char *n, const char *what)
{
	printf ("[%s] %s ... ", n, what);
	fflush (stdout);
}

static css_error resolve_url (void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
	(void) pw; (void) base;
	*abs = lwc_string_ref (rel);
	return CSS_OK;
}

int main (void)
{
	printf ("nstest: NetSurf brick smoke test\n");
	fflush (stdout);

	/* 1 -- libwapcaplet: intern a string */
	step ("1", "libwapcaplet intern");
	{
		lwc_string *s = 0;
		lwc_error e = lwc_intern_string ("Onyx", 4, &s);
		if (e != lwc_error_ok || !s) printf ("FAIL e=%d\n", e);
		else { printf ("OK '%.*s' len=%u\n", (int) lwc_string_length (s),
			       lwc_string_data (s), (unsigned) lwc_string_length (s));
		       lwc_string_unref (s); }
		fflush (stdout);
	}

	/* 2 -- zlib */
	step ("2", "zlib version");
	printf ("OK %s\n", zlibVersion ()); fflush (stdout);

	/* 3 -- libcss: parse a stylesheet (exercises the generated property parsers) */
	step ("3", "libcss parse");
	{
		css_stylesheet_params p; memset (&p, 0, sizeof p);
		p.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
		p.level = CSS_LEVEL_21; p.charset = "UTF-8";
		p.url = "http://x/"; p.title = ""; p.resolve = resolve_url;
		css_stylesheet *sh = 0;
		css_error c = css_stylesheet_create (&p, &sh);
		if (c != CSS_OK) printf ("FAIL create=%d\n", c);
		else {
			const char css[] = "h1{color:red}p{margin:1px}a{color:blue}body{font-size:12px}";
			c = css_stylesheet_append_data (sh, (const uint8_t *) css, sizeof css - 1);
			if (c != CSS_OK && c != CSS_NEEDDATA) printf ("FAIL append=%d\n", c);
			else {
				c = css_stylesheet_data_done (sh);
				size_t sz = 0; css_stylesheet_size (sh, &sz);
				printf ("OK done=%d size=%u\n", c, (unsigned) sz);
			}
			css_stylesheet_destroy (sh);
		}
		fflush (stdout);
	}

	/* 4 -- libdom + hubbub: parse HTML into a DOM, read the root element name */
	step ("4", "libdom+hubbub parse");
	{
		dom_hubbub_parser_params pp; memset (&pp, 0, sizeof pp);
		pp.enc = NULL; pp.fix_enc = true; pp.enable_script = false;
		pp.msg = NULL; pp.script = NULL; pp.ctx = NULL; pp.daf = NULL;
		dom_hubbub_parser *parser = 0; dom_document *doc = 0;
		dom_hubbub_error he = dom_hubbub_parser_create (&pp, &parser, &doc);
		if (he != DOM_HUBBUB_OK || !parser) printf ("FAIL create=%d\n", he);
		else {
			const char *html =
			    "<html><head><title>t</title></head>"
			    "<body><p>Hi <a href=\"x\">link</a></p></body></html>";
			he = dom_hubbub_parser_parse_chunk (parser, (const uint8_t *) html,
			                                    strlen (html));
			dom_hubbub_error hc = dom_hubbub_parser_completed (parser);
			printf ("chunk=%d done=%d ", he, hc); fflush (stdout);
			dom_element *root = 0;
			dom_exception ex = dom_document_get_document_element (doc, &root);
			if (ex == DOM_NO_ERR && root) {
				dom_string *nm = 0;
				dom_node_get_node_name ((dom_node *) root, &nm);
				if (nm) { printf ("OK root=<%.*s>\n",
					       (int) dom_string_length (nm), dom_string_data (nm));
					  dom_string_unref (nm); }
				else printf ("OK root(no-name)\n");
				dom_node_unref ((dom_node *) root);
			} else printf ("OK no-root ex=%d\n", ex);
			dom_hubbub_parser_destroy (parser);
			if (doc) dom_node_unref ((dom_node *) doc);
		}
		fflush (stdout);
	}

	/* 5 -- platform: iconv passthrough (UTF-8 path NetSurf's utf8.c relies on) */
	step ("5", "iconv passthrough");
	{
		iconv_t cd = iconv_open ("UTF-8", "UTF-8");
		char in[] = "hello"; char out[16];
		char *ip = in, *op = out; size_t il = 5, ol = sizeof out;
		size_t r = iconv (cd, &ip, &il, &op, &ol);
		*op = '\0'; iconv_close (cd);
		printf ("OK r=%ld out='%s'\n", (long) r, out); fflush (stdout);
	}

	/* 6 -- platform: directory + stat over the SD card */
	step ("6", "opendir/stat");
	{
		DIR *d = opendir ("SD:/");
		struct stat st; int sr = stat ("SD:etc/autostart", &st);
		printf ("OK opendir=%p stat=%d size=%ld\n", (void *) d, sr,
		        sr == 0 ? (long) st.st_size : -1L);
		if (d) closedir (d);
		fflush (stdout);
	}

	printf ("nstest: all steps reached -- the library bricks run.\n");
	fflush (stdout);
	return 0;
}
