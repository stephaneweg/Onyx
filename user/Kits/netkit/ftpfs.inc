//
// ftpfs.inc -- the code of ftpfs.h (NetKit: compiled into netkit.so; a PC build has it inline).
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
#ifdef NK_IMPL
#define NK_FN	extern "C"
#else
#define NK_FN	static inline
#endif
NK_FN void ftpfs__cpy (char *d, const char *s, int cap)
{ int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static const char ftpfs__key[] = "Onyx-ftpfs-v1";NK_FN void ftpfs_obf_hex (const char *in, char *out, int cap)
{
	static const char H[] = "0123456789abcdef";
	int k = 0;
	for (int i = 0; in[i] && k < cap - 2; i++)
	{
		unsigned char c = (unsigned char) in[i] ^ (unsigned char) ftpfs__key[i % (int) (sizeof ftpfs__key - 1)];
		out[k++] = H[c >> 4]; out[k++] = H[c & 15];
	}
	out[k] = '\0';
}
NK_FN int ftpfs__hv (char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; }
NK_FN void ftpfs_unobf_hex (const char *in, char *out, int cap)
{
	int k = 0;
	for (int i = 0; in[i] && in[i + 1] && k < cap - 1; i += 2, k++)
		out[k] = (char) (((ftpfs__hv (in[i]) << 4) | ftpfs__hv (in[i + 1])) ^ ftpfs__key[k % (int) (sizeof ftpfs__key - 1)]);
	out[k] = '\0';
}
NK_FN int ftpfs_parse_site (const char *line, struct ftpfs_site *s)
{
	char w[6][140]; int nw = 0;
	const char *p = line;
	while (*p == ' ' || *p == '\t') p++;
	if (*p == '#' || *p == ';' || *p == '\0') return 0;
	while (*p && nw < 6)
	{
		int n = 0;
		if (nw == 5) { while (*p && n < 139) w[nw][n++] = *p++; while (n && (w[nw][n - 1] == ' ' || w[nw][n - 1] == '\t')) n--; }	// folder: the rest
		else while (*p && *p != ' ' && *p != '\t' && n < 139) w[nw][n++] = *p++;
		w[nw++][n] = '\0';
		while (*p == ' ' || *p == '\t') p++;
	}
	if (nw < 3) return 0;
	ftpfs__cpy (s->host, w[0], sizeof s->host);
	ftpfs__cpy (s->user, (w[1][0] == '-' && !w[1][1]) ? "" : w[1], sizeof s->user);
	if (w[2][0] == '-' && !w[2][1]) s->pass[0] = '\0'; else ftpfs_unobf_hex (w[2], s->pass, sizeof s->pass);
	ftpfs__cpy (s->port, nw > 3 ? w[3] : "", sizeof s->port);
	s->tls = nw > 4 && w[4][0] == '1';
	ftpfs__cpy (s->folder, nw > 5 ? w[5] : "", sizeof s->folder);
	return 1;
}
NK_FN int ftpfs_format_site (const struct ftpfs_site *s, char *out, int cap)
{
	char hx[140];
	if (s->pass[0]) ftpfs_obf_hex (s->pass, hx, sizeof hx); else ftpfs__cpy (hx, "-", sizeof hx);
	const char *part[6] = { s->host, s->user[0] ? s->user : "-", hx, s->port[0] ? s->port : "21",
				s->tls ? "1" : "0", s->folder[0] ? s->folder : "/" };
	int n = 0;
	for (int k = 0; k < 6; k++)
	{
		if (k) { if (n < cap - 2) out[n++] = ' '; }
		for (int i = 0; part[k][i] && n < cap - 2; i++) out[n++] = part[k][i];
	}
	out[n++] = '\n'; out[n] = '\0';
	return n;
}
NK_FN int ftpfs_text_fix (char *b, int n)
{
	unsigned char *u = (unsigned char *) b;
	if (n >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
	{ for (int i = 3; i <= n; i++) b[i - 3] = b[i]; return n - 3; }
	if (n >= 2 && ((u[0] == 0xFF && u[1] == 0xFE) || (u[0] == 0xFE && u[1] == 0xFF)))
	{
		int lo = u[0] == 0xFF ? 2 : 3, k = 0;		// LE: low byte first
		for (int i = lo; i < n; i += 2) b[k++] = b[i];
		b[k] = '\0';
		return k;
	}
	return n;
}
NK_FN int ftpfs__ieq (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
		if (x != y) return 0;
		if (!x) return 1;
	}
}
NK_FN int ftpfs_load_sites (struct ftpfs_site *sites, int max)
{
	void *f = kapi_open (FTPFS_SITES_FILE);
	if (!f) return 0;
	static char buf[FTPFS_MAXSITES * 540 + 512];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return 0;
	buf[n] = '\0';
	n = ftpfs_text_fix (buf, n);
	int cnt = 0, sect = -1;				// sect: the [section] being filled
	char *line = buf;
	while (*line && cnt <= max)
	{
		char *e = line; while (*e && *e != '\n' && *e != '\r') e++;
		char c = *e; *e = '\0';
		char *p = line; while (*p == ' ' || *p == '\t') p++;
		char *eq = p; while (*eq && *eq != '=') eq++;
		if (*p == '[')
		{
			sect = -1;
			if (cnt < max)
			{
				struct ftpfs_site *s = &sites[cnt];
				int k = 0; p++;
				while (*p && *p != ']' && k < (int) sizeof s->host - 1) s->host[k++] = *p++;
				s->host[k] = '\0';
				s->user[0] = s->pass[0] = s->port[0] = s->folder[0] = '\0'; s->tls = 0;
				if (k) sect = cnt++;
			}
		}
		else if (*eq == '=' && *p != '#' && *p != ';')
		{
			if (sect >= 0)
			{
				struct ftpfs_site *s = &sites[sect];
				char *ke = eq; while (ke > p && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
				*ke = '\0';
				char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
				char *ve = v + 0; while (*ve) ve++;
				while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) *--ve = '\0';
				if      (ftpfs__ieq (p, "user"))     ftpfs__cpy (s->user, v, sizeof s->user);
				else if (ftpfs__ieq (p, "password") || ftpfs__ieq (p, "pass")) ftpfs__cpy (s->pass, v, sizeof s->pass);
				else if (ftpfs__ieq (p, "hexpass"))  ftpfs_unobf_hex (v, s->pass, sizeof s->pass);
				else if (ftpfs__ieq (p, "port"))     ftpfs__cpy (s->port, v, sizeof s->port);
				else if (ftpfs__ieq (p, "tls") || ftpfs__ieq (p, "ftps")) s->tls = v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T';
				else if (ftpfs__ieq (p, "folder"))   ftpfs__cpy (s->folder, v, sizeof s->folder);
				else if (ftpfs__ieq (p, "host"))     ftpfs__cpy (s->host, v, sizeof s->host);
			}
		}
		else if (cnt < max && ftpfs_parse_site (line, &sites[cnt])) { cnt++; sect = -1; }
		if (!c) break;
		line = e + 1;
	}
	return cnt;
}
NK_FN int ftpfs__pid (void)
{
	int pid = kapi_ipc_lookup ("ftpfs");
	if (pid == 0)
	{
		kapi_exec ("SD:/bin/ftpfs", "");
		for (int i = 0; i < 300 && pid == 0; i++) { kapi_msleep (10); pid = kapi_ipc_lookup ("ftpfs"); }
	}
	return pid;
}
NK_FN int ftpfs__send (int type, const char *const *part, int nparts)
{
	int pid = ftpfs__pid ();
	if (pid == 0) return 0;
	char msg[512]; int n = 0;
	for (int k = 0; k < nparts; k++)
	{
		for (int i = 0; part[k][i] && n < (int) sizeof msg - 2; i++) msg[n++] = part[k][i];
		msg[n++] = '\0';
	}
	return kapi_mailbox_send (pid, type, msg, (unsigned) n) != 0;
}
NK_FN int ftpfs_login (const char *host, const char *user, const char *pass, int remember)
{
	const char *part[3] = { host, user, pass };
	return ftpfs__send (remember ? 2 : 1, part, 3);		// MSG_LOGIN(_SAVE)
}
NK_FN int ftpfs_login_site (const struct ftpfs_site *s, int remember)
{
	const char *part[6] = { s->host, s->user, s->pass, s->port, s->tls ? "1" : "0", s->folder };
	return ftpfs__send (remember ? 2 : 1, part, 6);
}
NK_FN int ftpfs_forget (const char *host)
{
	const char *part[1] = { host };
	return ftpfs__send (3, part, 1);			// MSG_FORGET
}

#undef NK_FN
