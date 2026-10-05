//
// filekit/arcpath.h -- paths on the card, for the archives: two parts joined, what exists, every folder
// of a path made, an archive's name made safe for the card, a name not taken, "is under". Included by
// the engine (ops.h) and by the programs that show archives over FileKit's C interface.
//
#ifndef _filekit_arcpath_h
#define _filekit_arcpath_h

#include "arcbase.h"

namespace arc {

// ---- paths --------------------------------------------------------------------------------------------
static inline void join (char *out, int cap, const char *a, const char *b)
{
	scopy (out, a, cap);
	int n = (int) strlen (out);
	if (n && out[n - 1] != '/' && out[n - 1] != ':' && b[0]) scat (out, "/", cap);
	scat (out, b, cap);
}
static inline bool path_exists (const char *p) { void *h = kapi_open (p); if (h) { kapi_close (h); return true; } void *d = kapi_opendir (p); if (d) { kapi_closedir (d); return true; } return false; }
static inline bool path_is_dir (const char *p) { void *d = kapi_opendir (p); if (d) { kapi_closedir (d); return true; } return false; }
// Every folder of `path` made (kapi_mkdir makes one level; -1 when it exists).
static inline void mkdirs (const char *path)
{
	char b[300]; scopy (b, path, sizeof b);
	int start = 0;
	for (int i = 0; b[i]; i++) if (b[i] == ':') { start = i + 1; break; }
	while (b[start] == '/') start++;
	for (int i = start; ; i++)
	{
		if (b[i] == '/' || b[i] == 0)
		{
			char c = b[i]; b[i] = 0;
			if (b[start]) kapi_mkdir (b);
			b[i] = c;
			if (!c) break;
		}
	}
}
// An archive name made a safe name on the card: no "..", no absolute path, no drive, the characters
// FAT refuses replaced by '_', no trailing dots or spaces in a part.
static inline void safe_rel (const char *name, char *out, int cap)
{
	int k = 0;
	const char *s = name;
	if (s[0] && s[1] == ':') s += 2;
	while (*s && k < cap - 1)
	{
		while (*s == '/') s++;
		const char *e = s; while (*e && *e != '/') e++;
		int len = (int) (e - s);
		bool dots = (len == 1 && s[0] == '.') || (len == 2 && s[0] == '.' && s[1] == '.');
		if (len && !dots)
		{
			if (k && k < cap - 1) out[k++] = '/';
			int st = k;
			for (int i = 0; i < len && k < cap - 1; i++)
			{
				unsigned char c = (unsigned char) s[i];
				out[k++] = (c < 32 || strchr ("<>:\"|?*\\", c)) ? '_' : (char) c;
			}
			while (k > st && (out[k - 1] == '.' || out[k - 1] == ' ')) k--;
			if (k == st) out[k++] = '_';
		}
		s = e;
	}
	out[k] = 0;
}
// "name.txt" -> "name (2).txt" ... the first that does not exist
static inline void unique_path (char *path, int cap)
{
	if (!path_exists (path)) return;
	char base[300], ext[40] = "";
	scopy (base, path, sizeof base);
	char *dot = strrchr (base, '.'), *sl = strrchr (base, '/');
	if (dot && (!sl || dot > sl) && dot != base) { scopy (ext, dot, sizeof ext); *dot = 0; }
	for (int i = 2; i < 1000; i++)
	{
		char num[12]; u64_str ((u64) i, num, sizeof num);
		scopy (path, base, cap); scat (path, " (", cap); scat (path, num, cap); scat (path, ")", cap); scat (path, ext, cap);
		if (!path_exists (path)) return;
	}
}

// ---- which entries: a folder selected is everything under it -----------------------------------------------
// prefix "kernel/sys" -> the entries "kernel/sys" and "kernel/sys/..."
static inline bool under (const char *name, const char *prefix)
{
	int n = (int) strlen (prefix);
	if (!n) return true;
	return !strncmp (name, prefix, (size_t) n) && (name[n] == 0 || name[n] == '/');
}

} // namespace arc

#endif
