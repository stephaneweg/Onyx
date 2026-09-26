//
// launch.h -- starting programs from user space. The kernel only loads ELF programs; the
// other formats have a RUNNER (SD:/etc/runners.ini, "extension = program"): a BASIC
// program (.bas, .bax) is run by SD:/bin/basic, tomorrow a ROM by its emulator.
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

// The runner of a file, by its extension: 1 + out, 0 if none.
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
