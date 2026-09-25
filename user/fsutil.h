//
// fsutil.h -- file-system helpers shared by the file tools (File Viewer, trash, shelf):
// path join, exists / is_dir, recursive copy and delete, a free "name copy" name.
// Header-only, freestanding (kapi file calls + operator new/delete for the copy buffer).
//
#ifndef _fsutil_h
#define _fsutil_h
#include "kapi.h"

#define FS_NAMEL	72
#define FS_PATHL	300

static inline int  fs_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void fs_copy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static inline char fs_lower (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
static inline int  fs_ci_cmp (const char *a, const char *b)
{
	for (;; a++, b++) { char x = fs_lower (*a), y = fs_lower (*b); if (x != y || !x) return (unsigned char) x - (unsigned char) y; }
}

// out = dir + "/" + name (no doubled slash).
static inline void fs_join (char *out, int cap, const char *dir, const char *name)
{
	int p = 0;
	for (; dir[p] && p < cap - 1; p++) out[p] = dir[p];
	if (p > 0 && out[p - 1] != '/' && p < cap - 1) out[p++] = '/';
	for (int k = 0; name[k] && p < cap - 1; k++) out[p++] = name[k];
	out[p] = '\0';
}

// The last path component (points into `path`).
static inline const char *fs_basename (const char *path)
{
	const char *n = path;
	for (const char *p = path; *p; p++) if (*p == '/') n = p + 1;
	return n;
}

// Parent directory of path into out ("SD:/a/b" -> "SD:/a", "SD:/a" -> "SD:/").
static inline void fs_dirname (char *out, int cap, const char *path)
{
	fs_copy (out, path, cap);
	int n = fs_len (out);
	while (n > 0 && out[n - 1] != '/') n--;
	if (n > 0) n--;
	out[n] = '\0';
	if (fs_len (out) <= 3) fs_copy (out, "SD:/", cap);
}

static inline bool fs_is_dir (const char *path)
{
	void *d = kapi_opendir (path);
	if (d) { kapi_closedir (d); return true; }
	return false;
}

static inline bool fs_exists (const char *path)
{
	void *f = kapi_open (path);
	if (f) { kapi_close (f); return true; }
	return fs_is_dir (path);
}

static inline bool fs_copy_file (const char *src, const char *dst)
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
	bool ok = done == n && kapi_save_file (dst, buf, n) != 0;
	delete [] buf;
	return ok;
}

static inline bool fs_skip_dot (const char *n) { return n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')); }

// Copy a file or a whole folder tree (depth <= 12).
static inline bool fs_copy_tree (const char *src, const char *dst, int depth = 0)
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

// Delete a file or a whole folder tree (depth <= 12).
static inline bool fs_remove_tree (const char *path, int depth = 0)
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
	return kapi_remove (path) != 0;
}

// A free path in dir for `name`: "name", then "name copy", "name copy 2", ... (the
// extension stays at the end: "notes copy.txt").
static inline void fs_unique_name (char *out, int cap, const char *dir, const char *name, const char *tag = " copy")
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

#endif
