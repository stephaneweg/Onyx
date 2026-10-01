//
// launch.h -- starting programs from user space. The kernel only loads ELF programs; the
// other formats have a RUNNER (SD:/etc/runners.ini, "extension = program"): a BASIC
// program (.bas, .bax) is run by SD:/bin/basic -- or an app that says it opens them: an emulator's
// app.txt names the games it plays, "games = Game Boy Color: gbc; Game Boy: gb" (and "opens = dol"
// for files it opens that are not games): installing an emulator's package is enough.
//   lx_launch (name, args)   an app: SD:apps/<name>.app/main (an ELF), else its main.<ext>
//                            run by that extension's runner (named after the app)
//   lx_open (path, args)     a program file: an ELF as it is, else by its runner
//   lx_runner (path, out)    the runner of a file (by its extension), 0 if none
// C and C++ (run, init, the apps).
//
#ifndef _onyx_launch_h
#define _onyx_launch_h

#include "kapi.h"

#define LX_INI	"SD:/etc/runners.ini"

static inline char lx_low (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
static inline int lx_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void lx_cat (char *d, int cap, int *n, const char *s) { while (*s && *n < cap - 1) d[(*n)++] = *s++; d[*n] = '\0'; }
static inline int lx_exists (const char *path) { void *f = kapi_open (path); if (f == 0) return 0; kapi_close (f); return 1; }

// The i-th "ext = program" line of runners.ini (0-based): 1 found, 0 past the end.
static inline int lx_entry (int i, char *ext, int ecap, char *prog, int pcap)
{
	static char buf[1024]; static int len = -1;
	if (len < 0)
	{
		len = 0;
		void *f = kapi_open (LX_INI);
		if (f != 0) { int r = kapi_read (f, buf, sizeof buf - 1); kapi_close (f); len = r > 0 ? r : 0; }
		buf[len] = '\0';
	}
	int p = 0, k = 0;
	while (p < len)
	{
		int ls = p; while (p < len && buf[p] != '\n') p++;
		int le = p; if (p < len) p++;
		while (ls < le && (buf[ls] == ' ' || buf[ls] == '\t')) ls++;
		if (ls >= le || buf[ls] == '#') continue;
		int eq = ls; while (eq < le && buf[eq] != '=') eq++;
		if (eq >= le) continue;
		if (k++ != i) continue;
		int n = 0, e = eq; while (e > ls && (buf[e - 1] == ' ' || buf[e - 1] == '\t')) e--;
		for (int j = ls; j < e && n < ecap - 1; j++) ext[n++] = lx_low (buf[j]);
		ext[n] = '\0';
		int s = eq + 1; while (s < le && (buf[s] == ' ' || buf[s] == '\t')) s++;
		int t = le; while (t > s && (buf[t - 1] == ' ' || buf[t - 1] == '\t' || buf[t - 1] == '\r')) t--;
		n = 0; for (int j = s; j < t && n < pcap - 1; j++) prog[n++] = buf[j];
		prog[n] = '\0';
		return 1;
	}
	return 0;
}

// Does the value of an app.txt's "games" / "opens" (the extensions after each "System:", or all the
// words of "opens") hold ext?
static inline int lx_lists_ext (const char *v, int len, const char *ext, int games)
{
	int i = 0;
	while (i < len)
	{
		if (games) { while (i < len && v[i] != ':') i++; if (i >= len) return 0; i++; }	// past "System:"
		while (i < len && v[i] != ';')
		{
			while (i < len && (v[i] == ' ' || v[i] == ',' || v[i] == '.' || v[i] == '\t')) i++;
			int w = i; while (i < len && v[i] != ' ' && v[i] != ',' && v[i] != ';' && v[i] != '\t' && v[i] != '\r') i++;
			int j = 0; while (w + j < i && ext[j] && lx_low (v[w + j]) == ext[j]) j++;
			if (i > w && w + j == i && ext[j] == '\0') return 1;
		}
		if (i < len) i++;						// (';': the next system)
	}
	return 0;
}
// The app (SD:/apps/<name>.app/main) whose app.txt opens files of this extension: 1 + out, 0 if none.
static inline int lx_app_for (const char *ext, char *out, int cap)
{
	void *d = kapi_opendir ("SD:/apps");
	if (d == 0) return 0;
	struct kapi_dirent e; int found = 0;
	static char buf[1024];
	while (!found && kapi_readdir (d, &e))
	{
		int nl = lx_len (e.name);
		if (!e.is_dir || nl < 5 || e.name[nl - 4] != '.' || lx_low (e.name[nl - 3]) != 'a') continue;
		char p[128]; int n = 0; lx_cat (p, sizeof p, &n, "SD:/apps/"); lx_cat (p, sizeof p, &n, e.name); lx_cat (p, sizeof p, &n, "/app.txt");
		void *f = kapi_open (p);
		if (f == 0) continue;
		int len = kapi_read (f, buf, sizeof buf - 1); kapi_close (f);
		if (len <= 0) continue;
		buf[len] = '\0';
		for (int ls = 0; ls < len && !found; )
		{
			int le = ls; while (le < len && buf[le] != '\n') le++;
			int k = ls; while (k < le && (buf[k] == ' ' || buf[k] == '\t')) k++;
			int games = le - k > 5 && buf[k] == 'g' && buf[k + 1] == 'a' && buf[k + 2] == 'm' && buf[k + 3] == 'e' && buf[k + 4] == 's';
			int opens = le - k > 5 && buf[k] == 'o' && buf[k + 1] == 'p' && buf[k + 2] == 'e' && buf[k + 3] == 'n' && buf[k + 4] == 's';
			if (games || opens)
			{
				int q = k + 5; while (q < le && (buf[q] == ' ' || buf[q] == '\t')) q++;
				if (q < le && buf[q] == '=' && lx_lists_ext (buf + q + 1, le - q - 1, ext, games))
				{
					int m = 0; lx_cat (p, sizeof p, &m, "SD:/apps/"); lx_cat (p, sizeof p, &m, e.name); lx_cat (p, sizeof p, &m, "/main");
					if (lx_exists (p)) { m = 0; lx_cat (out, cap, &m, p); found = 1; }
				}
			}
			ls = le + 1;
		}
	}
	kapi_closedir (d);
	return found;
}

// The runner of a file, by its extension (runners.ini, else the built-in list): 1 + out, 0 if none.
static inline int lx_runner (const char *path, char *out, int cap)
{
	int n = lx_len (path), d = n;
	while (d > 0 && path[d - 1] != '.' && path[d - 1] != '/' && path[d - 1] != ':') d--;
	if (d <= 0 || path[d - 1] != '.') return 0;
	char ext[16], e2[16], prog[160];
	int k = 0; for (int i = d; i < n && k < 15; i++) ext[k++] = lx_low (path[i]);
	ext[k] = '\0';
	for (int i = 0; lx_entry (i, e2, sizeof e2, prog, sizeof prog); i++)
	{
		int j = 0; while (e2[j] && e2[j] == ext[j]) j++;
		if (e2[j] == '\0' && ext[j] == '\0') { int m = 0; lx_cat (out, cap, &m, prog); return 1; }
	}
	// An app that opens this extension (its app.txt: games = / opens =)
	if (lx_app_for (ext, out, cap)) return 1;
	// Not in runners.ini (an older copy on the card): Onyx's own runners (if there)
	static const char *const known[][2] = {
		{ "bax", "SD:/bin/basic" }, { "bas", "SD:/bin/basic" },
		{ "gb", "SD:/apps/gbemu.app/main" }, { "gbc", "SD:/apps/gbemu.app/main" },
		{ "gba", "SD:/apps/gbaemu.app/main" }, { "nes", "SD:/apps/nesemu.app/main" },
		{ "sfc", "SD:/apps/snesemu.app/main" }, { "smc", "SD:/apps/snesemu.app/main" },
		{ "z64", "SD:/apps/n64emu.app/main" }, { "n64", "SD:/apps/n64emu.app/main" }, { "v64", "SD:/apps/n64emu.app/main" },
		{ "wad", "SD:/apps/doom.app/main" } };
	for (unsigned i = 0; i < sizeof known / sizeof known[0]; i++)
	{
		const char *e = known[i][0];
		int j = 0; while (e[j] && e[j] == ext[j]) j++;
		if (e[j] == '\0' && ext[j] == '\0' && lx_exists (known[i][1])) { int m = 0; lx_cat (out, cap, &m, known[i][1]); return 1; }
	}
	return 0;
}

// The runner's command line: "path" args (the path quoted when it has spaces).
static inline void lx_cmdline (char *out, int cap, const char *path, const char *args)
{
	int n = 0, sp = 0;
	out[0] = '\0';
	for (const char *q = path; *q; q++) if (*q == ' ') sp = 1;
	if (sp) lx_cat (out, cap, &n, "\"");
	lx_cat (out, cap, &n, path);
	if (sp) lx_cat (out, cap, &n, "\"");
	if (args != 0 && args[0]) { lx_cat (out, cap, &n, " "); lx_cat (out, cap, &n, args); }
}

// A program file: by its runner if its extension has one, else as an ELF. name: the
// process name (0: the kernel's, from the path). 1 = started.
static inline int lx_open_as (const char *path, const char *args, const char *name)
{
	char run[160], cmd[512];
	if (lx_runner (path, run, sizeof run))
	{
		lx_cmdline (cmd, sizeof cmd, path, args);
		return kapi_exec_as (run, cmd, name);
	}
	return name != 0 ? kapi_exec_as (path, args ? args : "", name) : kapi_exec (path, args ? args : "");
}
static inline int lx_open (const char *path, const char *args) { return lx_open_as (path, args, 0); }

// An app bundle by folder path (".../x.app"): its main (ELF), else main.<ext> for a runner.
static inline int lx_launch_dir (const char *dir, const char *name, const char *args)
{
	char p[200]; int n = 0;
	lx_cat (p, sizeof p, &n, dir); lx_cat (p, sizeof p, &n, "/main");
	if (lx_exists (p)) return kapi_exec_as (p, args ? args : "", name);
	char ext[16], prog[160];
	for (int i = 0; lx_entry (i, ext, sizeof ext, prog, sizeof prog); i++)
	{
		int m = n; lx_cat (p, sizeof p, &m, "."); lx_cat (p, sizeof p, &m, ext);
		if (lx_exists (p)) return lx_open_as (p, args, name);
		p[n] = '\0';
	}
	return 0;
}

// An app by name (SD:apps/<name>.app), with arguments (0 / "": none). 1 = started.
static inline int lx_launch (const char *name, const char *args)
{
	char main[160]; int n = 0;
	lx_cat (main, sizeof main, &n, "SD:apps/"); lx_cat (main, sizeof main, &n, name); lx_cat (main, sizeof main, &n, ".app/main");
	if (lx_exists (main)) return (args == 0 || !args[0]) ? kapi_launch (name) : kapi_exec (main, args);
	char dir[160]; int d = 0;
	lx_cat (dir, sizeof dir, &d, "SD:apps/"); lx_cat (dir, sizeof dir, &d, name); lx_cat (dir, sizeof dir, &d, ".app");
	return lx_launch_dir (dir, name, args);
}

#endif
