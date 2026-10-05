//
// trash.h -- the Onyx trash (freedesktop-style layout on the SD card):
//   SD:/.Trash/files/<name>             the trashed file or folder
//   SD:/.Trash/info/<name>.trashinfo    "Path=<original path>"
// trash_move (path) moves an item there (a clash gets " (2)"-style names); trash_restore
// (name) puts it back at its original path (a free variant if that path is taken now);
// trash_purge (name) deletes one item for good; trash_empty () deletes everything.
// /bin/rm still deletes for real -- the trash is for interactive deletes (File Viewer).
//
#ifndef _trash_h
#define _trash_h
#include "appkit/appkit.h"
#include "sk_api.h"
#include "filekit/fsutil.h"

#define TRASH_DIR	"SD:/.Trash"
#define TRASH_FILES	"SD:/.Trash/files"
#define TRASH_INFO	"SD:/.Trash/info"
SK_API void trash_ensure (void);	// make the trash's folders (TRASH_DIR, TRASH_FILES, TRASH_INFO) if they are not there
SK_API void trash_info_path_ (char *out, int cap, const char *name);
// Move `path` to the trash. Returns true on success.
SK_API bool trash_move (const char *path);
// Original path of trashed item `name` (0 if unknown).
SK_API bool trash_origin (const char *name, char *out, int cap);
// Restore trashed item `name` to its original folder (recreated if needed). Returns
// true on success; `where` (optional) receives the restored path.
SK_API bool trash_restore (const char *name, char *where = 0, int wcap = 0);
// Delete trashed item `name` for good.
SK_API bool trash_purge (const char *name);
// Number of items in the trash.
SK_API int trash_count (void);
// Empty the trash (everything in files/ and info/).
SK_API int trash_empty (void);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "trash.inc"
#endif

#endif
