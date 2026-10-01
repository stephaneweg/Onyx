//
// bin/pkg.cpp -- Onyx's packages from the shell (docs/pkg/README.md; the library: ../pkg/pkglib.h):
//
//   pkg list [-a] [filter]       the packages installed (-a: every package of the repository)
//   pkg info <name>              a package: its versions, size, needs, mode
//   pkg add <name>...            install (and what it needs); already installed: said, nothing done
//   pkg add <file.opk>           install a package file (not from the repository)
//   pkg delete [-p] <name>...    remove (-p: the settings you changed too); not installed: said
//   pkg update <name>... | -a    update those (-a: every one with an update, but those set "never")
//   pkg upgrade                  one round of the daemon: the "auto" ones updated, the others listed
//   pkg check                    the index read again: the updates available
//   pkg mode <name> manual|auto|never
//   pkg commit                   the staged packages moved in (etc/autostart, at boot); reboots
//                                when the kernel or the firmware changed
//   -r <repo>                    another repository (a URL or a folder) for this command
//
// Exit code: 0 done, 1 nothing to do / a warning, 2 an error, 3 a bad command line.
//
#include "pkg/pkglib.h"
#include "bin/arccli.h"

using namespace pkg;

struct Out : Report
{
	void say (const char *s) override { printf ("%s\n", s); fflush (stdout); }
};

static Manager *M;
static Out R;

static int need_index (void)
{
	int rc = M->refresh (R);
	if (rc != OK && !M->haveIndex) return rc;
	return OK;
}
static void pad (const char *s, int w) { int n = printf ("%s", s); while (n++ < w) putchar (' '); }

static int cmd_list (int ac, char **av)
{
	bool all = false; const char *filter = 0;
	for (int i = 0; i < ac; i++) { if (eq (av[i], "-a")) all = true; else filter = av[i]; }
	if (all) { if (need_index () != OK) return 2; } else M->load_cached_quiet ();
	int shown = 0;
	if (all)
		for (int i = 0; i < M->index.n; i++)
		{
			const Pkg &p = M->index.p[i];
			if (filter && !strstr (p.name, filter) && !strstr (p.title, filter) && !strstr (p.category, filter)) continue;
			Inst *in = M->db.find (p.name);
			pad (p.name, 16); pad (p.version, 13);
			if (!in) printf ("-          %s\n", p.title);
			else if (vcmp (p.version, in->version ()) > 0) printf ("%-10s %s (installed: %s)\n", "update", p.title, in->version ());
			else printf ("%-10s %s\n", "installed", p.title);
			shown++;
		}
	else
		for (int i = 0; i < M->db.n; i++)
		{
			Inst &in = *M->db.v[i];
			if (filter && !strstr (in.name, filter)) continue;
			pad (in.name, 16); pad (in.version (), 13); pad (in.mode (), 8);
			const Pkg *u = M->haveIndex ? M->update_for (in) : 0;
			if (M->staged (in.name)) printf (" (staged: restart to finish)");
			else if (u) printf (" (%s available)", u->version);
			printf ("\n"); shown++;
		}
	if (!all)						// staged, not installed yet (the system's first install)
	{
		void *d = kapi_opendir (PKG_STAGE); struct kapi_dirent e;
		while (d && kapi_readdir (d, &e))
		{
			int l = (int) strlen (e.name);
			if (e.is_dir || l < 5 || strcmp (e.name + l - 4, ".ini")) continue;
			e.name[l - 4] = 0;
			if (M->db.find (e.name) || (filter && !strstr (e.name, filter))) continue;
			char ip[200]; snprintf (ip, sizeof ip, PKG_STAGE "/%s.ini", e.name);
			Ini st; st.load (ip);
			pad (e.name, 16); pad (st.get ("package", "version", "?"), 13); printf ("(staged: restart to finish)\n"); shown++;
		}
		if (d) kapi_closedir (d);
	}
	printf ("%d package%s\n", shown, shown == 1 ? "" : "s");
	return OK;
}

static int cmd_info (const char *name)
{
	M->load_cached_quiet ();
	const Pkg *p = M->haveIndex ? M->index.find (name) : 0;
	Inst *in = M->db.find (name);
	if (!p && !in) { printf ("%s: no such package\n", name); return 2; }
	printf ("%s -- %s\n", name, p ? p->title : in->ini.get ("package", "title", name));
	if (p) printf ("  %s\n  category %s, by %s\n  in the repository: %s, %llu KB (%llu KB installed)\n  needs: %s\n",
		       p->summary, p->category, p->author, p->version, (unsigned long long) (p->size + 1023) / 1024,
		       (unsigned long long) (p->installed + 1023) / 1024, p->needs);
	if (in)
	{
		int nf = 0; for (int i = 0; i < in->ini.n; i++) if (eq (in->ini.kv[i].sec, "files")) nf++;
		printf ("  installed: %s, %d files, updates: %s%s\n", in->version (), nf, in->mode (), in->required () ? " (part of the system)" : "");
	}
	else printf ("  not installed\n");
	return OK;
}

static int install_list (const Pkg **list, int n)
{
	int rc = OK;
	for (int i = 0; i < n; i++)
	{
		char t[200]; Inst *in = M->db.find (list[i]->name);
		snprintf (t, sizeof t, in ? "%s %s -> %s ..." : "%s %s%s ...", list[i]->name, in ? in->version () : "", list[i]->version);
		R.say (t);
		int r = M->install (*list[i], R);
		if (r != OK) rc = r;
	}
	return rc;
}

static int cmd_add (int ac, char **av)
{
	int rc = OK; bool any = false;
	for (int i = 0; i < ac; i++)
	{
		int l = (int) strlen (av[i]);
		if (l > 4 && !strcmp (av[i] + l - 4, ".opk"))			// a package file
		{
			char p[300]; arc::scopy (p, av[i], sizeof p);
			if (!strchr (p, ':')) { char cwd[200]; kapi_getcwd (cwd, sizeof cwd); arc::join (p, sizeof p, cwd, av[i]); }
			int r = M->install_file (p, 0, 0, R); if (r != OK) rc = r; any = true; continue;
		}
		Inst *in = M->db.find (av[i]);
		if (in) { printf ("%s is already installed (%s)\n", av[i], in->version ()); if (rc == OK) rc = E_ALREADY; continue; }
		if (!M->haveIndex && need_index () != OK) return 2;
		const Pkg *list[32]; int err = 0;
		int n = M->resolve (av[i], list, 32, &err, R);
		if (!n) { rc = err ? err : E_NOTFOUND; continue; }
		int r = install_list (list, n); if (r != OK) rc = r; any = true;
	}
	return rc == OK ? 0 : rc == E_ALREADY && !any ? 1 : rc == E_ALREADY ? 0 : 2;
}

static int cmd_delete (int ac, char **av)
{
	bool purge = false; int rc = 0;
	for (int i = 0; i < ac; i++)
	{
		if (eq (av[i], "-p")) { purge = true; continue; }
		int r = M->remove (av[i], purge, R);
		if (r == E_NOTINST) { if (!rc) rc = 1; } else if (r != OK) rc = 2;
	}
	return rc;
}

// update: those named, or (-a) every one with an update (not "never"); upgrade: the "auto" ones
static int cmd_update (int ac, char **av, bool upgrade)
{
	if (need_index () != OK) return 2;
	bool all = upgrade;
	for (int i = 0; i < ac; i++) if (eq (av[i], "-a")) all = true;
	const Pkg *list[128]; int n = 0, rc = 0, manual = 0;
	if (all)
		for (int i = 0; i < M->db.n && n < 128; i++)
		{
			Inst &in = *M->db.v[i];
			const Pkg *u = M->update_for (in);
			if (!u || M->staged (in.name) || eq (in.mode (), "never")) continue;
			if (upgrade && !eq (in.mode (), "auto")) { printf ("%s %s -> %s available (manual)\n", in.name, in.version (), u->version); manual++; continue; }
			list[n++] = u;
		}
	else
		for (int i = 0; i < ac && n < 128; i++)
		{
			Inst *in = M->db.find (av[i]);
			if (!in) { printf ("%s is not installed\n", av[i]); rc = 1; continue; }
			const Pkg *u = M->update_for (*in);
			if (!u) { printf ("%s is up to date (%s)\n", av[i], in->version ()); continue; }
			list[n++] = u;
		}
	// their needs first; one for a newer kernel waits for the system's update (and its restart)
	const Pkg *full[160]; int fn = 0;
	for (int i = 0; i < n; i++)
	{
		Need kn[16]; int kk = parse_needs (list[i]->needs, kn, 16); bool later = false;
		for (int k = 0; k < kk; k++) if (eq (kn[k].name, "kapi") && kapi_level () < atoi (kn[k].ver)) later = true;
		if (later) { printf ("%s %s needs the system's update first (then a restart)\n", list[i]->name, list[i]->version); continue; }
		Need nd[16]; int nn = parse_needs (list[i]->needs, nd, 16);
		for (int k = 0; k < nn; k++)
		{
			if (eq (nd[k].name, "kapi")) continue;
			Inst *in = M->db.find (nd[k].name);
			if (in && vcmp (in->version (), nd[k].ver) >= 0) continue;
			const Pkg *q = M->index.find (nd[k].name);
			bool dup = false; for (int j = 0; j < fn; j++) if (full[j] == q) dup = true;
			for (int j = i; j < n; j++) if (list[j] == q) dup = true;
			if (q && !dup && fn < 160) full[fn++] = q;
		}
		bool dup = false; for (int j = 0; j < fn; j++) if (full[j] == list[i]) dup = true;
		if (!dup && fn < 160) full[fn++] = list[i];
	}
	if (!fn) { if (!manual) printf ("everything is up to date\n"); return rc ? rc : 1; }
	return install_list (full, fn) == OK ? rc : 2;
}

static int cmd_check (void)
{
	if (need_index () != OK) return 2;
	int k = 0;
	for (int i = 0; i < M->db.n; i++)
	{
		const Pkg *u = M->update_for (*M->db.v[i]);
		if (u) { printf ("%-16s %s -> %s  (%s)\n", M->db.v[i]->name, M->db.v[i]->version (), u->version, M->db.v[i]->mode ()); k++; }
	}
	printf (k ? "%d update%s available\n" : "everything is up to date\n", k, k == 1 ? "" : "s");
	return k ? 0 : 1;
}

static int run (const char *c, char **rest, int nr);

int main (void)
{
	static char buf[2048]; char *av[64];
	int ac = cli_args (buf, sizeof buf, av, 64);
	static Manager m; M = &m;
	int a0 = 0;
	if (ac >= 2 && eq (av[0], "-r")) { arc::scopy (m.repo, av[1], sizeof m.repo); a0 = 2; }
	if (ac - a0 < 1)
	{
		printf ("usage: pkg list [-a] [filter] | info NAME | add NAME... | delete [-p] NAME... |\n"
			"       update NAME... | update -a | upgrade | check | mode NAME manual|auto|never | commit\n"
			"       [-r REPO] before the command: another repository\n");
		return 3;
	}
	const char *c = av[a0]; char **rest = av + a0 + 1; int nr = ac - a0 - 1;
	int rc = run (c, rest, nr);
	M->refresh_desktop ();					// (an app installed, updated or removed: the dock again)
	return rc;
}

static int run (const char *c, char **rest, int nr)
{
	Manager &m = *M;
	if (eq (c, "list")) return cmd_list (nr, rest);
	if (eq (c, "info") && nr == 1) return cmd_info (rest[0]);
	if (eq (c, "add") && nr) return cmd_add (nr, rest);
	if ((eq (c, "delete") || eq (c, "remove")) && nr) return cmd_delete (nr, rest);
	if (eq (c, "update") && nr) return cmd_update (nr, rest, false);
	if (eq (c, "upgrade")) return cmd_update (0, 0, true);
	if (eq (c, "check")) return cmd_check ();
	if (eq (c, "mode") && nr == 2)
	{
		if (!eq (rest[1], "manual") && !eq (rest[1], "auto") && !eq (rest[1], "never")) { printf ("the mode: manual, auto or never\n"); return 3; }
		if (!m.set_mode (rest[0], rest[1])) { printf ("%s is not installed\n", rest[0]); return 1; }
		printf ("%s: %s\n", rest[0], rest[1]); return 0;
	}
	if (eq (c, "commit"))
	{
		m.commit (R);
		if (m.kernelChanged) { printf ("the system changed: restarting\n"); fflush (stdout); kapi_msleep (1500); kapi_reboot (); }
		return 0;
	}
	printf ("pkg: unknown command or missing name: %s (pkg alone: the usage)\n", c);
	return 3;
}
