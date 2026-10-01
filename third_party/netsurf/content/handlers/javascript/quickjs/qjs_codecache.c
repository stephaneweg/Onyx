/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: the code cache -- the bytecode of the scripts kept on the card, as a browser keeps
 * its compiled code: the next load of a script reads it instead of parsing the source again
 * (Google's 1 MB script: seconds of parsing on the Pi).
 *
 * <data>/jscache/<16 hex digits>.bc, one file a source: a header (the engine's build --
 * JS_GetBuildId: a new build writes its own --, the source's SHA-256 and length, the
 * bytecode's length and checksum) then JS_WriteObject's bytecode (JS_WRITE_OBJ_BYTECODE).
 * A file is used only when all of that matches: the same source text, byte for byte, gives
 * the same code wherever it came from (a page's script, an inline one, a prelude).
 * <data>/jscache/index: the files, their sizes and when last used; past QJS_CC_BUDGET the
 * least recently used ones are removed. The files are written by a thread of their own
 * (the Pi's card is slow), in the order they were stored (up to QJS_CC_QUEUED bytes waiting;
 * past that a store is skipped: kept next time). NS_JSCACHE=0 (the PC bench) turns the cache
 * off.
 *
 * Onyx (docs/06 §32): a source's bytecode is written only the second time the source is seen
 * (as V8 does): the first time its hash goes into the index (an entry of size 0), the next
 * time -- this launch or a later one -- its file is written. Google's big scripts, different at
 * every visit, are never written. The writer paces its writes (user/netsurf/onyx_io.h: 16 KB
 * pieces, a sleep between them, a wait while the user acts or a page loads) and writes the
 * index once, a few seconds after the last store, not after each file.
 *
 * Onyx (docs/06 §33): the cache is in RAM by default -- RAM:/jet/jscache/, the kernel's RAM
 * volume, lost at a restart; Choices' cache_on_card:1 puts it back on the card (<data>/jscache/,
 * as above). In RAM a source's bytecode is written the first time it is compiled, at once (no
 * pacing, no quiet wait: no card), the index as soon as the writer is done; the budget is
 * 32 MB or a quarter of the RAM volume. The files are read and written through the kapi (the
 * kernel's open / read / save_file, which reach RAM: and the card alike; the PC bench's stand-in
 * maps RAM: to a folder).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/sha256.h>

#include "utils/log.h"
#include "javascript/quickjs/qjs_codecache.h"

#include "netsurf/onyx_perf.h"
#include "kapi.h"
#ifndef ONYX_HOST_SIM
#include "netsurf/onyx_io.h"
#endif

#ifndef ONYX_NS_DATAPATH
#define ONYX_NS_DATAPATH "/apps/jet.app/"
#endif
#define QJS_CC_DIR ONYX_NS_DATAPATH "jscache/"
#define QJS_CC_BUDGET (32 * 1024 * 1024)
/* Onyx: the folder (qjs_cc_set_dir: RAM:/jet/jscache/ by default, gui.c), its budget, in RAM? */
static char cc_dir[256] = QJS_CC_DIR;
static size_t cc_budget = QJS_CC_BUDGET;
static bool cc_ram;
#define QJS_CC_MAX_FILE (8 * 1024 * 1024)
#define QJS_CC_ENTRIES 512
#define QJS_CC_QUEUED (16 * 1024 * 1024)

struct cc_head {
	char magic[8];			/* "ONYXJSC1" */
	char build[56];			/* JS_GetBuildId() */
	uint8_t src_sha[32];
	uint64_t src_len;
	uint64_t bc_len;
	uint64_t bc_sum;
};

struct cc_entry {
	uint64_t key;			/* the SHA-256's first 8 bytes (the file's name) */
	uint32_t size;			/* the file's (Onyx: 0 -- seen once, no file yet) */
	uint32_t stamp;			/* last used (a counter) */
};

static struct {
	bool loaded, off, dir_made;
	int n;
	uint32_t clock;
	struct cc_entry e[QJS_CC_ENTRIES];
} cc;

/* the stores waiting for the writer (its thread runs while there are some) */
static struct cc_job *cc_queue, **cc_queue_tail = &cc_queue;
static size_t cc_queued;
static bool cc_writing;
static int cc_lock_word;
/* Onyx: the index changed since it was last written (a source seen), and when (ms) */
static bool cc_index_dirty;
static uint64_t cc_index_changed;
static unsigned long long cc_written;	/* (NS_PERF: the bytes this launch wrote) */

static void cc_lock(void)
{
	while (__atomic_exchange_n(&cc_lock_word, 1, __ATOMIC_ACQUIRE))
		;
}

static void cc_unlock(void)
{
	__atomic_store_n(&cc_lock_word, 0, __ATOMIC_RELEASE);
}

static uint64_t cc_checksum(const uint8_t *p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	size_t i;

	for (i = 0; i + 8 <= n; i += 8) {
		uint64_t w;
		memcpy(&w, p + i, 8);
		h = (h ^ w) * 0x100000001b3ull;
		h ^= h >> 29;
	}
	for (; i < n; i++)
		h = (h ^ p[i]) * 0x100000001b3ull;
	return h;
}

static uint64_t cc_key(const uint8_t sha[32])
{
	uint64_t k;
	memcpy(&k, sha, 8);
	return k;
}

static void cc_path(char *path, size_t cap, const char *leaf)
{
	snprintf(path, cap, "%s%s", cc_dir, leaf);
}

/* exported interface documented in qjs_codecache.h */
void qjs_cc_set_dir(const char *dir, size_t budget)
{
	size_t n;

	if (dir == NULL || dir[0] == '\0')
		return;
	snprintf(cc_dir, sizeof cc_dir, "%s", dir);
	n = strlen(cc_dir);
	if (n > 0 && cc_dir[n - 1] != '/' && n + 1 < sizeof cc_dir) {
		cc_dir[n] = '/';
		cc_dir[n + 1] = '\0';
	}
	cc_ram = strncmp(cc_dir, "RAM:", 4) == 0 || strncmp(cc_dir, "ram:", 4) == 0;
	cc_budget = budget > 0 ? budget : QJS_CC_BUDGET;
}

/* a whole file (kapi: RAM: or the card) -> malloc'd bytes, or NULL */
static uint8_t *cc_read(const char *path, size_t *len)
{
	void *f = kapi_open(path);
	unsigned n, got = 0;
	uint8_t *b;

	if (f == NULL)
		return NULL;
	n = kapi_fsize(f);
	b = malloc(n > 0 ? n : 1);
	while (b != NULL && got < n) {
		int r = kapi_read(f, b + got, n - got);
		if (r <= 0)
			break;
		got += (unsigned) r;
	}
	kapi_close(f);
	if (b != NULL && got != n) {
		free(b);
		b = NULL;
	}
	*len = n;
	return b;
}

static void cc_file(char *path, size_t cap, uint64_t key)
{
	char leaf[24];
	snprintf(leaf, sizeof leaf, "%016llx.bc", (unsigned long long) key);
	cc_path(path, cap, leaf);
}

/* the index read once (its clock: past the newest stamp) */
static void cc_load_index(void)
{
	char path[256];
	uint8_t *b;
	size_t len = 0;
	int i;

	if (cc.loaded)
		return;
	cc.loaded = true;
#ifdef ONYX_HOST_SIM
	{
		const char *e = getenv("NS_JSCACHE");
		if (e != NULL && e[0] == '0')
			cc.off = true;
	}
#endif
	cc_path(path, sizeof path, "index");
	b = cc_read(path, &len);
	if (b == NULL)
		return;
	cc.n = (int) (len / sizeof cc.e[0]);
	if (cc.n > QJS_CC_ENTRIES)
		cc.n = QJS_CC_ENTRIES;
	memcpy(cc.e, b, (size_t) cc.n * sizeof cc.e[0]);
	free(b);
	for (i = 0; i < cc.n; i++)
		if (cc.e[i].stamp >= cc.clock)
			cc.clock = cc.e[i].stamp + 1;
}

static int cc_find(uint64_t key)
{
	int i;
	for (i = 0; i < cc.n; i++)
		if (cc.e[i].key == key)
			return i;
	return -1;
}

static void cc_sha(const char *src, size_t len, uint8_t sha[32])
{
	mbedtls_sha256((const unsigned char *) src, len, sha, 0);
}

static void cc_build_id(char build[56])
{
	memset(build, 0, 56);
	strncpy(build, JS_GetBuildId(), 55);
}

/* exported interface documented in qjs_codecache.h */
uint8_t *qjs_cc_load(const char *src, size_t len, size_t *bclen)
{
	struct cc_head h;
	uint8_t sha[32];
	char path[256], build[56];
	uint8_t *bc, *file;
	size_t flen = 0;
	int i;

	cc_load_index();
	if (cc.off || len < QJS_CC_MIN)
		return NULL;
	cc_sha(src, len, sha);
	i = cc_find(cc_key(sha));
	if (i < 0 || cc.e[i].size == 0)
		return NULL;		/* (Onyx: size 0 -- seen once, no file) */
	cc_file(path, sizeof path, cc_key(sha));
	file = cc_read(path, &flen);	/* (Onyx: the kapi's -- RAM: or the card) */
	if (file == NULL)
		return NULL;
	cc_build_id(build);
	bc = NULL;
	if (flen >= sizeof h) {
		memcpy(&h, file, sizeof h);
		if (memcmp(h.magic, "ONYXJSC1", 8) == 0 &&
		    memcmp(h.build, build, sizeof build) == 0 &&
		    memcmp(h.src_sha, sha, 32) == 0 && h.src_len == len &&
		    h.bc_len > 0 && h.bc_len <= QJS_CC_MAX_FILE &&
		    h.bc_len == flen - sizeof h &&
		    cc_checksum(file + sizeof h, (size_t) h.bc_len) == h.bc_sum &&
		    (bc = malloc((size_t) h.bc_len)) != NULL)
			memcpy(bc, file + sizeof h, (size_t) h.bc_len);
	}
	free(file);
	if (bc == NULL)
		return NULL;
	cc.e[i].stamp = cc.clock++;	/* (written with the next store) */
	*bclen = (size_t) h.bc_len;
	return bc;
}

/* a store's work for the writer: the file, the files evicted, the index */
struct cc_job {
	struct cc_job *next;
	char path[256];
	uint8_t *file;
	size_t file_len;
	int n_evict;
	uint64_t evict[QJS_CC_ENTRIES];
	struct cc_entry *index;
	int index_n;
};

static void cc_write_file(const char *path, const void *p, size_t n)
{
#ifdef ONYX_HOST_SIM
	/* (the bench: the kapi stand-in's save_file -- RAM: a folder of its own, SIM_RAM) */
	if (kapi_save_file(path, p, (unsigned) n) != (int) n) {
		kapi_remove(path);
		return;
	}
	cc_written += n;
	if (onyx_perf_on())
		fprintf(stderr, "ONYX-PERF io:write %s %lu\n", path, (unsigned long) n);
#else
	/* Onyx: paced, in pieces (the SD driver busy-waits while the card writes); RAM: at
	 * once (onyx_io_save knows) */
	if (onyx_io_save(path, p, (unsigned) n) == 0)
		cc_written += n;
#endif
}

static void cc_job_free(struct cc_job *j)
{
	free(j->file);
	free(j->index);
	free(j);
}

static void cc_write_job(struct cc_job *j)
{
	char path[256];
	int i;

	for (i = 0; i < j->n_evict; i++) {
		cc_file(path, sizeof path, j->evict[i]);
		kapi_remove(path);
	}
	if (j->file != NULL) {
#ifndef ONYX_HOST_SIM
		if (!cc_ram)	/* (the card: not while the user acts, a page loads) */
			onyx_io_wait_quiet(30000);
#endif
		cc_write_file(j->path, j->file, j->file_len);
	}
}

/* Onyx: the index as it is now, written (the writer's, once its jobs are done) */
static void cc_write_index(void)
{
	char path[256];
	struct cc_entry *copy = malloc(sizeof cc.e);
	int n;

	if (copy == NULL)
		return;
	cc_lock();
	n = cc.n;
	memcpy(copy, cc.e, (size_t) n * sizeof cc.e[0]);
	cc_index_dirty = false;
	cc_unlock();
	cc_path(path, sizeof path, "index");
	cc_write_file(path, copy, (size_t) n * sizeof(struct cc_entry));
	free(copy);
}

static uint64_t cc_now_ms(void)
{
#ifdef ONYX_HOST_SIM
	return 0;
#else
	return (uint64_t) kapi_get_ticks() * 10;
#endif
}

/* the writer: the queue's jobs in order, until it is empty -- then (Onyx) the index, once
 * no store came for 3 s */
static int cc_writer(void *arg)
{
	(void) arg;
	for (;;) {
		struct cc_job *j;

		cc_lock();
		j = cc_queue;
		if (j == NULL) {
			bool dirty = cc_index_dirty;
			uint64_t since = cc_now_ms() - cc_index_changed;
			/* (Onyx: RAM: the index at once) */
			if (dirty && since < 3000 && !cc_ram) {
				cc_unlock();
#ifndef ONYX_HOST_SIM
				kapi_msleep(250);
#endif
				continue;
			}
			if (dirty) {
				cc_unlock();
#ifndef ONYX_HOST_SIM
				if (!cc_ram)
					onyx_io_wait_quiet(30000);
#endif
				cc_write_index();
				continue;
			}
			cc_writing = false;
			cc_unlock();
			return 0;
		}
		cc_queue = j->next;
		if (cc_queue == NULL)
			cc_queue_tail = &cc_queue;
		cc_queued -= j->file_len;
		cc_unlock();
		cc_write_job(j);
		cc_job_free(j);
	}
}

static void cc_make_dir(void)
{
	if (cc.dir_made)
		return;
	cc.dir_made = true;
	{
		/* the folder and its parents ("RAM:/jet/jscache/": RAM:/jet first; -1 when one is
		 * there already) */
		char dir[256];
		size_t n = strlen(cc_dir), i;
		memcpy(dir, cc_dir, n + 1);
		for (i = 1; i < n; i++)
			if (dir[i] == '/' && dir[i - 1] != ':' && dir[i - 1] != '/') {
				dir[i] = '\0';
				kapi_mkdir(dir);
				dir[i] = '/';
			}
	}
}

/* a job to the writer (a file, files evicted; or nothing: the index alone) -- the index
 * written after it */
static void cc_queue_job(struct cc_job *j)
{
	cc_make_dir();
	cc_lock();
	cc_index_dirty = true;
	cc_index_changed = cc_now_ms();
	cc_unlock();
#ifdef ONYX_HOST_SIM
	/* (the bench: written now, the index with each file -- as the Pi's writer does
	 * after a burst) */
	cc_write_job(j);
	cc_job_free(j);
	cc_write_index();
#else
	{
		bool start;
		cc_lock();
		*cc_queue_tail = j;
		cc_queue_tail = &j->next;
		cc_queued += j->file_len;
		start = !cc_writing;
		cc_writing = true;
		cc_unlock();
		if (start && kapi_thread_create(cc_writer, NULL, 0, "jscache") < 0)
			cc_writer(NULL);	/* (no thread: written now) */
	}
#endif
}

/* Onyx: a source seen for the first time -- an entry of size 0 (the least recently used
 * entry out when the index is full), the index written a while later */
static void cc_seen(uint64_t key)
{
	struct cc_job *j = calloc(1, sizeof *j);
	int i;

	if (j == NULL)
		return;
	if (cc.n == QJS_CC_ENTRIES) {
		int old = 0, k;
		for (k = 1; k < cc.n; k++)
			if (cc.e[k].stamp < cc.e[old].stamp)
				old = k;
		if (cc.e[old].size > 0)
			j->evict[j->n_evict++] = cc.e[old].key;
		cc_lock();
		cc.e[old] = cc.e[--cc.n];
		cc_unlock();
	}
	cc_lock();
	i = cc.n++;
	cc.e[i].key = key;
	cc.e[i].size = 0;
	cc.e[i].stamp = cc.clock++;
	cc_unlock();
	cc_queue_job(j);
}

/* exported interface documented in qjs_codecache.h */
void qjs_cc_store(const char *src, size_t len, const uint8_t *bc, size_t bclen)
{
	struct cc_head *h;
	struct cc_job *j;
	uint8_t sha[32];
	uint64_t key, total;
	int i;

	cc_load_index();
	if (cc.off || len < QJS_CC_MIN || bclen == 0 ||
	    bclen + sizeof *h > QJS_CC_MAX_FILE)
		return;
	cc_sha(src, len, sha);
	key = cc_key(sha);
	if (cc_find(key) < 0 && !cc_ram) {
		/* Onyx (docs/06 §32): seen for the first time -- only noted (an entry of
		 * size 0, in the index); its bytecode is written when it is seen again */
		cc_seen(key);
		return;
	}
	cc_lock();
	i = cc_queued + bclen > QJS_CC_QUEUED;
	cc_unlock();
	if (i)
		return;			/* (too much waiting: this one next time) */
	j = calloc(1, sizeof *j);
	if (j == NULL)
		goto fail;
	j->file_len = sizeof *h + bclen;
	j->file = malloc(j->file_len);
	j->index = malloc(sizeof cc.e);
	if (j->file == NULL || j->index == NULL)
		goto fail;
	h = (struct cc_head *) j->file;
	memset(h, 0, sizeof *h);
	memcpy(h->magic, "ONYXJSC1", 8);
	cc_build_id(h->build);
	memcpy(h->src_sha, sha, 32);
	h->src_len = len;
	h->bc_len = bclen;
	h->bc_sum = cc_checksum(bc, bclen);
	memcpy(j->file + sizeof *h, bc, bclen);
	cc_file(j->path, sizeof j->path, key);

	/* the index: this entry, then the least recently used out past the budget */
	cc_lock();
	i = cc_find(key);
	if (i < 0) {
		if (cc.n == QJS_CC_ENTRIES) {	/* (full: the oldest goes) */
			int old = 0, k;
			for (k = 1; k < cc.n; k++)
				if (cc.e[k].stamp < cc.e[old].stamp)
					old = k;
			j->evict[j->n_evict++] = cc.e[old].key;
			cc.e[old] = cc.e[--cc.n];
		}
		i = cc.n++;
	}
	cc.e[i].key = key;
	cc.e[i].size = (uint32_t) j->file_len;
	cc.e[i].stamp = cc.clock++;
	for (;;) {
		int old = -1, k;
		total = 0;
		for (k = 0; k < cc.n; k++) {
			total += cc.e[k].size;
			if (k != i && (old < 0 || cc.e[k].stamp < cc.e[old].stamp))
				old = k;
		}
		if (total <= cc_budget || old < 0)
			break;
		if (cc.e[old].size > 0)
			j->evict[j->n_evict++] = cc.e[old].key;
		cc.e[old] = cc.e[--cc.n];
		if (i == cc.n)
			i = old;	/* (the new entry moved into the hole) */
	}
	cc_unlock();
	memcpy(j->index, cc.e, cc.n * sizeof cc.e[0]);
	j->index_n = cc.n;
	cc_queue_job(j);
	return;
fail:
	if (j != NULL)
		cc_job_free(j);
}

/* exported interface documented in qjs_codecache.h */
JSValue qjs_cc_compile(JSContext *ctx, const char *src, size_t len, const char *name,
		bool strip)
{
	JSValue obj;
	size_t bclen;
	uint8_t *bc;

	if (len >= QJS_CC_MIN && (bc = qjs_cc_load(src, len, &bclen)) != NULL) {
		obj = JS_ReadObject(ctx, bc, bclen, JS_READ_OBJ_BYTECODE);
		free(bc);
		if (!JS_IsException(obj))
			return obj;
		JS_FreeValue(ctx, JS_GetException(ctx));	/* (unreadable: compiled again) */
	}
	obj = JS_Eval(ctx, src, len, name, JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
	if (!JS_IsException(obj) && len >= QJS_CC_MIN && !cc.off) {
		uint8_t *b = JS_WriteObject(ctx, &bclen, obj, JS_WRITE_OBJ_BYTECODE |
				(strip ? JS_WRITE_OBJ_STRIP_SOURCE : 0));
		if (b != NULL) {
			qjs_cc_store(src, len, b, bclen);
			js_free(ctx, b);
		}
	}
	return obj;
}
