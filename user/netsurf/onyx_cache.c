/*
 * onyx_cache.c -- Onyx: NetSurf's disk cache on the card (the low-level cache's backing
 * store: content/backing_store.h's gui_llcache_table, registered by the framebuffer frontend).
 *
 * llcache writes an object here once it is complete and worth keeping -- fresh for a while
 * (Cache-Control max-age, Expires) or revalidatable (an ETag, a Last-Modified: Onyx change
 * in llcache.c) -- and reads it back when a page asks for its URL again, in this launch or a
 * later one: a fresh object needs no request at all, a stale one is revalidated
 * (If-None-Match / If-Modified-Since; a 304 keeps the stored bytes). Each object is two
 * files in the cache folder (SD:/apps/netsurf.app/cache/ by default): <id>.d its data and
 * <id>.m its metadata (llcache's serialisation: the URL, the headers, the times); "index"
 * lists them ("id data-size meta-size last-use url"), read at the start, written after the
 * writes and at the end. The size is bounded (Choices' disc_cache_size, 64 MB by default on
 * Onyx): past it the least recently used objects go.
 *
 * The files are written by a thread of its own (kernel v67): llcache's store call only
 * queues the object (the UI thread does not wait for the card); reads are made at once (the
 * page waits for them anyway). The file calls are the kapi's (whole files: kapi_save_file,
 * kapi_open / kapi_read), not newlib's (its descriptor table is not the threads').
 * Memory: the object's bytes are shared with llcache (the store takes a reference; llcache's
 * release drops it), as the upstream fs_backing_store does.
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
	bool index_dirty;
	unsigned hits, misses, written;
} oc;

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
	kapi_save_file(path, buf != NULL ? buf : "", (unsigned) len);
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
		{
			bool ok = kapi_save_file(path, en->e[elem].data, (unsigned) en->e[elem].len) == 0;
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

static int oc_thread(void *arg)
{
	unsigned last_index = kapi_get_ticks();

	(void) arg;
	while (!oc.stop) {
		unsigned seen = oc.wake;
		oc_write_some();
		/* the index: a few seconds after the last writes (not after each one) */
		if (oc.index_dirty && kapi_get_ticks() - last_index > 300) {
			oc_write_index();
			last_index = kapi_get_ticks();
		}
		if (kapi_wait_word(&oc.wake, seen, 200) < 0)
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
	kapi_mkdir(oc.dir);
	oc.limit = params != NULL && params->limit != 0 ? params->limit : 64u << 20;
	oc.hysteresis = params != NULL ? params->hysteresis : oc.limit / 5;
	if (oc.hysteresis >= oc.limit)
		oc.hysteresis = oc.limit / 5;
	oc.next_id = 1;
	oc_load_index();
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
		fprintf(stderr, "ONYX-PERF cache:open %s: %u objects, %lu KB (limit %lu KB)\n",
				oc.dir, oc.count, (unsigned long) (oc.total / 1024),
				(unsigned long) (oc.limit / 1024));
	return NSERROR_OK;
}

static nserror oc_finalise(void)
{
	unsigned t0;

	if (!oc.up)
		return NSERROR_OK;
	oc.up = false;		/* (no store from now on) */
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
				"%u KB written\n", oc.count, (unsigned long) (oc.total / 1024),
				oc.hits, oc.misses, oc.written / 1024);
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
