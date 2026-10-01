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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/sha256.h>

#include "utils/log.h"
#include "javascript/quickjs/qjs_codecache.h"

#ifdef ONYX_HOST_SIM
#include <sys/stat.h>
#else
#include "kapi.h"
#endif

#ifndef ONYX_NS_DATAPATH
#define ONYX_NS_DATAPATH "/apps/netsurf.app/"
#endif
#define QJS_CC_DIR ONYX_NS_DATAPATH "jscache/"
#define QJS_CC_BUDGET (32 * 1024 * 1024)
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
	uint32_t size;			/* the file's */
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
	snprintf(path, cap, "%s%s", QJS_CC_DIR, leaf);
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
	FILE *f;
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
	f = fopen(path, "rb");
	if (f == NULL)
		return;
	cc.n = (int) fread(cc.e, sizeof cc.e[0], QJS_CC_ENTRIES, f);
	fclose(f);
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
	uint8_t *bc;
	FILE *f;
	int i;

	cc_load_index();
	if (cc.off || len < QJS_CC_MIN)
		return NULL;
	cc_sha(src, len, sha);
	i = cc_find(cc_key(sha));
	if (i < 0)
		return NULL;
	cc_file(path, sizeof path, cc_key(sha));
	f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	cc_build_id(build);
	bc = NULL;
	if (fread(&h, sizeof h, 1, f) == 1 && memcmp(h.magic, "ONYXJSC1", 8) == 0 &&
	    memcmp(h.build, build, sizeof build) == 0 &&
	    memcmp(h.src_sha, sha, 32) == 0 && h.src_len == len &&
	    h.bc_len > 0 && h.bc_len <= QJS_CC_MAX_FILE &&
	    (bc = malloc((size_t) h.bc_len)) != NULL) {
		if (fread(bc, 1, (size_t) h.bc_len, f) != h.bc_len ||
		    cc_checksum(bc, (size_t) h.bc_len) != h.bc_sum) {
			free(bc);
			bc = NULL;
		}
	}
	fclose(f);
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
	FILE *f = fopen(path, "wb");
	bool ok;

	if (f == NULL)
		return;
	ok = fwrite(p, 1, n, f) == n;
	ok = fclose(f) == 0 && ok;
	if (!ok)
		remove(path);
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
		remove(path);
	}
	cc_write_file(j->path, j->file, j->file_len);
	cc_path(path, sizeof path, "index");
	cc_write_file(path, j->index, j->index_n * sizeof(struct cc_entry));
}

/* the writer: the queue's jobs in order, until it is empty */
static int cc_writer(void *arg)
{
	(void) arg;
	for (;;) {
		struct cc_job *j;

		cc_lock();
		j = cc_queue;
		if (j == NULL) {
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
	cc_sha(src, len, sha);
	key = cc_key(sha);
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
		if (total <= QJS_CC_BUDGET || old < 0)
			break;
		j->evict[j->n_evict++] = cc.e[old].key;
		cc.e[old] = cc.e[--cc.n];
		if (i == cc.n)
			i = old;	/* (the new entry moved into the hole) */
	}
	memcpy(j->index, cc.e, cc.n * sizeof cc.e[0]);
	j->index_n = cc.n;

#ifdef ONYX_HOST_SIM
	if (!cc.dir_made) {
		mkdir(QJS_CC_DIR, 0755);
		cc.dir_made = true;
	}
	cc_write_job(j);		/* (the bench: written now) */
	cc_job_free(j);
#else
	if (!cc.dir_made) {
		char dir[256];
		size_t n = strlen(QJS_CC_DIR);
		memcpy(dir, QJS_CC_DIR, n - 1);		/* (without the last '/') */
		dir[n - 1] = '\0';
		kapi_mkdir(dir);			/* (-1 when it is there already) */
		cc.dir_made = true;
	}
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
