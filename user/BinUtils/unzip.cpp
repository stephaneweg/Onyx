//
// bin/unzip.cpp -- extract, list, test ZIP archives (the Archiver's engine), as Info-ZIP's unzip:
//
//   unzip [-l | -v | -t | -p] [-o | -n] [-j] [-q] [-d DIR] [-P PASSWORD] ARCHIVE [NAME ...] [-x NAME ...]
//
//   -l  list   -v  list with the method, the packed size, the CRC   -t  test (decompress, check)
//   -p  write the files to the output (a pipe)   -o  replace existing files   -n  never replace them
//   -j  no folders (every file into DIR)   -d  the folder to extract into (made; default: here)
//   -P  the password (ZipCrypto)   -q  quiet   NAME: a name, a pattern (* ?) or a folder; -x: left out
//
// Without -o / -n a file that exists is asked about: [y]es, [n]o, [A]ll, [N]one, [r]ename.
// Exit code: 0 all right, 1 a warning (files skipped), 2 an error, 3 a bad command line.
//
#include "arccli.h"

static bool g_quiet;
struct Say : Progress
{
	const char *verb;
	bool step (u64) override { return true; }
	void file (const char *n) override { if (!g_quiet) printf ("%11s: %s\n", verb, n); }
};
static int ask (void *, const char *path)
{
	static int all;
	if (all) return all;
	for (;;)
	{
		printf ("replace %s? [y]es, [n]o, [A]ll, [N]one, [r]ename: ", path); fflush (stdout);
		char line[32];
		if (!fgets (line, sizeof line, stdin)) return ANS_SKIP;
		switch (line[0])
		{
		case 'y': return ANS_REPLACE; case 'n': return ANS_SKIP; case 'r': return ANS_KEEP_BOTH;
		case 'A': all = ANS_REPLACE; return ANS_REPLACE_ALL; case 'N': all = ANS_SKIP; return ANS_SKIP_ALL;
		}
	}
}
struct OutSink : Sink { bool write (const void *b, u32 n) override { return fwrite (b, 1, n, stdout) == n; } };

static void usage ()
{
	printf ("unzip -- extract, list, test ZIP archives\n"
		"usage: unzip [-l|-v|-t|-p] [-o|-n] [-j] [-q] [-d DIR] [-P PASSWORD] ARCHIVE [NAME...] [-x NAME...]\n"
		"  -l list  -v list in detail  -t test  -p to the output\n"
		"  -o replace existing files  -n never replace  -j no folders  -d DIR where  -q quiet\n");
}

int main (void)
{
	static char buf[2048]; char *av[128];
	int ac = cli_args (buf, sizeof buf, av, 128);
	char mode = 'x'; int ow = OW_ASK; bool junk = false; const char *dest = 0, *pw = 0, *path = 0;
	char *pat[64], *excl[64]; int np = 0, nx = 0; bool inExcl = false;
	for (int i = 0; i < ac; i++)
	{
		char *a = av[i];
		if (a[0] == '-' && a[1] && !path)
		{
			for (int k = 1; a[k]; k++)
				switch (a[k])
				{
				case 'l': case 'v': case 't': case 'p': mode = a[k]; break;
				case 'o': ow = OW_REPLACE; break;
				case 'n': ow = OW_SKIP; break;
				case 'j': junk = true; break;
				case 'q': g_quiet = true; break;
				case 'd': if (i + 1 < ac) dest = av[++i]; k = (int) strlen (a) - 1; break;
				case 'P': if (i + 1 < ac) pw = av[++i]; k = (int) strlen (a) - 1; break;
				case 'h': usage (); return 0;
				default: printf ("unzip: unknown option -%c\n", a[k]); usage (); return 3;
				}
			continue;
		}
		if (!strcmp (a, "-x")) { inExcl = true; continue; }
		if (!strcmp (a, "-d") && i + 1 < ac) { dest = av[++i]; continue; }
		if (!path) { path = a; continue; }
		if (inExcl) { if (nx < 64) excl[nx++] = a; }
		else if (np < 64) pat[np++] = a;
	}
	if (!path) { usage (); return 3; }
	char why[200], p[300]; scopy (p, path, sizeof p);
	Archive *ar = archive_open (p, why, sizeof why);
	if (!ar && !strchr (p, '.')) { scat (p, ".zip", sizeof p); ar = archive_open (p, why, sizeof why); }
	if (!ar) { printf ("unzip: %s: %s\n", path, why); return 2; }
	if (pw && !strcmp (ar->format (), "ZIP")) scopy (((ZipArchive *) ar)->password, pw, 128);
	char *sel = (char *) calloc ((size_t) (ar->n ? ar->n : 1), 1);
	int nsel = 0;
	for (int i = 0; i < ar->n; i++)
		if (chosen (ar->e[i].name, pat, np) && !(nx && chosen (ar->e[i].name, excl, nx))) { sel[i] = 1; nsel++; }
	if (np && !nsel) { printf ("unzip: nothing matches in %s\n", p); return 1; }
	int rc = 0;
	if (mode == 'l' || mode == 'v')
	{
		u64 ts = 0, tp = 0; int nf = 0;
		printf ("Archive:  %s\n", p);
		if (mode == 'l') printf ("  Length      Date    Time    Name\n---------  ---------- -----   ----\n");
		else printf (" Length   Method    Size  Cmpr    Date    Time   CRC-32   Name\n--------  ------  ------- ---- ---------- -----  --------  ----\n");
		for (int i = 0; i < ar->n; i++)
		{
			if (!sel[i]) continue;
			const Entry &e = ar->e[i];
			u32 t = e.dostime;
			char d[24]; snprintf (d, sizeof d, "%04u-%02u-%02u %02u:%02u", (t >> 25) + 1980, (t >> 21) & 15, (t >> 16) & 31, (t >> 11) & 31, (t >> 5) & 63);
			if (mode == 'l') printf ("%9llu  %s   %s%s\n", e.size, d, e.name, e.dir ? "/" : "");
			else
			{
				char c[8]; pct (e.size, e.packed, c, sizeof c);
				printf ("%8llu  %-6s %8llu %4s %s  %08x  %s%s\n", e.size, e.dir ? "Store" : ar->methodName (e), e.packed, c, d, e.crc, e.name, e.dir ? "/" : "");
			}
			ts += e.size; tp += e.packed; if (!e.dir) nf++;
		}
		if (mode == 'l') printf ("---------                     -------\n%9llu                     %d file%s\n", ts, nf, nf == 1 ? "" : "s");
		else { char c[8]; pct (ts, tp, c, sizeof c); printf ("--------          -------  ---                            -------\n%8llu         %8llu %4s                            %d file%s\n", ts, tp, c, nf, nf == 1 ? "" : "s"); }
	}
	else if (mode == 't')
	{
		int bad = 0, n = 0;
		for (int i = 0; i < ar->n; i++)
		{
			if (!sel[i] || ar->e[i].dir) continue;
			NullSink ns; n++;
			bool ok = ar->extract (i, ns, 0);
			if (!g_quiet || !ok) printf ("    testing: %-40s %s\n", ar->e[i].name, ok ? "OK" : ar->error);
			if (!ok) bad++;
		}
		if (bad) { printf ("At least one error was detected in %s (%d of %d files).\n", p, bad, n); rc = 2; }
		else printf ("No errors detected in compressed data of %s.\n", p);
	}
	else if (mode == 'p')
	{
		OutSink o;
		for (int i = 0; i < ar->n; i++)
			if (sel[i] && !ar->e[i].dir && !ar->extract (i, o, 0)) { fprintf (stderr, "unzip: %s\n", ar->error); rc = 2; break; }
	}
	else
	{
		if (!g_quiet) printf ("Archive:  %s\n", p);
		Extract x; x.a = ar; x.sel = sel;
		scopy (x.dest, dest ? dest : ".", sizeof x.dest);
		if (!dest) { char cwd[260]; cwd[0] = 0; kapi_getcwd (cwd, sizeof cwd); cwd[259] = 0; if (cwd[0]) scopy (x.dest, cwd, sizeof x.dest); }
		x.layout = junk ? LAY_FLAT : LAY_FULL; x.overwrite = ow; x.ask = ask;
		Say say; say.verb = "inflating";
		if (!run_extract (x, &say)) { printf ("unzip: %s\n", x.error); rc = 2; }
		else if (x.skipped) rc = 1;
		if (!g_quiet && rc != 2) printf ("%d file%s extracted%s.\n", x.files, x.files == 1 ? "" : "s", x.skipped ? ", some skipped" : "");
	}
	free (sel); delete ar;
	return rc;
}
