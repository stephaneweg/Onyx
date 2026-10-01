//
// bin/zip.cpp -- make or change ZIP archives (the Archiver's engine), as Info-ZIP's zip:
//
//   zip [-r] [-0 .. -9] [-j] [-k] [-q] [-p FOLDER] ARCHIVE PATH ...     add (or replace) files
//   zip -d ARCHIVE NAME ...                                             delete entries
//
//   -r  folders with what they hold (without it: only the folder's own entry)
//   -0  store, -1 fastest .. -9 best (default -6); already packed files (png, zip...) are stored
//   -j  no folders: the files alone, at the archive's top (or in FOLDER)
//   -k  keep a name the archive has already (default: replace it)
//   -p  put them in that folder of the archive ("-p docs/images")
//   -q  quiet      NAME (with -d): a name, a pattern (* ?) or a folder (everything under it)
//
// A relative path is stored as it is given ("zip -r pkg.zip myapp/res" stores myapp/res/...); a path
// with its volume from its last part ("zip -r pkg.zip SD:/apps/foo.app" stores foo.app/...). The archive is written as a new copy next
// to it, then swapped in. Exit code: 0 all right, 1 nothing done, 2 an error, 3 a bad command line.
//
#include "arccli.h"

static void usage ()
{
	printf ("zip -- make or change ZIP archives\n"
		"usage: zip [-r] [-0..-9] [-j] [-k] [-q] [-p FOLDER] ARCHIVE PATH...\n"
		"       zip -d ARCHIVE NAME...\n"
		"  -r with the folders' contents  -0 store .. -9 best  -j no folders  -k keep existing names\n"
		"  -p FOLDER into that folder of the archive  -d delete  -q quiet\n");
}
struct Quiet : Progress { bool step (u64) override { return true; } };

int main (void)
{
	static char buf[2048]; char *av[256];
	int ac = cli_args (buf, sizeof buf, av, 256);
	bool rec = false, junk = false, keep = false, quiet = false, del = false;
	int level = 6; const char *prefix = "", *path = 0;
	char *items[200]; int ni = 0;
	for (int i = 0; i < ac; i++)
	{
		char *a = av[i];
		if (a[0] == '-' && a[1] && !path)
		{
			for (int k = 1; a[k]; k++)
				switch (a[k])
				{
				case 'r': rec = true; break;
				case 'j': junk = true; break;
				case 'k': keep = true; break;
				case 'q': quiet = true; break;
				case 'd': del = true; break;
				case 'p': if (i + 1 < ac) prefix = av[++i]; k = (int) strlen (a) - 1; break;
				case 'h': usage (); return 0;
				default:
					if (a[k] >= '0' && a[k] <= '9') { level = a[k] - '0'; break; }
					printf ("zip: unknown option -%c\n", a[k]); usage (); return 3;
				}
			continue;
		}
		if (!path) path = a;
		else if (ni < 200) items[ni++] = a;
	}
	if (!path || !ni) { usage (); return 3; }
	char p[300]; scopy (p, path, sizeof p);
	char x[12]; ext_of (p, x, sizeof x);
	if (!x[0] && !path_exists (p)) scat (p, ".zip", sizeof p);
	char why[200];
	Archive *ar = path_exists (p) ? archive_open (p, why, sizeof why) : (del ? 0 : archive_new (p));
	if (!ar) { printf ("zip: %s: %s\n", p, path_exists (p) ? why : "no such archive"); return 2; }
	if (!ar->writable ()) { printf ("zip: %s: this format is read only\n", p); return 2; }
	Quiet q;
	if (del)
	{
		char *sel = (char *) calloc ((size_t) (ar->n ? ar->n : 1), 1); int n = 0;
		for (int i = 0; i < ar->n; i++)
			if (chosen (ar->e[i].name, items, ni)) { sel[i] = 1; n++; if (!quiet) printf ("deleting: %s%s\n", ar->e[i].name, ar->e[i].dir ? "/" : ""); }
		if (!n) { printf ("zip: nothing matches in %s\n", p); return 1; }
		bool ok = op_delete (*ar, sel, &q);
		free (sel);
		if (!ok) { printf ("zip: %s\n", ar->error); return 2; }
		if (!quiet) printf ("%d entr%s deleted, %d left.\n", n, n == 1 ? "y" : "ies", ar->n);
		delete ar;
		return 0;
	}
	char pre[300]; archive_name (prefix, pre, sizeof pre);
	AddSet s; plan_begin (*ar, s);
	for (int i = 0; i < ni; i++)
	{
		if (!path_exists (items[i])) { printf ("zip warning: name not matched: %s\n", items[i]); continue; }
		char an[300], into[600];
		archive_name (items[i], an, sizeof an);
		const char *colon = strchr (items[i], ':');
		bool absolute = colon && colon - items[i] < 6;	// "SD:/apps/foo.app": from its last part (foo.app/...)
		if (junk || absolute) scopy (into, pre, sizeof into);
		else
		{
			char d[300]; dir_of (an, d, sizeof d);
			scopy (into, pre, sizeof into);
			if (into[0] && d[0]) scat (into, "/", sizeof into);
			scat (into, d, sizeof into);
		}
		if (!an[0] && path_is_dir (items[i]))
		{	// "." / "SD:/": its contents (the folder itself has no name)
			void *d = kapi_opendir (items[i]); struct kapi_dirent de;
			char names[128][128]; int nn = 0;
			while (d && nn < 128 && kapi_readdir (d, &de)) if (strcmp (de.name, ".") && strcmp (de.name, "..")) scopy (names[nn++], de.name, 128);
			if (d) kapi_closedir (d);
			for (int k = 0; k < nn; k++) { char sub[300]; join (sub, sizeof sub, items[i], names[k]); plan_add_path (*ar, s, sub, into, !junk, !keep, rec); }
			continue;
		}
		plan_add_path (*ar, s, items[i], into, !junk, !keep, rec);
	}
	plan_finish (*ar, s);
	s.plan.level = level;
	if (!s.files && !s.folders) { printf ("zip: nothing to do%s\n", s.skipped ? " (the names exist: -k)" : ""); return 1; }
	if (!quiet)
		for (int i = 0; i < s.plan.nadd; i++)
		{
			const NewItem &it = s.plan.add[i];
			bool replaced = ar->find (it.name) >= 0;
			printf ("%s %s%s\n", replaced ? "updating:" : "  adding:", it.name, it.disk ? "" : "/");
		}
	bool ok = apply_plan (*ar, s.plan, &q);
	if (!ok) { printf ("zip: %s\n", ar->error); return 2; }
	if (!quiet)
	{
		char a[24], b[24], c[8]; human_size (ar->totalSize (), a, sizeof a); human_size (ar->totalPacked (), b, sizeof b);
		pct (ar->totalSize (), ar->totalPacked (), c, sizeof c);
		printf ("%s: %d entries, %s -> %s (%s saved).\n", p, ar->n, a, b, c);
	}
	delete ar;
	return 0;
}
