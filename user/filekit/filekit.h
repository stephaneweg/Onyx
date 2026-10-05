//
// filekit.h -- FileKit: what programs do with files, one copy for the whole system
// (SD:/lib/filekit.so; the shared libraries: docs/03 section 5.6). A program links
// lib/filekit.imp.a and calls plain C functions (fk_*):
//
//   compression  zlib's deflate and inflate (raw, zlib or gzip wrapped), CRC-32, Adler-32
//   ZIP          an archive on the card read (its entries, one read to memory or extracted, all of
//                them) and made (files and folders of the card, buffers) -- the Archiver's engine:
//                ZIP64, old code pages, ZipCrypto passwords;
//                an archive IN MEMORY found into and built (the office formats, OpenRaster)
//   files        one loaded whole, saved; a file or a tree copied, moved, removed, measured; folders made
//   paths        the name, the extension, the folder of a path; two parts joined; a size for people
//
// Everything is integer and pointers: a program built without the FPU calls it. A buffer the library
// returns (void **out) is freed with fk_free, never with free / delete. The first version: 2026-10-05.
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
void fk_zip_close (fk_zip *z);
const char *fk_zip_error (fk_zip *z);					// the last failure's words
void fk_zip_password (fk_zip *z, const char *password);			// for its encrypted entries
int fk_zip_count (fk_zip *z);
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
fk_zipw *fk_zipw_create (const char *path);
int fk_zipw_add (fk_zipw *w, const char *disk_path, const char *name);	// a file, or a folder and all it holds, as `name` (0: its own)
int fk_zipw_add_data (fk_zipw *w, const char *name, const void *data, unsigned n);	// a buffer as an entry (copied)
void fk_zipw_level (fk_zipw *w, int level);				// 0 store, 1 fast, 6 (the default), 9
int fk_zipw_close (fk_zipw *w, fk_progress cb, void *user, char *err, int cap);	// written -> 0 / -1 (err); w freed

// ---- a ZIP archive in memory (a document read whole: .docx, .xlsx, .odt, .ora ...) --------------
int fk_zipmem_count (const void *zip, unsigned n);			// its entries, -1: not a ZIP
// Entry i: its name (cap bytes) and its size -> 1 / 0.
int fk_zipmem_entry (const void *zip, unsigned n, int i, char *name, int cap, unsigned *size);
// The entry of that name inflated (a 0 byte after its end) -> 0 and *out (fk_free), -1: none, damaged.
int fk_zipmem_get (const void *zip, unsigned n, const char *name, void **out, unsigned *out_n);
// ... and one built: entries added (level 0: stored -- the "mimetype" of an OpenDocument first),
// then the archive's bytes.
typedef struct fk_zipbuf fk_zipbuf;
fk_zipbuf *fk_zipbuf_new (void);
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
void fk_path_join (char *out, int cap, const char *folder, const char *name);
void fk_path_unique (char *path, int cap);				// a name not taken: "a.txt" -> "a (2).txt"
void fk_human_size (unsigned long long bytes, char *out, int cap);	// "12.4 KB"
void fk_dos_time_str (unsigned dos_time, char *out, int cap);		// "28/09/2026 14:12"

#ifdef __cplusplus
}
#endif

#endif
