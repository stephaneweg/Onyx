//
// fsutil.h -- file-system helpers shared by the file tools (File Viewer, trash, dock):
// path join, exists / is_dir, recursive copy and delete, a free "name copy" name.
// FileKit's (filekit.so): this header declares, fsutil.inc has the code (it was user/fsutil.h, a header
// of inline functions, until 2026-10-05). C++.
//
#ifndef _fsutil_h
#define _fsutil_h
#include "appkit/appkit.h"

// (a program for Onyx: FileKit's functions, by name. A PC build, or FS_INLINE: the code inline in the
//  program -- no shared library there.)
#if defined (__aarch64__) && !defined (FS_INLINE)
#define FS_API	extern "C"
#else
#define FS_API	static inline
#endif

#define FS_NAMEL	72
#define FS_PATHL	300
FS_API int fs_len (const char *s);	// a string's length in bytes (0 for a null pointer)
FS_API void fs_copy (char *d, const char *s, int cap);	// s into d, cut to cap - 1 bytes, always ended by a 0
FS_API char fs_lower (char c);	// an ASCII capital as its small letter, anything else unchanged
FS_API int fs_ci_cmp (const char *a, const char *b);	// strcmp with ASCII letters' case ignored: 0 equal, else < 0 or > 0
// out = dir + "/" + name (no doubled slash).
FS_API void fs_join (char *out, int cap, const char *dir, const char *name);
// The last path component (points into `path`).
FS_API const char *fs_basename (const char *path);
// Parent directory of path into out ("SD:/a/b" -> "SD:/a", "SD:/a" -> "SD:/").
FS_API void fs_dirname (char *out, int cap, const char *path);
FS_API bool fs_is_dir (const char *path);
FS_API bool fs_exists (const char *path);
FS_API bool fs_copy_file (const char *src, const char *dst);
FS_API bool fs_skip_dot (const char *n);
// Copy a file or a whole folder tree (depth <= 12).
FS_API bool fs_copy_tree (const char *src, const char *dst, int depth = 0);
// Delete a file or a whole folder tree (depth <= 12).
FS_API bool fs_remove_tree (const char *path, int depth = 0);
// A free path in dir for `name`: "name", then "name copy", "name copy 2", ... (the
// extension stays at the end: "notes copy.txt").
FS_API void fs_unique_name (char *out, int cap, const char *dir, const char *name, const char *tag = " copy");
// A text file saved by a Windows editor: drop a UTF-8 BOM, turn UTF-16 (Notepad) into
// 8-bit. In place (b[n] must be writable); returns the new length.
FS_API int fs_text_fix (char *b, int n);

#if (!defined (__aarch64__) || defined (FS_INLINE)) && !defined (FS_IMPL)
#include "fsutil.inc"
#endif

#endif
