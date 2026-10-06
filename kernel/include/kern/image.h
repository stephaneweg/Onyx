//
// image.h -- program images (kapi v77, docs/02 section 7 "Program images", docs/ELF-LOADER-PLAN.md).
//
// A program file is loaded ONCE: an image object (TImage) holds the frames of its read-only
// segments (R and R+X: 99.8 % of a large program) and a copy of the first bytes of its writable
// ones. The first process of a program streams the file from the card into the object (the
// headers, then each PT_LOAD segment straight into its frames: no whole-file buffer, sections
// that are not loaded never read); every process of that program maps those frames -- not owned
// by its address space, read-only or read + execute -- and gets private pages for the writable
// segments, filled from the object's copy (never from the file again).
//
// THE KEY IS THE PROGRAM'S FULL PATH, in its canonical form (ImageCanonPath: the one function
// that run, preload, unload, the query and the file layer's hook use). A run of a path that has
// an image maps it without looking at the card at all. So an image must lose its name where its
// file changes: the file kapis call ImageFileChanged (an unlink, a rename from or onto the path
// or of a folder above it, an open for writing or a create, the close of a written file), which
// does to the image what image_unload does -- the running processes keep it, no new process maps
// it, its frames are freed with its last reference, a pinned image is unpinned.
//
// An object lives while a process maps it or while it is pinned (a preload: kapi image_preload,
// /bin/preload). Images do not survive a restart.
//
// Everything here runs on core 0 in task context (the kernel is not preempted): no lock. The only
// places that let other tasks run are the reads of a load and the wait for another task's load.
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#ifndef _kern_image_h
#define _kern_image_h

#include <circle/types.h>

class CAddressSpace;
struct kapi_image_info;
struct TImage;

#define IMG_PATH_MAX		256		// a canonical path with its end (as kapi_image_info's)
#define IMG_CHUNK		0x20000		// bytes read between two yields (128 KB, as before v77)
#define IMG_WAIT_MS		5		// a start during another task's load looks again every 5 ms

// Where a program's bytes come from (the kernel: a FatFs file; LoadELF: a buffer; the host test).
struct TImgSource
{
	// nBytes from ulOffset into pBuffer -> the bytes read (fewer: the end of the file), < 0: an
	// error. It may yield (the SD driver does).
	int	(*pRead) (void *pCtx, u64 ulOffset, void *pBuffer, unsigned nBytes);
	void	 *pCtx;
	u64	  nSize;			// the file's size
};

// The canonical form of a program's path -- the image's key -- in pOut (IMG_PATH_MAX bytes):
//  - the volume first, always: pIn's ("SD:", "sd1:"...), else pCwd's (a path starting with '/' is
//    on pCwd's volume, any other is under pCwd -- as ResolvePath, sys/kapi.cpp); pCwd 0: "SD:/",
//    which is also what FatFs makes of a path without a volume (the kernel never changes FatFs's
//    current directory);
//  - only the spellings of ONE volume collapse: its name in any case, FatFs's numeric form ("0:"
//    is the first name of FF_VOLUME_STRS, "1:" the second...) and the kernel's "SD0:" (= "SD:") and "USB:" (= "USB1:").
//    "SD:" and "SD1:" are two partitions: two volumes, two keys;
//  - '/' between the names (FatFs takes '\' too: turned into '/'), no empty name, no "." or "..",
//    no '/' at the end (the root alone is "sd:/"), a name's trailing dots and spaces dropped (as
//    FatFs drops them);
//  - LOWER CASE, the volume included -- ASCII letters only: every other byte is kept as it is.
//    FAT folds accented letters too, so two spellings of one accented name are two keys here: the
//    file is then loaded twice, never the wrong file run (see docs/02 for what the hook misses).
// FALSE (pOut empty): no path, or too long.
boolean ImageCanonPath (const char *pIn, const char *pCwd, char *pOut);

// ImageOpen's flags
#define IMG_OPEN_PIN		1		// keep it (a preload): pinned from now on
#define IMG_OPEN_LIB		2		// (v83) a shared library (else: a program)
#define IMG_OPEN_ANY		4		// (v83) whichever the file is (a preload)

// How ImageOpen got the image (*pHow): for the start's log line
#define IMG_HOW_LOADED		1		// read from the file now
#define IMG_HOW_SHARED		2		// already in memory
#define IMG_HOW_WAITED		3		// being loaded by another task: waited for it

// The image of the program at pPath (canonicalised here with pCwd), a reference taken
// (ImageRelease). In memory already: that one (being loaded by another task: waits for it) -- the
// card is not touched. Else, pSrc 0: -KAPI_ENOENT, nothing done (the caller opens the file and
// calls again with it); pSrc: loaded from it now, the calling task reading, with a yield every
// IMG_CHUNK bytes. pPath 0: an image of its own, never shared (LoadELF's buffer).
// -> 0, or -KAPI_E* with *ppWhy a short reason for the log (nothing is left allocated).
int ImageOpen (const char *pPath, const char *pCwd, const TImgSource *pSrc, unsigned nFlags,
	       TImage **ppImage, unsigned *pHow, const char **ppWhy);

// The image mapped in pAS: its shared pages (not owned by pAS), private pages for the writable
// segments (owned, filled from the image's copy, the bss zero), the regions noted (kern/vm.h).
// pAS holds a reference of its own from now on (dropped by its destructor), also when this fails
// (FALSE: out of memory; the caller deletes pAS). No yield. The caller then synchronises the
// caches (SyncDataAndInstructionCache), as LoadELF always did.
boolean ImageMap (TImage *pImage, CAddressSpace *pAS, u64 *pEntry);

// (v83) A shared library (an image opened with IMG_OPEN_LIB) mapped in pAS at its place -- the same
// in every process: the kernel chose it in the library arena (kern/layout.h USER_LIB_BASE) when it
// loaded the file, and relocated the copy of its data once. *pTable: its export table there.
// -> 1 (mapped now: the caller synchronises the caches), 0 (pAS had it already: the same table),
// -KAPI_ENOMEM (pAS keeps its reference and what was mapped; the library is not usable in it),
// -KAPI_EMFILE (AS_LIB_MAX libraries in pAS), -KAPI_EINVAL (not a library). No yield.
int ImageMapLib (TImage *pImage, CAddressSpace *pAS, u64 *pTable);

// A library's place, the relocations applied at its load, its export table's version -> FALSE:
// not a library.
boolean ImageLibInfo (const TImage *pImage, u64 *pBase, unsigned *pRelocs, unsigned *pVersion);
// n bytes of a library as it is in memory (relocated), from ulOffset after its export table's start
// -> FALSE: not a library, or past its end. (AppKit's table, read by the kernel: kernel.cpp.)
boolean ImageLibTableRead (const TImage *pImage, u64 ulOffset, void *pOut, u64 n);

// A reference dropped. The last one frees the image's frames unless it is pinned. No yield, no
// I/O (the address space's teardown calls it).
void ImageRelease (TImage *pImage);

// The image of pPath loses its pin and its name at once: no new process maps it; its frames are
// freed when the last process running it ends (now if none). -> 0 / -KAPI_ENOENT (no image).
int ImageUnload (const char *pPath, const char *pCwd);

// The file layer's hook: the file (or folder) at pAbsPath -- an absolute path, as ResolvePath
// gives -- is being removed, renamed, replaced or written. The image of that path and the images
// of the programs under that folder lose their name as by ImageUnload. For "<folder>/app.txt"
// (where a program's stack size is) the image of "<folder>/main" stays, and forgets its stack
// size: its next start reads app.txt again. Cheap: nothing is done when no image exists; no I/O,
// no yield.
void ImageFileChanged (const char *pAbsPath);

// pPath 0: every live image, up to nCap written to pOut -> how many there are. pPath: the image
// a new process of that path would map -> 1 (pOut[0] filled if nCap > 0) / 0.
unsigned ImageList (const char *pPath, const char *pCwd, struct kapi_image_info *pOut, unsigned nCap);

// The 64 KB frames held by every image (kapi_meminfo counts them with the apps' pages).
unsigned ImagePagesTotal (void);

// What an image holds, for the start's log line: its shared bytes, its private (writable) bytes.
void ImageSizes (const TImage *pImage, u64 *pShared, u64 *pPrivate);

// The user stack's size its processes get (app.txt's "stack =", read once per image by the
// kernel's loader: a start from an image in memory reads nothing). 0: not set yet, or its
// app.txt changed since (ImageFileChanged).
unsigned ImageStack (const TImage *pImage);
void ImageSetStack (TImage *pImage, unsigned nStack);

#endif // _kern_image_h
