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

#undef APPKIT_LIB
