/*
 * onyx_cache.c -- Onyx: NetSurf's disk cache on the card (the low-level cache's backing
 * store: content/backing_store.h's gui_llcache_table, registered by the framebuffer frontend).
 *
 * llcache writes an object here once it is complete and worth keeping -- fresh for a while
 * (Cache-Control max-age, Expires) or revalidatable (an ETag, a Last-Modified: Onyx change
 * in llcache.c) -- and reads it back when a page asks for its URL again, in this launch or a
 * later one: a fresh object needs no request at all, a stale one is revalidated
 * (If-None-Match / If-Modified-Since; a 304 keeps the stored bytes). Each object is two
 * files in the cache folder (SD:/apps/jet.app/cache/ by default): <id>.d its data and
 * <id>.m its metadata (llcache's serialisation: the URL, the headers, the times); "index"
 * lists them ("id data-size meta-size last-use url"), read at the start, written after the
 * writes and at the end. The size is bounded (Choices' disc_cache_size, 64 MB by default on
 * Onyx): past it the least recently used objects go.
 *
 * The files are written by a thread of its own (kernel v67): llcache's store call only
 * queues the object (the UI thread does not wait for the card); reads are made at once (the
 * page waits for them anyway). The file calls are the kapi's (kapi_save_file, kapi_file_out /
 * kapi_stream_write, kapi_open / kapi_read), not newlib's (its descriptor table is not the
 * threads'). Memory: the object's bytes are shared with llcache (the store takes a reference;
 * llcache's release drops it), as the upstream fs_backing_store does.
 *
 * Onyx (docs/06 §32): on the Pi the SD driver busy-waits while the card writes (core 0, which
 * every app shares, frozen 100-200 ms at a time), so this writes less and gently:
 *  - an object is stored only the second time it is seen (its URL's hash met in an earlier
 *    launch -- the hashes seen are kept in the index, "- <hash>" lines): most of what a page
 *    fetches is never asked for again (a Google page's scripts change at every visit);
 *  - nor a body over OC_MAX_OBJECT (512 KB);
 *  - the files are written paced (onyx_io_save: 16 KB pieces, a sleep between them), waiting
 *    while the user acts or a page loads (onyx_io_wait_quiet);
 *  - the index is written once a minute at most, and at the end (files a lost index left are
 *    removed at the next launch, OC_SWEEP).
 * Also implemented here: the pacing (onyx_io.h), the JS code cache's too.
 *
 * Onyx (docs/06 §33): the cache is in RAM by default -- RAM:/jet/cache, the kernel's RAM volume
 * (kern/ramfs.h): nothing of the pages visited reaches the card, nothing freezes core 0, and it
 * is all gone when the Pi restarts. Choices' cache_on_card:1 (gui.c) puts it back on the card.
 * In RAM: an object is stored the first time it is seen (up to OC_MAX_OBJECT_RAM), written at
 * once (no pacing, no waiting for a quiet moment), the index a few seconds after a change; the
 * size is Choices' disc_cache_size but at most half the RAM volume.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "utils/useragent.h"	/* (the "Desktop Site" key) */
#include "content/backing_store.h"
#include "netsurf/onyx_perf.h"

#include "kapi.h"
#include "onyx_io.h"

#define OC_ELEM_DATA	0
#define OC_ELEM_META	1

struct oc_elem {
	uint8_t *data;		/* the bytes in memory (shared with llcache), or NULL */
	size_t len;		/* their size (on the card too) */
	int ref;		/* references to data: llcache's, the writer's */
	bool on_card;		/* written */
	bool queued;		/* waiting for the writer */
};

struct oc_entry {
	char *url;
	uint32_t hash;
	unsigned id;
	unsigned used;		/* the use clock at its last store / fetch (LRU) */
	bool dead;		/* invalidated or evicted: gone once its references are */
	struct oc_elem e[2];
	struct oc_entry *next;	/* the hash chain */
};

#define OC_HASH 1024
#define OC_QUEUE 1024
#define OC_MAX_OBJECT	(512 * 1024)	/* Onyx: a larger body is not stored */
#define OC_MAX_OBJECT_RAM (2 * 1024 * 1024)	/* Onyx: in RAM: */
#define OC_INDEX_TICKS_RAM 200		/* Onyx: in RAM:, 2 s after a change */
#define OC_SEEN_MAX	4096		/* Onyx: the URLs' hashes remembered (seen once) */
#define OC_INDEX_TICKS	6000		/* Onyx: the index written once a minute at most */
#define OC_SWEEP_TICKS	3000		/* Onyx: 30 s after the start, the orphans removed */

static struct {
	bool up;
	char dir[256];
	size_t limit, hysteresis, total;
	unsigned next_id, clock, count;
	struct oc_entry *hash[OC_HASH];
	volatile int lk;
	/* the writer's queue: entries' elements to write, files to delete */
	struct { struct oc_entry *en; int elem; unsigned del_id; } q[OC_QUEUE];
	unsigned qhead, qtail;
	volatile unsigned wake;
	volatile int stop, running;
	bool threads;
	bool ram;		/* Onyx: in RAM: (docs/06 §33) */
	bool index_dirty;
	unsigned hits, misses, written;
	/* Onyx: the URLs seen (their keys' hashes), a ring: those loaded from the index
	 * (seen in an earlier launch: stored now) and this launch's (stored the next time) */
	uint32_t seen[OC_SEEN_MAX];
	uint8_t seen_old[OC_SEEN_MAX];
	unsigned nseen, seen_at;
	unsigned first_new_id;	/* (ids below it: the index's -- the sweep's) */
	unsigned declined, too_big;
} oc;

/* ---- Onyx (docs/06 §32): the writes paced (onyx_io.h) --------------------------------------- */

static volatile unsigned io_input_at;	/* ticks of the user's last input */
static volatile int io_loading;		/* a page loading */
static volatile int io_stopping;	/* Jet Browser closing: no more waiting */
static volatile unsigned io_loading_at;	/* since (ticks) */
static unsigned long long io_written;

void onyx_io_activity(void)
{
	io_input_at = kapi_get_ticks();
}

void onyx_io_loading(int on)
{
	io_loading = on;
	io_loading_at = kapi_get_ticks();
}

int onyx_io_is_ram(const char *path)
{
	return path != NULL && (path[0] == 'R' || path[0] == 'r') &&
		(path[1] == 'A' || path[1] == 'a') && (path[2] == 'M' || path[2] == 'm') &&
		path[3] == ':';
}

void onyx_io_wait_quiet(unsigned max_ms)
{
#ifndef ONYX_HOST_SIM
	unsigned waited = 0;

	for (;;) {
		unsigned now = kapi_get_ticks();
		/* (a load that never ends -- a page streaming: 60 s at most) */
		bool loading = io_loading && now - io_loading_at < 6000;
		if ((!loading && now - io_input_at >= 100) || waited >= max_ms || io_stopping)
			return;
		kapi_msleep(100);
		waited += 100;
	}
#else
	(void) max_ms;		/* (the bench writes at once) */
#endif
}

unsigned long long onyx_io_written(void)
{
	return io_written;
}

int onyx_io_save(const char *path, const void *p, unsigned n)
{
	const uint8_t *b = p;
	int ok;

#ifdef ONYX_HOST_SIM
	/* (the bench has no kapi_file_out: it writes at once; its save_file answers as the
	   kernel's, the bytes written or -1) */
	ok = kapi_save_file(path, p, n) == (int) n;
#else
	if (n <= ONYX_IO_PIECE || onyx_io_is_ram(path)) {
		/* (RAM: at once -- no card, a copy into the kernel's pages) */
		/* (kapi_save_file: the bytes written, -1) */
		ok = kapi_save_file(path, p, n) == (int) n;
	} else {
		void *h = kapi_file_out(path, 0);
		unsigned off = 0;
		ok = h != NULL;
		while (ok && off < n) {
			unsigned k = n - off > ONYX_IO_PIECE ? ONYX_IO_PIECE : n - off;
			ok = kapi_stream_write(h, b + off, k) == (int) k;
			off += k;
			if (ok && off < n) {
				/* (the card's other users, the desktop, the input: their turn) */
				kapi_msleep(ONYX_IO_NAP);
				onyx_io_wait_quiet(2000);
			}
		}
		if (h != NULL)
			kapi_stream_close(h);
	}
#endif
	if (!ok) {
		kapi_remove(path);
		return -1;
	}
	io_written += n;
	if (onyx_perf_on())
		fprintf(stderr, "ONYX-PERF io:write %s %u\n", path, n);
	return 0;
}

/* ---- the URLs seen ---------------------------------------------------------------------------- */

static uint32_t oc_key_hash(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (uint8_t) *s) * 16777619u;
	return h != 0 ? h : 1;
}

/* (oc.lk held) -1 not seen, 0 seen in this launch, 1 seen in an earlier one */
static int oc_seen(uint32_t h)
{
	for (unsigned i = 0; i < oc.nseen; i++)
		if (oc.seen[i] == h)
			return oc.seen_old[i];
	return -1;
}

/* (oc.lk held) */
static void oc_seen_add(uint32_t h, bool old)
{
	unsigned i = oc.nseen < OC_SEEN_MAX ? oc.nseen++ : oc.seen_at++ % OC_SEEN_MAX;
	oc.seen[i] = h;
	oc.seen_old[i] = old;
}

static void oc_path(char *buf, size_t cap, unsigned id, int elem)
{
	snprintf(buf, cap, "%s/%08x.%c", oc.dir, id, elem == OC_ELEM_META ? 'm' : 'd');
}

/* A whole file read (kapi): malloc'd bytes, or NULL */
static uint8_t *oc_read(const char *path, size_t *len)
{
	void *f = kapi_open(path);
	unsigned n, got = 0;
	uint8_t *b;

	if (f == NULL)
		return NULL;
	n = kapi_fsize(f);
	b = malloc(n > 0 ? n : 1);
	if (b != NULL) {
		while (got < n) {
			int r = kapi_read(f, b + got, n - got);
			if (r <= 0)
				break;
			got += (unsigned) r;
		}
		if (got != n) {
			free(b);
			b = NULL;
		}
	}
	kapi_close(f);
	*len = n;
	return b;
}

/* Onyx: the entry's key -- its URL, "D|" before it when the URL's site is shown in its
 * desktop version ("Desktop Site", utils/useragent.c): the two versions of a page are
 * different responses (a Vary: User-Agent), a site switched back found the other's copy */
static const char *oc_key(nsurl *url, char *buf, size_t cap)
{
	lwc_string *h = nsurl_get_component(url, NSURL_HOST);
	bool d = h != NULL && user_agent_is_desktop(lwc_string_data(h));

	if (h != NULL)
		lwc_string_unref(h);
	if (!d)
		return nsurl_access(url);
	snprintf(buf, cap, "D|%s", nsurl_access(url));
	return buf;
}

static struct oc_entry *oc_find(nsurl *url)
{
	uint32_t h = nsurl_hash(url);
	struct oc_entry *e;
	char kb[4096];
	const char *s = oc_key(url, kb, sizeof kb);

	for (e = oc.hash[h % OC_HASH]; e != NULL; e = e->next)
		if (e->hash == h && !e->dead && strcmp(e->url, s) == 0)
			return e;
	return NULL;
}

/* (oc.lk held) an element's reference dropped: its bytes freed with the last one */
static void oc_unref(struct oc_elem *el)
{
	if (el->ref > 0 && --el->ref == 0) {
		free(el->data);
		el->data = NULL;
	}
}

/* (oc.lk held) an entry out of the hash, freed if nothing refers to it any more */
static void oc_reap(struct oc_entry *e)
{
	struct oc_entry **p;

	if (!e->dead || e->e[0].ref > 0 || e->e[1].ref > 0 || e->e[0].queued || e->e[1].queued)
		return;
	for (p = &oc.hash[e->hash % OC_HASH]; *p != NULL; p = &(*p)->next)
		if (*p == e) {
			*p = e->next;
			break;
		}
	free(e->url);
	free(e);
}

/* (oc.lk held) queue a write (elem >= 0) or the deletion of an id's files (en NULL) */
static bool oc_queue(struct oc_entry *en, int elem, unsigned del_id)
{
	unsigned n = (oc.qtail + 1) % OC_QUEUE;
	if (n == oc.qhead)
		return false;			/* (full: not written, or the files left) */
	oc.q[oc.qtail].en = en;
	oc.q[oc.qtail].elem = elem;
	oc.q[oc.qtail].del_id = del_id;
	oc.qtail = n;
	oc.wake++;
	kapi_wake_word(&oc.wake);
	return true;
}

/* (oc.lk held) an entry dropped: its files deleted, its memory once llcache let it go */
static void oc_kill(struct oc_entry *e)
{
	if (e->dead)
		return;
	e->dead = true;
	oc.total -= e->e[0].len + e->e[1].len;
	oc.count--;
	if (e->e[0].on_card || e->e[1].on_card || e->e[0].queued || e->e[1].queued)
		oc_queue(NULL, -1, e->id);
	oc.index_dirty = true;
	oc_reap(e);
}

/* (oc.lk held) the least recently used objects dropped until the size is under the limit */
static void oc_evict(void)
{
	if (oc.total <= oc.limit)
		return;
	while (oc.total > oc.limit - oc.hysteresis && oc.count > 0) {
		struct oc_entry *old = NULL, *e;
		int i;
		for (i = 0; i < OC_HASH; i++)
			for (e = oc.hash[i]; e != NULL; e = e->next)
				if (!e->dead && (old == NULL || oc.clock - e->used > oc.clock - old->used))
					old = e;
		if (old == NULL)
			break;
		oc_kill(old);
	}
}

/* The index written (the writer thread, or the end) */
static void oc_write_index(void)
{
	char path[300], *buf = NULL;
	size_t len = 0, cap = 0;
	int i;
	struct oc_entry *e;

	kapi_lock(&oc.lk);
	oc.index_dirty = false;
	/* Onyx: the URLs seen (stored when seen again in a later launch) */
	for (unsigned k = 0; k < oc.nseen; k++) {
		if (len + 16 > cap) {
			char *nb = realloc(buf, cap = cap * 2 + 4096);
			if (nb == NULL)
				break;
			buf = nb;
		}
		len += (size_t) snprintf(buf + len, cap - len, "- %08x\n", (unsigned) oc.seen[k]);
	}
	for (i = 0; i < OC_HASH; i++)
		for (e = oc.hash[i]; e != NULL; e = e->next) {
			size_t need;
			if (e->dead || !e->e[0].on_card || !e->e[1].on_card)
				continue;
			need = len + strlen(e->url) + 64;
			if (need > cap) {
				char *nb = realloc(buf, cap = need * 2 + 4096);
				if (nb == NULL)
					break;
				buf = nb;
			}
			len += (size_t) snprintf(buf + len, cap - len, "%x %lu %lu %u %s\n", e->id,
					(unsigned long) e->e[0].len, (unsigned long) e->e[1].len,
					e->used, e->url);
		}
	kapi_unlock(&oc.lk);
	snprintf(path, sizeof path, "%s/index", oc.dir);
	onyx_io_save(path, buf != NULL ? buf : "", (unsigned) len);
	free(buf);
}

/* The writer: the queued elements to the card, the dropped ones' files deleted */
static void oc_write_some(void)
{
	for (;;) {
		struct oc_entry *en;
		int elem;
		unsigned del_id;
		char path[300];

		kapi_lock(&oc.lk);
		if (oc.qhead == oc.qtail) {
			kapi_unlock(&oc.lk);
			return;
		}
		en = oc.q[oc.qhead].en;
		elem = oc.q[oc.qhead].elem;
		del_id = oc.q[oc.qhead].del_id;
		oc.qhead = (oc.qhead + 1) % OC_QUEUE;
		kapi_unlock(&oc.lk);

		if (en == NULL) {
			oc_path(path, sizeof path, del_id, OC_ELEM_DATA);
			kapi_remove(path);
			oc_path(path, sizeof path, del_id, OC_ELEM_META);
			kapi_remove(path);
			continue;
		}
		/* (the element's bytes are held: its reference taken at the queueing) */
		oc_path(path, sizeof path, en->id, elem);
		if (!oc.ram)	/* (Onyx: the card -- not while the user acts, a page loads) */
			onyx_io_wait_quiet(30000);
		{
			/* (Onyx: paced -- and kapi_save_file answers the bytes written: the
			 * old "== 0" took every write for a failure, nothing was ever read
			 * back from the card in a later launch) */
			bool ok = onyx_io_save(path, en->e[elem].data, (unsigned) en->e[elem].len) == 0;
			kapi_lock(&oc.lk);
			en->e[elem].queued = false;
			en->e[elem].on_card = ok;
			if (ok) {
				oc.written += (unsigned) en->e[elem].len;
				oc.index_dirty = true;
			}
			oc_unref(&en->e[elem]);
			if (en->dead)
				oc_reap(en);
			kapi_unlock(&oc.lk);
		}
	}
}

/* Onyx: the files of the cache folder no entry of the index has (written after the last index
 * a launch wrote: the Pi switched off), removed -- once, a while after the start */
static void oc_sweep(void)
{
	void *d = kapi_opendir(oc.dir);
	struct kapi_dirent de;
	unsigned removed = 0;

	if (d == NULL)
		return;
	while (kapi_readdir(d, &de) > 0 && !oc.stop) {
		unsigned id;
		char kind, path[300];
		bool known = false;
		int i;
		struct oc_entry *e;

		if (sscanf(de.name, "%8x.%c", &id, &kind) != 2 || (kind != 'd' && kind != 'm') ||
		    id >= oc.first_new_id)
			continue;
		kapi_lock(&oc.lk);
		for (i = 0; i < OC_HASH && !known; i++)
			for (e = oc.hash[i]; e != NULL; e = e->next)
				if (e->id == id) {
					known = true;
					break;
				}
		kapi_unlock(&oc.lk);
		if (known)
			continue;
		if (!oc.ram)
			onyx_io_wait_quiet(30000);
		snprintf(path, sizeof path, "%s/%s", oc.dir, de.name);
		kapi_remove(path);
		removed++;
	}
	kapi_closedir(d);
	if (removed > 0 && onyx_perf_on())
		fprintf(stderr, "ONYX-PERF cache:sweep %u orphan files removed\n", removed);
}

static int oc_thread(void *arg)
{
	unsigned start = kapi_get_ticks(), last_index = start;
	bool swept = false;

	(void) arg;
	while (!oc.stop) {
		unsigned seen = oc.wake;
		oc_write_some();
		/* the index: a minute after the last time at most (Onyx: was 3 s -- a
		 * Google page's visit wrote it again and again) */
		if (oc.index_dirty && kapi_get_ticks() - last_index >
				(oc.ram ? OC_INDEX_TICKS_RAM : OC_INDEX_TICKS)) {
			if (!oc.ram)
				onyx_io_wait_quiet(30000);
			oc_write_index();
			last_index = kapi_get_ticks();
		}
		if (!swept && kapi_get_ticks() - start > OC_SWEEP_TICKS) {
			swept = true;
			oc_sweep();
		}
		if (kapi_wait_word(&oc.wake, seen, 1000) < 0)
			kapi_msleep(50);
	}
	oc_write_some();
	oc.running = 0;
	return 0;
}

static void oc_load_index(void)
{
	char path[300];
	size_t len = 0;
	uint8_t *b;
	char *p, *end;

	snprintf(path, sizeof path, "%s/index", oc.dir);
	b = oc_read(path, &len);
	if (b == NULL)
		return;
	p = (char *) b;
	end = p + len;
	while (p < end) {
		char *nl = memchr(p, '\n', (size_t) (end - p)), *url;
		unsigned id, used;
		unsigned long dl, ml;
		int off = 0;
		nsurl *u;
		if (nl == NULL)
			break;
		*nl = '\0';
		if (p[0] == '-' && p[1] == ' ') {	/* (Onyx: a URL seen) */
			unsigned h;
			if (sscanf(p + 2, "%x", &h) == 1)
				oc_seen_add(h, true);
			p = nl + 1;
			continue;
		}
		if (sscanf(p, "%x %lu %lu %u %n", &id, &dl, &ml, &used, &off) == 4 && off > 0 &&
		    nsurl_create(p + off + (strncmp(p + off, "D|", 2) == 0 ? 2 : 0), &u) ==
				NSERROR_OK) {
			struct oc_entry *e = calloc(1, sizeof *e);
			url = strdup(p + off);	/* (its key: "D|" kept -- oc_key) */
			if (e != NULL && url != NULL) {
				e->url = url;
				e->hash = nsurl_hash(u);
				e->id = id;
				e->used = used;
				e->e[0].len = dl;
				e->e[1].len = ml;
				e->e[0].on_card = e->e[1].on_card = true;
				e->next = oc.hash[e->hash % OC_HASH];
				oc.hash[e->hash % OC_HASH] = e;
				oc.total += dl + ml;
				oc.count++;
				if (id >= oc.next_id)
					oc.next_id = id + 1;
				if (used > oc.clock)
					oc.clock = used;
			} else {
				free(e);
				free(url);
			}
			nsurl_unref(u);
		}
		p = nl + 1;
	}
	free(b);
}

static nserror oc_initialise(const struct llcache_store_parameters *params)
{
	const char *dir = params != NULL && params->path != NULL ? params->path :
		ONYX_NS_DATAPATH "cache";
	size_t n;

	if (oc.up)
		return NSERROR_OK;
	memset(&oc, 0, sizeof oc);
	snprintf(oc.dir, sizeof oc.dir, "%s", dir);
	n = strlen(oc.dir);
	while (n > 1 && oc.dir[n - 1] == '/')
		oc.dir[--n] = '\0';
	oc.ram = onyx_io_is_ram(oc.dir);
	{
		/* (the folder and its parents: "RAM:/jet/cache" -- RAM:/jet first) */
		size_t i;
		for (i = 1; i < n; i++)
			if (oc.dir[i] == '/' && oc.dir[i - 1] != ':' && oc.dir[i - 1] != '/') {
				oc.dir[i] = '\0';
				kapi_mkdir(oc.dir);
				oc.dir[i] = '/';
			}
	}
	kapi_mkdir(oc.dir);
	oc.limit = params != NULL && params->limit != 0 ? params->limit : 64u << 20;
	if (oc.ram) {
		/* (Onyx: at most half the RAM volume -- the code cache, the others, share it) */
		struct kapi_vol_info vi;
		if (kapi_vol_info(oc.dir, &vi) == 0 && vi.total > 0 && oc.limit > vi.total / 2)
			oc.limit = (size_t) (vi.total / 2);
	}
	oc.hysteresis = params != NULL ? params->hysteresis : oc.limit / 5;
	if (oc.hysteresis >= oc.limit)
		oc.hysteresis = oc.limit / 5;
	oc.next_id = 1;
	oc_load_index();
	oc.first_new_id = oc.next_id;
	kapi_lock(&oc.lk);
	oc_evict();
	kapi_unlock(&oc.lk);
	oc.threads = KT->version >= 67 && KT->thread_create != 0;
	if (oc.threads) {
		oc.running = 1;
		if (kapi_thread_create(oc_thread, NULL, 0, "cache") < 0) {
			oc.running = 0;
			oc.threads = false;
		}
	}
	oc.up = true;
	if (onyx_perf_on())
		fprintf(stderr, "ONYX-PERF cache:open %s%s: %u objects, %lu KB (limit %lu KB)\n",
				oc.dir, oc.ram ? " (RAM)" : "", oc.count, (unsigned long) (oc.total / 1024),
				(unsigned long) (oc.limit / 1024));
	return NSERROR_OK;
}

static nserror oc_finalise(void)
{
	unsigned t0;

	if (!oc.up)
		return NSERROR_OK;
	oc.up = false;		/* (no store from now on) */
	io_stopping = 1;	/* (Onyx: the writes left, at once) */
	if (oc.threads) {
		oc.stop = 1;
		oc.wake++;
		kapi_wake_word(&oc.wake);
		t0 = kapi_get_ticks();
		while (oc.running && kapi_get_ticks() - t0 < 300)	/* (3 s at most) */
			kapi_msleep(10);
	} else {
		oc_write_some();
	}
	oc_write_index();
	if (onyx_perf_on())
		fprintf(stderr, "ONYX-PERF cache:close %u objects, %lu KB; %u hits, %u misses, "
				"%u KB written; %u not stored (seen once), %u too big; the card: "
				"%llu KB written\n", oc.count, (unsigned long) (oc.total / 1024),
				oc.hits, oc.misses, oc.written / 1024, oc.declined, oc.too_big,
				onyx_io_written() / 1024);
	return NSERROR_OK;
}

static nserror oc_store(nsurl *url, enum backing_store_flags flags, uint8_t *data,
		const size_t datalen)
{
	int elem = (flags & BACKING_STORE_META) ? OC_ELEM_META : OC_ELEM_DATA;
	struct oc_entry *e;
	struct oc_elem *el;

	if (!oc.up)
		return NSERROR_INIT_FAILED;
	kapi_lock(&oc.lk);
	e = oc_find(url);
	if (e == NULL && elem == OC_ELEM_DATA) {
		/* Onyx (docs/06 §32): a new object stored only when it is worth it -- not a
		 * large body, and only an URL seen in an earlier launch (most of what a page
		 * fetches is never asked for again) */
		char kb[4096];
		uint32_t h = oc_key_hash(oc_key(url, kb, sizeof kb));
		int sn = oc_seen(h);
		if (oc.ram && datalen <= OC_MAX_OBJECT_RAM) {
			/* (RAM: the first time -- no card to spare) */
		} else if (datalen > (oc.ram ? OC_MAX_OBJECT_RAM : OC_MAX_OBJECT) || sn != 1) {
			if (sn < 0 && !oc.ram) {
				oc_seen_add(h, false);
				oc.index_dirty = true;
			}
			if (datalen > OC_MAX_OBJECT)
				oc.too_big++;
			else
				oc.declined++;
			kapi_unlock(&oc.lk);
			return NSERROR_NOSPACE;	/* (llcache keeps it in memory) */
		}
	}
	if (e == NULL) {
		e = calloc(1, sizeof *e);
		char kb[4096];
		if (e == NULL || (e->url = strdup(oc_key(url, kb, sizeof kb))) == NULL) {
			free(e);
			kapi_unlock(&oc.lk);
			return NSERROR_NOMEM;
		}
		e->hash = nsurl_hash(url);
		e->id = oc.next_id++;
		e->next = oc.hash[e->hash % OC_HASH];
		oc.hash[e->hash % OC_HASH] = e;
		oc.count++;
	}
	el = &e->e[elem];
	if (el->data != data) {
		/* (another version: the old bytes let go -- by llcache when it has them) */
		oc_unref(el);
		el->data = data;
		el->ref = 1;		/* (the caller's: its release) */
	} else {
		el->ref++;
	}
	oc.total -= el->len;
	el->len = datalen;
	oc.total += datalen;
	el->on_card = false;
	e->used = ++oc.clock;
	if (!el->queued) {
		el->ref++;		/* (the writer's, until written) */
		el->queued = true;
		if (!oc_queue(e, elem, 0)) {
			el->queued = false;
			el->ref--;
		}
	}
	oc_evict();
	kapi_unlock(&oc.lk);
	if (!oc.threads)
		oc_write_some();
	if (elem == OC_ELEM_DATA && onyx_perf_on())
		fprintf(stderr, "ONYX-PERF cache:store %s %lu bytes\n", nsurl_access(url),
				(unsigned long) datalen);
	return NSERROR_OK;
}

static nserror oc_fetch(nsurl *url, enum backing_store_flags flags, uint8_t **data,
		size_t *datalen)
{
	int elem = (flags & BACKING_STORE_META) ? OC_ELEM_META : OC_ELEM_DATA;
	struct oc_entry *e;
	struct oc_elem *el;
	char path[300];
	unsigned id;

	if (!oc.up)
		return NSERROR_INIT_FAILED;
	kapi_lock(&oc.lk);
	e = oc_find(url);
	if (e == NULL || (e->e[elem].data == NULL && !e->e[elem].on_card)) {
		if (elem == OC_ELEM_META)
			oc.misses++;
		kapi_unlock(&oc.lk);
		return NSERROR_NOT_FOUND;
	}
	el = &e->e[elem];
	e->used = ++oc.clock;
	if (el->data != NULL) {
		el->ref++;
		*data = el->data;
		*datalen = el->len;
		kapi_unlock(&oc.lk);
		return NSERROR_OK;
	}
	id = e->id;
	kapi_unlock(&oc.lk);

	/* read from the card (outside the lock: the writer goes on meanwhile) */
	{
		uint64_t t0 = onyx_perf_now();
		size_t len = 0;
		uint8_t *b;
		oc_path(path, sizeof path, id, elem);
		b = oc_read(path, &len);
		kapi_lock(&oc.lk);
		e = oc_find(url);
		if (b == NULL || e == NULL || e->id != id) {
			if (e != NULL && b == NULL)
				oc_kill(e);	/* (its file is gone: forgotten) */
			kapi_unlock(&oc.lk);
			free(b);
			return NSERROR_NOT_FOUND;
		}
		el = &e->e[elem];
		if (el->data != NULL) {		/* (read meanwhile) */
			free(b);
		} else {
			el->data = b;
			el->len = len;
		}
		el->ref++;
		*data = el->data;
		*datalen = el->len;
		if (elem == OC_ELEM_DATA)
			oc.hits++;
		kapi_unlock(&oc.lk);
		if (onyx_perf_on())
			fprintf(stderr, "ONYX-PERF cache:read %s %lu bytes %lu us\n", nsurl_access(url),
					(unsigned long) len, (unsigned long) (onyx_perf_now() - t0));
	}
	return NSERROR_OK;
}

static nserror oc_release(nsurl *url, enum backing_store_flags flags)
{
	int elem = (flags & BACKING_STORE_META) ? OC_ELEM_META : OC_ELEM_DATA;
	char kb[4096];
	const char *s = oc_key(url, kb, sizeof kb);
	uint32_t h = nsurl_hash(url);
	struct oc_entry *e;

	int pass;

	kapi_lock(&oc.lk);
	/* (the live entry first, then a dead one: llcache may still hold its bytes) */
	for (pass = 0, e = NULL; pass < 2 && e == NULL; pass++)
		for (e = oc.hash[h % OC_HASH]; e != NULL; e = e->next)
			if (e->hash == h && e->dead == (pass == 1) && strcmp(e->url, s) == 0 &&
			    e->e[elem].ref > 0)
				break;
	if (e != NULL) {
		oc_unref(&e->e[elem]);
		if (e->dead)
			oc_reap(e);
	}
	kapi_unlock(&oc.lk);
	return e != NULL ? NSERROR_OK : NSERROR_NOT_FOUND;
}

static nserror oc_invalidate(nsurl *url)
{
	struct oc_entry *e;

	kapi_lock(&oc.lk);
	e = oc_find(url);
	if (e != NULL)
		oc_kill(e);
	kapi_unlock(&oc.lk);
	return NSERROR_OK;
}

static struct gui_llcache_table onyx_llcache_table_s = {
	.initialise = oc_initialise,
	.finalise = oc_finalise,
	.store = oc_store,
	.fetch = oc_fetch,
	.invalidate = oc_invalidate,
	.release = oc_release,
};

struct gui_llcache_table *onyx_llcache_table = &onyx_llcache_table_s;
