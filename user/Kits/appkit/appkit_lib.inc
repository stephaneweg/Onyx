//
// appkit_lib.inc -- AppKit's small services for every program: strings without a C library, the
// console, the .ini reader, the keyboard layout. They were user/applib.h (a header of inline
// functions, copied into every program) until 2026-10-05; appkit.h declares them.
//
// Compiled into AppKit (appkit.c) and, for the tests of the kernel's table and the PC builds, inline
// into the program (appkit.h's end). The .ini reader's store is AppKit's data: a program's own copy
// (a library's data is private to each program), as it was when the header carried it.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifdef KAPI_IMPL
#define APPKIT_LIB
#else
#define APPKIT_LIB	static inline
#endif

// ---- strings ----
APPKIT_LIB void ax_strcat (char *dst, int cap, int *pos, const char *src)
{
	while (*src != '\0' && *pos < cap - 1)
	{
		dst[(*pos)++] = *src++;
	}
	dst[*pos] = '\0';
}

APPKIT_LIB void ax_app_path (char *dst, int cap, const char *name, const char *suffix)
{
	int p = 0;
	ax_strcat (dst, cap, &p, "SD:apps/");
	ax_strcat (dst, cap, &p, name);
	ax_strcat (dst, cap, &p, suffix);
}

APPKIT_LIB int ax_streq (const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) { a++; b++; }
	return *a == *b;
}

APPKIT_LIB int ax_strlen (const char *s)
{
	int n = 0;
	while (s[n] != '\0') n++;
	return n;
}

APPKIT_LIB int ax_itoa (int v, char *b)
{
	char t[12]; int n = 0, p = 0;
	unsigned u = (unsigned) v;
	if (v < 0) { b[p++] = '-'; u = 0u - u; }
	if (u == 0) t[n++] = '0';
	while (u) { t[n++] = (char) ('0' + u % 10); u /= 10; }
	while (n) b[p++] = t[--n];
	b[p] = '\0';
	return p;
}

APPKIT_LIB void ax_fmt2 (char *d, int v)
{
	if (v < 0) v = 0;
	d[0] = (char) ('0' + (v / 10) % 10);
	d[1] = (char) ('0' + v % 10);
}

// ---- the console ----
APPKIT_LIB void ax_puts (const char *s)  { kapi_stdout_write (s, (unsigned) ax_strlen (s)); }
APPKIT_LIB void ax_putln (const char *s) { ax_puts (s); kapi_stdout_write ("\n", 1); }

// ---- the .ini reader ----
static char appkit_ini_sec[INI_MAX][INI_STRLEN];
static char appkit_ini_key[INI_MAX][INI_STRLEN];
static char appkit_ini_val[INI_MAX][INI_STRLEN];
static int  appkit_ini_n = 0;
static char appkit_ini_buf[INI_BUFSZ];

// buf[s..e) into dst, the spaces and tabs around it trimmed, INI_STRLEN at most.
static void appkit_ini_range (char *dst, const char *buf, int s, int e)
{
	while (s < e && (buf[s] == ' ' || buf[s] == '\t')) s++;
	while (e > s && (buf[e - 1] == ' ' || buf[e - 1] == '\t')) e--;
	int j = 0;
	for (int i = s; i < e && j < INI_STRLEN - 1; i++) dst[j++] = buf[i];
	dst[j] = '\0';
}

APPKIT_LIB int app_ini_load_path (const char *path)
{
	char *b = appkit_ini_buf;
	appkit_ini_n = 0;

	void *f = kapi_open (path);
	if (f == 0) return -1;
	int n = kapi_read (f, b, INI_BUFSZ - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	b[n] = '\0';

	char cur[INI_STRLEN]; cur[0] = '\0';			// current section
	int i = 0;
	while (b[i] != '\0' && appkit_ini_n < INI_MAX)
	{
		while (b[i] == ' ' || b[i] == '\t') i++;
		int ls = i;
		while (b[i] != '\0' && b[i] != '\n' && b[i] != '\r') i++;
		int le = i;
		while (b[i] == '\n' || b[i] == '\r') i++;
		while (le > ls && (b[le - 1] == ' ' || b[le - 1] == '\t')) le--;
		if (le <= ls) continue;				// blank
		char c0 = b[ls];
		if (c0 == ';' || c0 == '#') continue;		// comment
		if (c0 == '[')					// [section]
		{
			int e = le;
			if (e > ls + 1 && b[e - 1] == ']') e--;
			appkit_ini_range (cur, b, ls + 1, e);
			continue;
		}
		int eq = ls;					// key=value
		while (eq < le && b[eq] != '=') eq++;
		if (eq >= le) continue;				// no '='
		int k = appkit_ini_n;
		int j = 0;					// section: cur is already clean
		while (cur[j] != '\0' && j < INI_STRLEN - 1) { appkit_ini_sec[k][j] = cur[j]; j++; }
		appkit_ini_sec[k][j] = '\0';
		appkit_ini_range (appkit_ini_key[k], b, ls, eq);
		appkit_ini_range (appkit_ini_val[k], b, eq + 1, le);
		appkit_ini_n++;
	}
	return appkit_ini_n;
}

APPKIT_LIB int app_ini_load (const char *filename)
{
	char path[96];
	int pn = kapi_app_dir (path, sizeof (path));		// "SD:apps/<self>.app/"
	for (int i = 0; filename[i] != '\0' && pn < (int) sizeof (path) - 1; i++)
		path[pn++] = filename[i];
	path[pn] = '\0';
	return app_ini_load_path (path);
}

APPKIT_LIB const char *app_ini_get (const char *section, const char *key, const char *def)
{
	const char *sec = section ? section : "";
	for (int i = 0; i < appkit_ini_n; i++)
		if (ax_streq (appkit_ini_sec[i], sec) && ax_streq (appkit_ini_key[i], key))
			return appkit_ini_val[i];
	return def;
}

APPKIT_LIB int app_ini_get_int (const char *section, const char *key, int def)
{
	const char *v = app_ini_get (section, key, 0);
	if (v == 0) return def;
	int i = 0, neg = 0, any = 0; long r = 0;
	if (v[0] == '-') { neg = 1; i = 1; } else if (v[0] == '+') i = 1;
	while (v[i] >= '0' && v[i] <= '9') { r = r * 10 + (v[i] - '0'); i++; any = 1; }
	if (!any) return def;
	return (int) (neg ? -r : r);
}

APPKIT_LIB int app_ini_count (void) { return appkit_ini_n; }
APPKIT_LIB const char *app_ini_section (int i) { return i >= 0 && i < appkit_ini_n ? appkit_ini_sec[i] : ""; }
APPKIT_LIB const char *app_ini_key (int i)     { return i >= 0 && i < appkit_ini_n ? appkit_ini_key[i] : ""; }
APPKIT_LIB const char *app_ini_value (int i)   { return i >= 0 && i < appkit_ini_n ? appkit_ini_val[i] : ""; }

// ---- the keyboard layout ----
APPKIT_LIB int ax_load_keymap (const char *name)
{
	char path[64]; int p = 0;
	const char *pre = "SD:/etc/keymaps/";
	for (int k = 0; pre[k]; k++) path[p++] = pre[k];
	for (int k = 0; name[k] && p < (int) sizeof path - 6; k++) path[p++] = name[k];
	const char *suf = ".kmap"; for (int k = 0; suf[k]; k++) path[p++] = suf[k];
	path[p] = '\0';

	int ok = 0;
	void *f = kapi_open (path);
	if (f)
	{
		static unsigned char buf[2048];
		int n = kapi_read (f, buf, sizeof buf);
		kapi_close (f);
		if (n > 0) ok = kapi_set_keymap_data (name, buf, (unsigned) n);
	}
	if (!ok) ok = kapi_set_keymap (name);		// fallback: compiled-in country map
	return ok;
}

// ---- Starting programs ----
APPKIT_LIB char lx_low (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
APPKIT_LIB int lx_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
APPKIT_LIB void lx_cat (char *d, int cap, int *n, const char *s) { while (*s && *n < cap - 1) d[(*n)++] = *s++; d[*n] = '\0'; }
APPKIT_LIB int lx_exists (const char *path) { void *f = kapi_open (path); if (f == 0) return 0; kapi_close (f); return 1; }
APPKIT_LIB int lx_entry (int i, char *ext, int ecap, char *prog, int pcap)
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
APPKIT_LIB int lx_lists_ext (const char *v, int len, const char *ext, int games)
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
APPKIT_LIB int lx_app_for (const char *ext, char *out, int cap)
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
APPKIT_LIB int lx_runner (const char *path, char *out, int cap)
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
APPKIT_LIB void lx_cmdline (char *out, int cap, const char *path, const char *args)
{
	int n = 0, sp = 0;
	out[0] = '\0';
	for (const char *q = path; *q; q++) if (*q == ' ') sp = 1;
	if (sp) lx_cat (out, cap, &n, "\"");
	lx_cat (out, cap, &n, path);
	if (sp) lx_cat (out, cap, &n, "\"");
	if (args != 0 && args[0]) { lx_cat (out, cap, &n, " "); lx_cat (out, cap, &n, args); }
}
APPKIT_LIB int lx_open_as (const char *path, const char *args, const char *name)
{
	char run[160], cmd[512];
	if (lx_runner (path, run, sizeof run))
	{
		lx_cmdline (cmd, sizeof cmd, path, args);
		return kapi_exec_as (run, cmd, name);
	}
	return name != 0 ? kapi_exec_as (path, args ? args : "", name) : kapi_exec (path, args ? args : "");
}
APPKIT_LIB int lx_open (const char *path, const char *args) { return lx_open_as (path, args, 0); }
APPKIT_LIB int lx_launch_dir (const char *dir, const char *name, const char *args)
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
APPKIT_LIB int lx_launch (const char *name, const char *args)
{
	char main[160]; int n = 0;
	lx_cat (main, sizeof main, &n, "SD:apps/"); lx_cat (main, sizeof main, &n, name); lx_cat (main, sizeof main, &n, ".app/main");
	if (lx_exists (main)) return (args == 0 || !args[0]) ? kapi_launch (name) : kapi_exec (main, args);
	char dir[160]; int d = 0;
	lx_cat (dir, sizeof dir, &d, "SD:apps/"); lx_cat (dir, sizeof dir, &d, name); lx_cat (dir, sizeof dir, &d, ".app");
	return lx_launch_dir (dir, name, args);
}

#undef APPKIT_LIB
