//
// fileassoc.h -- file associations (SD:/etc/fileassoc.ini: "ext = app" lines).
//
//   fa_app_for (path, app, cap)  the app associated with path's extension (0 = none)
//   fa_open (path)               open it: a folder in the File Viewer, a .app bundle
//                                or an ELF program runs, a file in its associated app
//                                (SD:apps/<app>.app/main <path>) -- a NEW instance.
//
#ifndef _fileassoc_h
#define _fileassoc_h
#include "appkit/appkit.h"
#include "sk_api.h"
#include "filekit/fsutil.h"


#define FA_INI		"SD:/etc/fileassoc.ini"
// Extension of path (after the last '.' of its basename), or "" if none.
SK_API const char *fa_ext (const char *path);
SK_API bool fa_app_for (const char *path, char *app, int cap);
SK_API bool fa_is_program (const char *path);
SK_API bool fa_open (const char *path);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "fileassoc.inc"
#endif

#endif
