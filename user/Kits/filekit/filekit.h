//
// filekit.h -- FileKit: what programs do with files, one copy for the whole system
// (SD:/lib/filekit.so; the shared libraries: docs/03 section 5.6). A program links
// lib/filekit.imp.a and calls plain C functions (fk_*):
//
//   compression  zlib's deflate and inflate (raw, zlib or gzip wrapped), CRC-32, Adler-32
//   archives     the formats known asked (fk_arc_formats), an archive of any of them opened, extracted
//                with choices, changed (add, delete, rename) -- ZIP read and written, tar / tar.gz /
//                gzip read
//   ZIP          an archive on the card read (its entries, one read to memory or extracted, all of
//                them) and made (files and folders of the card, buffers) -- the Archiver's engine:
//                ZIP64, old code pages, ZipCrypto passwords;
//                an archive IN MEMORY found into and built (the office formats, OpenRaster)
//   files        one loaded whole, saved; a file or a tree copied, moved, removed, measured; folders made
//   paths        the name, the extension, the folder of a path; two parts joined; a size for people
//   key / value  a text of [section] headers and "key = value" lines read, changed, written back (kvtext.h)
//
// Everything is integer and pointers: a program built without the FPU calls it. A buffer the library
// returns (void **out) is freed with fk_free, never with free / delete. The first version: 2026-10-05;
// the second (the same day): archives of any format, asked from the library (fk_arc_*), tar and gzip read.
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
#ifndef _filekit_h
#define _filekit_h

#ifdef __cplusplus
extern "C" {
#endif

void fk_free (void *p);				// a buffer the library returned

// ---- compression (zlib) -----------------------------------------------------------------------
#define FK_RAW		0			// the deflate stream alone (a ZIP entry's)
#define FK_ZLIB		1			// with zlib's header and Adler-32 (PNG, PDF)
#define FK_GZIP		2			// with gzip's header and CRC-32 (.gz, HTTP)
#define FK_AUTO		3			// (inflate only) zlib or gzip, told from the first bytes

unsigned fk_crc32 (unsigned crc, const void *data, unsigned n);		// (crc: 0 at first, then the last result)
unsigned fk_adler32 (unsigned adler, const void *data, unsigned n);	// (adler: 1 at first)
// The whole of `data` compressed (level 0..9, -1: the default 6) -> 0 and *out (fk_free) / *out_n, -1.
int fk_deflate (const void *data, unsigned n, int wrap, int level, void **out, unsigned *out_n);
// ... and back. size_hint: the size expected (0: not known; the buffer grows) -> 0 / -1 (damaged).
// The result has a 0 byte after its end (a text can be used as it is).
int fk_inflate (const void *data, unsigned n, int wrap, unsigned size_hint, void **out, unsigned *out_n);

// ---- a ZIP archive on the card ----------------------------------------------------------------
struct fk_zip_entry
{
	char		   name[300];		// its path in the archive (UTF-8, '/' between the parts)
	unsigned long long size, packed;	// the original size, what it takes in the archive
	unsigned	   crc, dos_time;	// CRC-32; the DOS date and time (fk_dos_time_str)
	int		   dir, encrypted;	// a folder; needs the password
	int		   method;		// 0 stored, 8 deflate...
	int		   reserved[4];
};
typedef struct fk_zip fk_zip;
// A step of a long work: `done` of `total` bytes, the file being worked on (0: the same) -> 0 to
// go on, anything else to stop.
typedef int (*fk_progress) (void *user, unsigned long long done, unsigned long long total, const char *name);

fk_zip *fk_zip_open (const char *path, char *err, int cap);		// 0: err says why
void fk_zip_close (fk_zip *z);	// the archive closed, its handle freed (0: nothing)
const char *fk_zip_error (fk_zip *z);					// the last failure's words
void fk_zip_password (fk_zip *z, const char *password);			// for its encrypted entries
int fk_zip_count (fk_zip *z);	// how many entries it holds (0: no archive)
int fk_zip_entry (fk_zip *z, int i, struct fk_zip_entry *out);		// 1 / 0
int fk_zip_find (fk_zip *z, const char *name);				// its index (letters' case ignored), -1
int fk_zip_read (fk_zip *z, int i, void **out, unsigned *out_n);	// entry i in memory (fk_free) -> 0 / -1
int fk_zip_extract (fk_zip *z, int i, const char *dest_path);		// entry i as that file -> 0 / -1
// Everything under `prefix` ("" or 0: the whole archive) into the folder dest_dir, the folders made,
// the names made safe for the card -> the files written, -1 (fk_zip_error; stopped: what was done stays).
int fk_zip_extract_all (fk_zip *z, const char *prefix, const char *dest_dir, fk_progress cb, void *user);

// A new archive (an existing file is replaced when it is closed): its entries said first, written
// by fk_zipw_close.
typedef struct fk_zipw fk_zipw;
fk_zipw *fk_zipw_create (const char *path);	// nothing written yet; 0: no path
int fk_zipw_add (fk_zipw *w, const char *disk_path, const char *name);	// a file, or a folder and all it holds, as `name` (0: its own)
int fk_zipw_add_data (fk_zipw *w, const char *name, const void *data, unsigned n);	// a buffer as an entry (copied)
void fk_zipw_level (fk_zipw *w, int level);				// 0 store, 1 fast, 6 (the default), 9
int fk_zipw_close (fk_zipw *w, fk_progress cb, void *user, char *err, int cap);	// written -> 0 / -1 (err); w freed

// ---- archives of any format the library knows (version 2, 2026-10-05) --------------------------
// The formats are asked, not assumed: a program that shows archives (the Archiver) lists them from
// here, so a format added to the library is one it has at once.
struct fk_format
{
	char name[16];				// "ZIP", "TAR", "TAR.GZ", "GZIP"
	char extensions[96];			// "zip jar docx ..." (lower case, a space between)
	char note[80];				// what can be done with it, in a few words
	int  can_read, can_write;		// opened and extracted; made and changed (add, delete, rename)
	int  can_password;			// encrypted entries are read (with fk_arc_password)
	int  reserved[4];
};
int fk_arc_formats (struct fk_format *out, int max);			// how many there are (out: up to max)
// What the file at `path` is: 1 and its format's name -- or 0 and `why` ("7z archives are not supported yet.").
int fk_arc_probe (const char *path, char *format, int fcap, char *why, int wcap);
int fk_arc_is_name (const char *name);					// 1: its extension is a format's that is read

// An archive opened: the same handle as fk_zip (fk_zip_count, _entry, _find, _read, _extract,
// _extract_all, _password, _error and _close work on it whatever its format).
typedef struct fk_zip fk_arc;
fk_arc *fk_arc_open (const char *path, char *err, int cap);		// any format read; 0: err says why
fk_arc *fk_arc_new (const char *path, const char *format);		// a new one, not on the card yet ("ZIP"; 0: not a format written)
const char *fk_arc_format (fk_arc *a);					// "ZIP" ...
const char *fk_arc_path (fk_arc *a);	// the archive file's path ("": no archive)
const char *fk_arc_comment (fk_arc *a);					// the archive's own ("": none)
int fk_arc_writable (fk_arc *a);					// 1: it can be changed
int fk_arc_method_name (fk_arc *a, int i, char *out, int cap);		// entry i: "Deflate", "Store, crypted"...
int fk_arc_test (fk_arc *a, int i, fk_progress cb, void *user);	// entry i read and checked, nothing written -> 0 / -1

// Extracting with choices.
#define FK_LAYOUT_FULL		0		// the archive's folders kept
#define FK_LAYOUT_FROM		1		// ... from the folder `from` down (what is above it dropped)
#define FK_LAYOUT_FLAT		2		// the files alone, no folder
#define FK_EXISTS_ASK		0		// a file that exists: ask (the callback), replace, skip, keep both
#define FK_EXISTS_REPLACE	1
#define FK_EXISTS_SKIP		2
#define FK_EXISTS_KEEP_BOTH	3
#define FK_ANSWER_REPLACE	1		// the callback's answers
#define FK_ANSWER_SKIP		2
#define FK_ANSWER_KEEP_BOTH	3
#define FK_ANSWER_REPLACE_ALL	4
#define FK_ANSWER_SKIP_ALL	5
#define FK_ANSWER_CANCEL	(-1)
struct fk_extract
{
	unsigned     size;			// sizeof (struct fk_extract)
	const char  *dest;			// the folder to extract into (made)
	const char  *selected;			// one byte an entry, not 0: taken (0: every entry)
	const char  *from;			// FK_LAYOUT_FROM: the archive's folder shown
	int	     layout, exists;
	int	   (*ask) (void *user, const char *path);	// FK_EXISTS_ASK -> an FK_ANSWER_*
	fk_progress  progress;
	void	    *user;			// (both callbacks')
	int	     files, skipped;		// out: what was done
};
int fk_arc_extract_with (fk_arc *a, struct fk_extract *x);		// 0 / -1 (fk_zip_error)
unsigned long long fk_arc_extract_bytes (fk_arc *a, const char *selected);	// what that selection weighs

// Changing an archive that can be (fk_arc_writable): it is rewritten beside itself, then swapped in,
// and its entries are read again (the indexes change). -> 0 / -1 (fk_zip_error).
// paths: files and folders of the card, put in the archive's folder `into` ("": its top);
// keep_folders: a folder keeps its name and tree; replace: a name that exists is replaced, else skipped.
int fk_arc_add (fk_arc *a, const char *const *paths, int n, const char *into, int keep_folders, int replace, int level,
		fk_progress cb, void *user, int *added);
int fk_arc_add_conflicts (fk_arc *a, const char *const *paths, int n, const char *into, int keep_folders);	// names that exist already
unsigned long long fk_arc_add_bytes (fk_arc *a, const char *const *paths, int n, const char *into, int keep_folders, int replace);	// the work's size
int fk_arc_delete (fk_arc *a, const char *selected, fk_progress cb, void *user);
int fk_arc_rename (fk_arc *a, const char *from, const char *to, fk_progress cb, void *user);	// a file, or a folder and what is under it
int fk_arc_new_folder (fk_arc *a, const char *name, fk_progress cb, void *user);
int fk_arc_write_empty (fk_arc *a);					// a new archive put on the card with nothing in it

// ---- a ZIP archive in memory (a document read whole: .docx, .xlsx, .odt, .ora ...) --------------
int fk_zipmem_count (const void *zip, unsigned n);			// its entries, -1: not a ZIP
// Entry i: its name (cap bytes) and its size -> 1 / 0.
int fk_zipmem_entry (const void *zip, unsigned n, int i, char *name, int cap, unsigned *size);
// The entry of that name inflated (a 0 byte after its end) -> 0 and *out (fk_free), -1: none, damaged.
int fk_zipmem_get (const void *zip, unsigned n, const char *name, void **out, unsigned *out_n);
// ... and one built: entries added (level 0: stored -- the "mimetype" of an OpenDocument first),
// then the archive's bytes.
typedef struct fk_zipbuf fk_zipbuf;
fk_zipbuf *fk_zipbuf_new (void);	// an empty one; 0: no memory
int fk_zipbuf_add (fk_zipbuf *b, const char *name, const void *data, unsigned n, int level);	// 0 / -1
int fk_zipbuf_finish (fk_zipbuf *b, void **out, unsigned *out_n);	// the archive (fk_free) -> 0 / -1; b freed
void fk_zipbuf_free (fk_zipbuf *b);					// (given up before finish)

// ---- files ------------------------------------------------------------------------------------
int fk_exists (const char *path);					// 1 a file, 2 a folder, 0
long long fk_file_size (const char *path);				// -1: no such file
int fk_load (const char *path, void **out, unsigned *out_n);		// the whole file (a 0 byte after it; fk_free) -> 0 / -1
int fk_save (const char *path, const void *data, unsigned n);		// 0 / -1
int fk_mkdirs (const char *path);					// every folder of the path made -> 0 / -1
// A file, or a folder and all it holds. `dst` is the new path itself (not the folder to put it in).
int fk_copy (const char *src, const char *dst, fk_progress cb, void *user);	// 0 / -1
int fk_move (const char *src, const char *dst, fk_progress cb, void *user);	// renamed, else copied then removed
int fk_remove (const char *path);					// a file, or a folder and all it holds -> 0 / -1
// The bytes under a path (a file's size; a folder's whole tree); *files, *folders: how many (0: not wanted).
long long fk_tree_size (const char *path, int *files, int *folders);

// ---- paths ------------------------------------------------------------------------------------
const char *fk_path_name (const char *path);				// after the last '/' or ':' (inside path)
void fk_path_ext (const char *path, char *out, int cap);		// "png" (lower case, without the dot; "": none)
void fk_path_folder (const char *path, char *out, int cap);		// "SD:/a/b.txt" -> "SD:/a"; "SD:/a" -> "SD:/"
void fk_path_join (char *out, int cap, const char *folder, const char *name);	// folder + "/" + name (no '/' added after a '/' or a ':', or before an empty name), cut to cap
void fk_path_unique (char *path, int cap);				// a name not taken: "a.txt" -> "a (2).txt"
void fk_human_size (unsigned long long bytes, char *out, int cap);	// "12.4 KB"
void fk_dos_time_str (unsigned dos_time, char *out, int cap);		// "28/09/2026 14:12"

#ifdef __cplusplus
}
#endif

// (the file helpers -- fs_join, fs_exists, fs_copy_tree...: fsutil.h, C++)
#ifdef __cplusplus
#include "fsutil.h"
#endif

// (a text document of [section] headers and "key = value" lines, read and written back -- fk_kv_*: kvtext.h, C and
//  C++)
#include "kvtext.h"

#endif
