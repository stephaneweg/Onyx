//
// fileassoc.h -- file associations (SD:/etc/fileassoc.ini: "ext = app" lines).
//
//   fa_app_for (path, app, cap)  the app associated with path's extension (0 = none)
//   fa_open (path)               open it: a folder in the File Viewer, a .app bundle
//                                or an ELF program runs, a file in its associated app
//                                (SD:apps/<app>.app/main <path>) -- a NEW instance.
// Header-only, no allocation. Used by fileviewer (double-click) and shelf (click).
//
#ifndef _fileassoc_h
#define _fileassoc_h

#include "kapi.h"
#include "fsutil.h"

#define FA_INI		"SD:/etc/fileassoc.ini"

// Extension of path (after the last '.' of its basename), or "" if none.
static inline const char *fa_ext (const char *path)
{
	const char *b = fs_basename (path), *dot = 0;
	for (const char *p = b; *p; p++) if (*p == '.') dot = p;
	return dot != 0 && dot != b ? dot + 1 : "";
}

static inline bool fa_app_for (const char *path, char *app, int cap)
{
	const char *ext = fa_ext (path);
	if (ext[0] == '\0') return false;
	void *f = kapi_open (FA_INI);
	if (f == 0) return false;
	static char buf[2048];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return false;
	buf[n] = '\0';
	for (int i = 0; i < n; )
	{
		int ls = i;					// one line: [ls, le)
		while (i < n && buf[i] != '\n') i++;
		int le = i++;
		while (ls < le && (buf[ls] == ' ' || buf[ls] == '\t')) ls++;
		if (ls >= le || buf[ls] == '#' || buf[ls] == ';') continue;
		int eq = ls; while (eq < le && buf[eq] != '=') eq++;
		if (eq >= le) continue;
		int ke = eq; while (ke > ls && (buf[ke - 1] == ' ' || buf[ke - 1] == '\t')) ke--;
		int k = 0; bool same = true;			// key == ext (case-insensitive)?
		for (; ls + k < ke && ext[k]; k++) if (fs_lower (buf[ls + k]) != fs_lower (ext[k])) { same = false; break; }
		if (!same || ls + k != ke || ext[k] != '\0') continue;
		int vs = eq + 1; while (vs < le && (buf[vs] == ' ' || buf[vs] == '\t')) vs++;
		int ve = le; while (ve > vs && (buf[ve - 1] == ' ' || buf[ve - 1] == '\t' || buf[ve - 1] == '\r')) ve--;
		if (ve <= vs) return false;
		int j = 0; for (; vs + j < ve && j < cap - 1; j++) app[j] = buf[vs + j];
		app[j] = '\0';
		return true;
	}
	return false;
}

static inline bool fa_is_program (const char *path)
{
	void *f = kapi_open (path);
	if (f == 0) return false;
	unsigned char m[4] = { 0, 0, 0, 0 };
	int n = kapi_read (f, m, sizeof m);
	kapi_close (f);
	return n == 4 && m[0] == 0x7F && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
}

static inline bool fa_open (const char *path)
{
	char exe[300], app[48];
	// A remote path (FTP:... via ftpfs): a file with an association opens right away
	// (no folder test first -- some servers LIST a file too), and it is never opened
	// just to test for an ELF (that would download it whole).
	bool remote = !(fs_lower (path[0]) == 's' && fs_lower (path[1]) == 'd' && path[2] == ':');
	bool hasApp = fa_app_for (path, app, sizeof app);
	if (remote && hasApp)
	{
		int p = 0;
		const char *pre = "SD:apps/", *suf = ".app/main";
		for (int i = 0; pre[i] && p < (int) sizeof exe - 1; i++) exe[p++] = pre[i];
		for (int i = 0; app[i] && p < (int) sizeof exe - 1; i++) exe[p++] = app[i];
		for (int i = 0; suf[i] && p < (int) sizeof exe - 1; i++) exe[p++] = suf[i];
		exe[p] = '\0';
		return kapi_exec (exe, path) != 0;
	}
	if (fs_is_dir (path))
	{
		const char *e = fa_ext (path);
		if (fs_lower (e[0]) == 'a' && fs_lower (e[1]) == 'p' && fs_lower (e[2]) == 'p' && e[3] == '\0')
		{
			fs_join (exe, sizeof exe, path, "main");		// an app bundle: run it
			return kapi_exec (exe, "") != 0;
		}
		return kapi_exec ("SD:apps/fileviewer.app/main", path) != 0;	// a folder
	}
	if (hasApp)
	{
		int p = 0;
		const char *pre = "SD:apps/";
		for (int i = 0; pre[i] && p < (int) sizeof exe - 1; i++) exe[p++] = pre[i];
		for (int i = 0; app[i] && p < (int) sizeof exe - 1; i++) exe[p++] = app[i];
		const char *suf = ".app/main";
		for (int i = 0; suf[i] && p < (int) sizeof exe - 1; i++) exe[p++] = suf[i];
		exe[p] = '\0';
		return kapi_exec (exe, path) != 0;
	}
	if (!remote && fa_is_program (path)) return kapi_exec (path, "") != 0;
	return false;
}

#endif
