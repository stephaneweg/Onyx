/*
 * tcpbench.c -- the network's speed without a disk and without the internet: a TCP server on the
 * Pi that a program of the PC talks to (tools/tests/net/tcpbench.py).
 *
 *   tcpbench [port]        (default 5001; runs until killed)
 *
 * A client connects and sends one line:
 *   "S <megabytes>\n"   the Pi sends that much (64 KB a write), then closes
 *   "R\n"               the Pi reads until the client closes its side, then answers
 *                       "<bytes> <milliseconds>\n"
 *   "E\n"               an echo: every block read is written back (latency: a round trip a block)
 * Each connection's line in the output: the bytes, the time, the rate, the reads / writes and the
 * largest and smallest count one of them returned.
 *
 * A POSIX program (libonyxposix's sockets): built by user/BinUtils/Makefile's POSIX rule.
 *
 * MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static char buf[65536];

/* tcpbench udp [port]: datagrams counted, a line a second (the PC floods at a rate of its own: how
 * many frames a second reach a program, without TCP's pacing); the first 4 bytes of each are its
 * number, so the ones lost show */
static int udp_main(int port)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in a;
	unsigned got = 0, bytes = 0, first = 0, last = 0;
	double t0 = 0;

	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short) port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (s < 0 || bind(s, (struct sockaddr *) &a, sizeof a) < 0) { perror("udp socket / bind"); return 1; }
	printf("tcpbench: udp: listening on port %d\n", port);
	fflush(stdout);
	for (;;) {
		int r = (int) recv(s, buf, sizeof buf, 0);
		double now = now_ms();
		unsigned seq;
		if (r < 4) continue;
		memcpy(&seq, buf, 4);
		if (got == 0) { t0 = now; first = seq; }
		got++; bytes += (unsigned) r; last = seq;
		if (now - t0 >= 1000) {
			printf("tcpbench: udp: %u datagrams in %.0f ms (%u KB/s), numbers %u..%u: %u lost\n", got, now - t0, (unsigned) (bytes / 1.024 / (now - t0)),
				first, last, last - first + 1 - got);
			fflush(stdout);
			got = bytes = 0;
		}
	}
}

int main(int argc, char **argv)
{
	int port = argc > 1 ? atoi(argv[1]) : 5001;
	if (argc > 1 && !strcmp(argv[1], "udp")) return udp_main(argc > 2 ? atoi(argv[2]) : 5002);
	int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
	struct sockaddr_in a;

	if (ls < 0) { perror("socket"); return 1; }
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short) port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(ls, (struct sockaddr *) &a, sizeof a) < 0 || listen(ls, 4) < 0) { perror("bind / listen"); return 1; }
	printf("tcpbench: listening on port %d\n", port);
	fflush(stdout);
	for (;;) {
		int s = accept(ls, NULL, NULL);
		char line[64];
		int n = 0, calls = 0, most = 0, least = 1 << 30;
		long long total = 0;
		double t0;

		if (s < 0) { perror("accept"); sleep(1); continue; }
		while (n < (int) sizeof line - 1) {
			int r = (int) recv(s, line + n, 1, 0);
			if (r <= 0 || line[n] == '\n') break;
			n++;
		}
		line[n] = 0;
		t0 = now_ms();
		if (line[0] == 'S') {
			long long want = atoll(line + 1) * 1024 * 1024;
			memset(buf, 'x', sizeof buf);
			while (total < want) {
				size_t chunk = want - total > (long long) sizeof buf ? sizeof buf : (size_t) (want - total);
				int w = (int) send(s, buf, chunk, 0);
				if (w <= 0) { printf("tcpbench: send: %s\n", strerror(errno)); break; }
				total += w; calls++;
				if (w > most) most = w;
				if (w < least) least = w;
			}
		} else if (line[0] == 'R' || line[0] == 'E') {
			for (;;) {
				int r = (int) recv(s, buf, sizeof buf, 0);
				if (r <= 0) break;
				total += r; calls++;
				if (r > most) most = r;
				if (r < least) least = r;
				if (line[0] == 'E' && send(s, buf, (size_t) r, 0) != r) break;
			}
			if (line[0] == 'R') {
				char ans[64];
				int l = snprintf(ans, sizeof ans, "%lld %.0f\n", total, now_ms() - t0);
				send(s, ans, (size_t) l, 0);
			}
		}
		{
			double ms = now_ms() - t0;
			printf("tcpbench: %s: %lld bytes in %.0f ms, %.0f KB/s, %d %s (%d..%d bytes each)\n", line, total, ms,
				ms > 0 ? total / 1.024 / ms : 0.0, calls, line[0] == 'S' ? "writes" : "reads", calls ? least : 0, most);
			fflush(stdout);
		}
		shutdown(s, SHUT_WR);
		close(s);
	}
}
