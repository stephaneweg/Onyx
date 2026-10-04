//
// ipp_test.cpp -- the IPP client (user/print/ipp.h) on the PC: addresses, a request's attributes written and
// read back, an HTTP answer in chunks taken apart; and, given a printer's address, the real thing -- what
// it can do, and whether it would take our PWG Raster job (Validate-Job: nothing is printed).
//
//   ipp_test [printer address]
//
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include "print/ipp.h"

static int g_fail;
#define CHECK(c, what) do { if (!(c)) { printf ("FAIL: %s\n", what); g_fail++; } } while (0)

// ---- BSD sockets ----
static int h_connect (const char *host, int port)
{
	char p[12]; snprintf (p, sizeof p, "%d", port);
	struct addrinfo hint, *ai = 0; memset (&hint, 0, sizeof hint); hint.ai_socktype = SOCK_STREAM; hint.ai_family = AF_INET;
	if (getaddrinfo (host, p, &hint, &ai) || !ai) return -1;
	int s = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (s >= 0 && connect (s, ai->ai_addr, ai->ai_addrlen)) { close (s); s = -1; }
	freeaddrinfo (ai);
	return s;
}
static bool h_send (int s, const void *b, unsigned n) { while (n) { ssize_t k = send (s, b, n, 0); if (k <= 0) return false; b = (const char *) b + k; n -= (unsigned) k; } return true; }
static int h_recv (int s, void *b, unsigned n, unsigned wait)
{
	struct pollfd p = { s, POLLIN, 0 };
	if (poll (&p, 1, (int) wait) <= 0) return 0;
	ssize_t k = recv (s, b, n, 0);
	return k > 0 ? (int) k : -1;
}
static void h_close (int s) { close (s); }
static const ipp::Net HOST = { h_connect, h_send, h_recv, h_close };

// ---- a printer made of a canned answer: what was sent is kept ----
static ipp::Buf g_sent; static const char *g_answer; static unsigned g_answerLen, g_at;
static int f_connect (const char *, int) { g_sent.n = 0; g_at = 0; return 1; }
static bool f_send (int, const void *b, unsigned n) { g_sent.put (b, n); return true; }
static int f_recv (int, void *b, unsigned n, unsigned)
{
	if (g_at >= g_answerLen) return -1;
	unsigned k = g_answerLen - g_at; if (k > 7) k = 7; if (k > n) k = n;		// (in small pieces)
	memcpy (b, g_answer + g_at, k); g_at += k;
	return (int) k;
}
static void f_close (int) {}
static const ipp::Net FAKE = { f_connect, f_send, f_recv, f_close };

int main (int argc, char **argv)
{
	ipp::Uri u;
	CHECK (ipp::parse_uri ("192.168.0.14", u) && u.port == 631 && !strcmp (u.path, "/ipp/print") && !strcmp (u.ipp, "ipp://192.168.0.14:631/ipp/print"), "a bare address");
	CHECK (ipp::parse_uri ("ipp://printer.local:8631/printers/x", u) && u.port == 8631 && !strcmp (u.host, "printer.local") && !strcmp (u.path, "/printers/x"), "a full address");
	CHECK (!ipp::parse_uri ("", u), "an empty address refused");

	// a request read back
	ipp::Msg m; m.begin (ipp::OP_PRINT_JOB, 7);
	m.str (ipp::T_URI, "printer-uri", "ipp://h:631/ipp/print");
	m.group (ipp::T_JOB); m.num (ipp::T_INTEGER, "copies", 3); m.str (ipp::T_KEYWORD, "media", "iso_a4_210x297mm"); m.end ();
	ipp::Parser p; ipp::Attr a; int copies = 0, n = 0; char media[64] = "";
	CHECK (p.begin (m.b.b, m.b.n) && p.status == ipp::OP_PRINT_JOB, "the request's head");
	while (p.next (a)) { n++; if (!strcmp (a.name, "copies") && a.group == ipp::T_JOB) copies = ipp::Parser::integer (a); if (!strcmp (a.name, "media")) ipp::Parser::text (a, media, sizeof media); }
	CHECK (n == 5 && copies == 3 && !strcmp (media, "iso_a4_210x297mm"), "the request's attributes");

	// a job sent to the canned printer: the request in chunks, the answer (after a 100 Continue, in chunks) read
	static unsigned char ans[512]; unsigned al = 0;
	{
		ipp::Msg r; r.b.put ((unsigned char) 2); r.b.put ((unsigned char) 0); r.b.put ((unsigned char) 0); r.b.put ((unsigned char) 0); r.b.be32 (2);
		r.group (ipp::T_OPERATION); r.str (ipp::T_CHARSET, "attributes-charset", "utf-8");
		r.group (ipp::T_JOB); r.num (ipp::T_INTEGER, "job-id", 42); r.num (ipp::T_ENUM, "job-state", 5); r.end ();
		char head[256]; int hl = snprintf (head, sizeof head, "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nTransfer-Encoding: chunked\r\n\r\n%X\r\n", 20);
		memcpy (ans, head, hl); al = hl; memcpy (ans + al, r.b.b, 20); al += 20;
		hl = snprintf (head, sizeof head, "\r\n%X\r\n", r.b.n - 20); memcpy (ans + al, head, hl); al += hl;
		memcpy (ans + al, r.b.b + 20, r.b.n - 20); al += r.b.n - 20; memcpy (ans + al, "\r\n0\r\n\r\n", 7); al += 7;
	}
	g_answer = (const char *) ans; g_answerLen = al;
	ipp::Send s; ipp::JobSetup js = { "Test", "me", "image/pwg-raster", "iso_a4_210x297mm", 2, 4, true };
	CHECK (s.begin (&FAKE, "10.0.0.1", js), "a job begun");
	CHECK (s.write ("RaS2", 4) && s.write ("0123456789ABCDEFGHIJ", 20), "the document's pieces");
	CHECK (s.end () == 42, "the printer's job number read from a chunked answer");
	g_sent.put ((unsigned char) 0);
	const char *sent = (const char *) g_sent.b;
	CHECK (!strncmp (sent, "POST /ipp/print HTTP/1.1\r\nHost: 10.0.0.1:631\r\n", 46) && strstr (sent, "Transfer-Encoding: chunked\r\n"), "the HTTP request");
	CHECK (g_sent.n > 40 && !memcmp (g_sent.b + g_sent.n - 1 - 5, "0\r\n\r\n", 5) && memmem (g_sent.b, g_sent.n, "\r\n14\r\n0123456789ABCDEFGHIJ\r\n", 28), "the chunks");

	if (argc > 1)
	{
		ipp::Caps c; char err[96] = "";
		if (!ipp::get_caps (&HOST, argv[1], c, err, sizeof err)) { printf ("FAIL: %s: %s\n", argv[1], err); g_fail++; }
		else
		{
			printf ("%s (%s)\n  formats: %s\n  PWG Raster %s, PDF %s, %d dpi, %s, quality bits %#x, copies up to %d, %s\n  paper: %s (default %s)\n  margins (1/100 mm): %d %d %d %d\n",
				c.model, c.name, c.formats, c.pwg ? "yes" : "no", c.pdf ? "yes" : "no", c.dpi, c.color ? "colour" : "black only", c.quality, c.copies,
				c.duplex ? "two-sided" : "one-sided", c.media, c.media_default, c.margin[0], c.margin[1], c.margin[2], c.margin[3]);
			for (int i = 0; i < c.ninks; i++) printf ("  %s: %d %%\n", c.ink_name[i], c.ink[i]);
			ipp::Send v; ipp::JobSetup vs = { "Onyx validation", "onyx", "image/pwg-raster", c.media_default, 1, 4, false };
			bool ok = v.begin (&HOST, argv[1], vs, true) && v.end () >= 0;
			printf ("  Validate-Job (PWG Raster, %s, colour): %s %s\n", c.media_default, ok ? "accepted" : "REFUSED", v.err);
			CHECK (ok, "the printer would take our job");
			ipp::JobSetup bad = { "Onyx validation", "onyx", "application/pdf", c.media_default, 1, 4, false };
			ipp::Send w; bool okPdf = w.begin (&HOST, argv[1], bad, true) && w.end () >= 0;
			printf ("  Validate-Job (PDF): %s %s\n", okPdf ? "accepted" : "refused:", w.err);
			CHECK (okPdf == c.pdf, "a format it does not list is refused, with a reason");
		}
	}
	if (g_fail) { printf ("ipp: %d FAILED\n", g_fail); return 1; }
	printf ("ipp: all good\n");
	return 0;
}
