//
// tools/tests/archiver/arctool.cpp -- the Archiver's engine (user/Apps/archiver/ops.h, zip.h) driven
// from a command line on the PC, over the desktop simulator's kapi (fakekapi.cpp: RAM: is the
// folder SIM_RAM). test.py runs it on archives made by Python's zipfile and the zip tool, and checks
// what it extracts and writes. Commands (paths are Onyx paths: RAM:/...):
//   list    ARC                                       one line an entry: D|F size packed crc name
//   extract ARC DEST LAYOUT CURRENT OW [NAME...]      LAYOUT 0 full / 1 from current / 2 flat;
//                                                     OW 1 replace / 2 skip / 3 keep both
//   pw      ARC PASSWORD DEST                         extract all with a password
//   add     ARC INTO KEEPFOLDERS LEVEL REPLACE PATH...
//   delete  ARC NAME...       rename ARC FROM TO       mkdir ARC NAME
//
#include <stdio.h>
#include "../../../user/Kits/filekit/ops.h"

using namespace arc;

struct Bar : Progress { u64 done = 0; bool step (u64 b) override { done += b; return true; } };

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "usage: arctool CMD ARC ...\n"); return 2; }
	const char *cmd = argv[1], *path = argv[2];
	char why[200];
	Archive *a = 0;
	if (!strcmp (cmd, "add") && !path_exists (path)) a = archive_new (path);
	else a = archive_open (path, why, sizeof why);
	if (!a) { printf ("ERROR %s\n", why); return 1; }
	Bar bar;
	bool ok = true;
	if (!strcmp (cmd, "list"))
	{
		printf ("FORMAT %s %d\n", a->format (), a->n);
		if (a->comment) printf ("COMMENT %s\n", a->comment);
		for (int i = 0; i < a->n; i++)
			printf ("%c %llu %llu %08x %s%s\n", a->e[i].dir ? 'D' : 'F', a->e[i].size, a->e[i].packed, a->e[i].crc,
				a->e[i].name, a->e[i].encrypted ? " *" : "");
	}
	else if (!strcmp (cmd, "extract") && argc >= 7)
	{
		Extract x; x.a = a;
		scopy (x.dest, argv[3], sizeof x.dest); x.layout = atoi (argv[4]);
		scopy (x.current, strcmp (argv[5], "-") ? argv[5] : "", sizeof x.current); x.overwrite = atoi (argv[6]);
		char *sel = 0;
		if (argc > 7) { sel = select_names (*a, argv + 7, argc - 7); x.sel = sel; }
		ok = run_extract (x, &bar);
		printf ("%s files=%d skipped=%d bytes=%llu/%llu %s\n", ok ? "OK" : "ERROR", x.files, x.skipped, bar.done,
			extract_total (x), x.error);
		free (sel);
	}
	else if (!strcmp (cmd, "pw") && argc >= 5)
	{
		ZipArchive *z = (ZipArchive *) a; scopy (z->password, argv[3], sizeof z->password);
		Extract x; x.a = a; scopy (x.dest, argv[4], sizeof x.dest); x.overwrite = OW_REPLACE;
		ok = run_extract (x, &bar);
		printf ("%s files=%d %s\n", ok ? "OK" : "ERROR", x.files, x.error);
	}
	else if (!strcmp (cmd, "add") && argc >= 8)
	{
		int added = 0;
		ok = op_add (*a, argv + 7, argc - 7, strcmp (argv[3], "-") ? argv[3] : "", atoi (argv[4]) != 0,
			     atoi (argv[6]) != 0, atoi (argv[5]), &bar, &added);
		printf ("%s added=%d entries=%d %s\n", ok ? "OK" : "ERROR", added, a->n, ok ? "" : a->error);
	}
	else if (!strcmp (cmd, "delete") && argc >= 4)
	{
		char *sel = select_names (*a, argv + 3, argc - 3);
		ok = op_delete (*a, sel, &bar); free (sel);
		printf ("%s entries=%d %s\n", ok ? "OK" : "ERROR", a->n, ok ? "" : a->error);
	}
	else if (!strcmp (cmd, "rename") && argc >= 5)
	{
		ok = op_rename (*a, argv[3], argv[4], &bar);
		printf ("%s entries=%d %s\n", ok ? "OK" : "ERROR", a->n, ok ? "" : a->error);
	}
	else if (!strcmp (cmd, "mkdir") && argc >= 4)
	{
		ok = op_new_folder (*a, argv[3], &bar);
		printf ("%s entries=%d %s\n", ok ? "OK" : "ERROR", a->n, ok ? "" : a->error);
	}
	else { fprintf (stderr, "bad command\n"); return 2; }
	delete a;
	return ok ? 0 : 1;
}
