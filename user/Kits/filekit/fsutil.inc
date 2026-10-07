//
// fsutil.inc -- the code of fsutil.h (FileKit: fsutil.cpp compiles it into filekit.so; a PC build has it
// inline).
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
#ifdef FS_IMPL
#define FS_FN	extern "C"
#else
#define FS_FN	static inline
#endif
FS_FN int fs_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
FS_FN void fs_copy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
FS_FN char fs_lower (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
FS_FN int fs_ci_cmp (const char *a, const char *b)
{
	for (;; a++, b++) { char x = fs_lower (*a), y = fs_lower (*b); if (x != y || !x) return (unsigned char) x - (unsigned char) y; }
}
FS_FN void fs_join (char *out, int cap, const char *dir, const char *name)
{
	int p = 0;
	for (; dir[p] && p < cap - 1; p++) out[p] = dir[p];
	if (p > 0 && out[p - 1] != '/' && p < cap - 1) out[p++] = '/';
	for (int k = 0; name[k] && p < cap - 1; k++) out[p++] = name[k];
	out[p] = '\0';
}
FS_FN const char *fs_basename (const char *path)
{
	const char *n = path;
	for (const char *p = path; *p; p++) if (*p == '/') n = p + 1;
	return n;
}
FS_FN void fs_dirname (char *out, int cap, const char *path)
{
	fs_copy (out, path, cap);
	int n = fs_len (out);
	while (n > 0 && out[n - 1] != '/') n--;
	if (n > 0) n--;
	out[n] = '\0';
	if (fs_len (out) <= 3) fs_copy (out, "SD:/", cap);
}
FS_FN bool fs_is_dir (const char *path)
{
	void *d = kapi_opendir (path);
	if (d) { kapi_closedir (d); return true; }
	return false;
}
FS_FN bool fs_exists (const char *path)
{
	void *f = kapi_open (path);
	if (f) { kapi_close (f); return true; }
	return fs_is_dir (path);
}
FS_FN bool fs_copy_file (const char *src, const char *dst)
{
	void *f = kapi_open (src);
	if (!f) return false;
	unsigned n = kapi_fsize (f);
	unsigned char *buf = new unsigned char[n ? n : 1];
	if (!buf) { kapi_close (f); return false; }
	unsigned done = 0;
	while (done < n)
	{
		int r = kapi_read (f, buf + done, n - done);
		if (r <= 0) break;
		done += (unsigned) r;
	}
	kapi_close (f);
	bool ok = done == n && kapi_save_file (dst, buf, n) == (int) n;	// (the bytes written, or -1)
	delete [] buf;
	return ok;
}
FS_FN bool fs_skip_dot (const char *n) { return n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')); }
FS_FN bool fs_copy_tree (const char *src, const char *dst, int depth)
{
	if (depth > 12) return false;
	void *d = kapi_opendir (src);
	if (!d) return fs_copy_file (src, dst);
	kapi_mkdir (dst);
	struct kapi_dirent ent;
	bool ok = true;
	while (kapi_readdir (d, &ent))
	{
		if (fs_skip_dot (ent.name)) continue;
		char s[FS_PATHL], t[FS_PATHL];
		fs_join (s, sizeof s, src, ent.name);
		fs_join (t, sizeof t, dst, ent.name);
		ok = (ent.is_dir ? fs_copy_tree (s, t, depth + 1) : fs_copy_file (s, t)) && ok;
	}
	kapi_closedir (d);
	return ok;
}
FS_FN bool fs_remove_tree (const char *path, int depth)
{
	if (depth > 12) return false;
	void *d = kapi_opendir (path);
	if (d)
	{
		struct kapi_dirent ent;
		static char names[13][32][FS_NAMEL];	// per-depth batch (not on the stack)
		int dirs[32];
		for (;;)				// delete in batches (don't modify while iterating)
		{
			int n = 0;
			while (n < 32 && kapi_readdir (d, &ent))
			{
				if (fs_skip_dot (ent.name)) continue;
				fs_copy (names[depth][n], ent.name, FS_NAMEL); dirs[n] = ent.is_dir; n++;
			}
			kapi_closedir (d);
			if (n == 0) break;
			for (int i = 0; i < n; i++)
			{
				char s[FS_PATHL]; fs_join (s, sizeof s, path, names[depth][i]);
				if (dirs[i]) fs_remove_tree (s, depth + 1); else kapi_remove (s);
			}
			d = kapi_opendir (path);
			if (!d) break;
		}
	}
	return kapi_remove (path) == 0;			// (kapi: 0 = ok)
}
FS_FN void fs_unique_name (char *out, int cap, const char *dir, const char *name, const char *tag)
{
	char base[FS_NAMEL], ext[FS_NAMEL] = "";
	fs_copy (base, name, sizeof base);
	int dot = -1;
	for (int i = 1; base[i]; i++) if (base[i] == '.') dot = i;
	if (dot > 0) { fs_copy (ext, base + dot, sizeof ext); base[dot] = '\0'; }
	for (int n = 0; n < 100; n++)
	{
		char cand[FS_NAMEL]; int p = 0;
		for (int i = 0; base[i] && p < FS_NAMEL - 20; i++) cand[p++] = base[i];
		if (n > 0) for (int i = 0; tag[i]; i++) cand[p++] = tag[i];
		if (n > 1) { cand[p++] = ' '; if (n >= 10) cand[p++] = (char) ('0' + n / 10); cand[p++] = (char) ('0' + n % 10); }
		for (int i = 0; ext[i] && p < FS_NAMEL - 1; i++) cand[p++] = ext[i];
		cand[p] = '\0';
		fs_join (out, cap, dir, cand);
		if (!fs_exists (out)) return;
	}
}
FS_FN int fs_text_fix (char *b, int n)
{
	unsigned char *u = (unsigned char *) b;
	if (n >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
	{ for (int i = 3; i <= n; i++) b[i - 3] = b[i]; return n - 3; }
	if (n >= 2 && ((u[0] == 0xFF && u[1] == 0xFE) || (u[0] == 0xFE && u[1] == 0xFF)))
	{
		int k = 0;
		for (int i = u[0] == 0xFF ? 2 : 3; i < n; i += 2) b[k++] = b[i];	// LE: low byte first
		b[k] = '\0';
		return k;
	}
	return n;
}

#undef FS_FN
