/*
 * onyx_io.h -- Onyx (docs/06 §32): Jet Browser's writes to the card, paced.
 *
 * On the Pi every app shares core 0, and the SD driver busy-waits while the card writes: a
 * large write freezes the desktop for 100-200 ms. The browser's writers (the disk cache's
 * thread, user/netsurf/onyx_cache.c; the JS code cache's, quickjs/qjs_codecache.c) therefore
 * write in small pieces with a real sleep between them, and wait -- within limits -- while
 * the user acts (a click, the wheel, a key) or a page loads. Implemented in onyx_cache.c.
 */
#ifndef ONYX_IO_H
#define ONYX_IO_H

#ifdef __cplusplus
extern "C" {
#endif

/** the size of a piece written at once, the sleep after it (ms) */
#define ONYX_IO_PIECE	(16 * 1024)
#define ONYX_IO_NAP	8

/** Onyx (docs/06 §33): a path on the kernel's RAM volume ("RAM:...")? Its writes are not paced
 * (onyx_io_save writes it at once) and its writers need not wait for a quiet moment. */
int onyx_io_is_ram(const char *path);

/** The UI thread: the user acted (a click, the wheel, a key). */
void onyx_io_activity(void);

/** The UI thread: a page's load began (1) or ended (0). */
void onyx_io_loading(int on);

/** A writer thread: waits while the user acts (the last second) or a page loads, max_ms at
 * most. */
void onyx_io_wait_quiet(unsigned max_ms);

/** A writer thread: the file path made of n bytes at p, paced (pieces of ONYX_IO_PIECE, a
 * sleep of ONYX_IO_NAP ms and a quiet wait between them) -> 0, or -1 (not written: the
 * file removed). NS_PERF: "ONYX-PERF io:write <path> <n>". */
int onyx_io_save(const char *path, const void *p, unsigned n);

/** The bytes written so far by onyx_io_save (NS_PERF's tally). */
unsigned long long onyx_io_written(void);

#ifdef __cplusplus
}
#endif

#endif
