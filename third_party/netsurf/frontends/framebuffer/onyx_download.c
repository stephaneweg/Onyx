/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Onyx (Jet Browser, docs/06 §38): the downloads.
 *
 * The core makes a download context for a response that is not shown -- a type NetSurf does
 * not display, a "Content-Disposition: attachment" (content/hlcache.c), a link with a download
 * attribute (html/interaction.c, dom.js) -- and calls this table: create at its head, data as
 * its body comes, done / error at its end. create answers at once (the fetch goes on) and the
 * Save dialog is shown from the main loop a moment later (framebuffer_schedule: never inside
 * the core's callback): the name filled in -- the Content-Disposition's, the link's, else the
 * address's last segment, made safe for the card (FAT: no / \ : * ? " < > |) -- in
 * SD:/Downloads, made when missing. Meanwhile the bytes that came are kept; the fetcher's thread
 * stops reading past 8 MB not taken (onyx_fetch.c), so a big file waits in the socket.
 *
 * Saved: a WRITER THREAD writes the bytes as they come (kapi_file_out, 64 KB at a time -- the
 * card's writes never on the UI thread); the core's data callback only queues them. Progress:
 * the toolbar's download button (its menu: each download, Cancel), the status bar, the log
 * ("ONYX-DOWNLOAD ..." lines). Cancel in the dialog, or in the button's menu: the fetch
 * aborted, the partial file removed. The end: a notification (notifyd) and the status bar;
 * an error: a message box.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "netsurf/download.h"
#include "netsurf/onyx_jet.h"
#include "desktop/download.h"

#include "framebuffer/schedule.h"
#include "framebuffer/onyx_download.h"

#include "netsurf/onyx_chrome.h"	/* user/netsurf: the dialog, the button */
#include "kapi.h"

#define DL_DIR		"SD:/Downloads"
#define DL_NAME_MAX	60		/* (the dialog's name box holds 63) */
#define DL_PIECE	(64 * 1024)	/* written at once */
#define DL_PENDING_MAX	(64u << 20)	/* kept before the dialog's answer, at most */
#define DL_LIST_MAX	16		/* the button's menu: that many, the oldest finished dropped */

struct dl_chunk {
	struct dl_chunk *next;
	size_t len;
	uint8_t data[];
};

struct gui_download_window {
	struct gui_download_window *next;
	int id;
	download_context *ctx;		/* the core's (NULL once destroyed, or a script's bytes) */
	char name[DL_NAME_MAX + 4];	/* ASCII, safe for the card */
	char path[512];			/* where it is written, "" while asked */
	char url[256];			/* (the log) */
	char err[160];
	unsigned long long got, total, written;
	int state;			/* ONYX_DL_* */
	bool finished;			/* every byte came (the core's done) */
	/* before the dialog's answer */
	uint8_t *pend;
	size_t plen, pcap;
	/* the file and its writer */
	void *out;
	volatile int lk;		/* kapi_lock: the queue, the flags below */
	struct dl_chunk *qh, *qt;
	size_t queued;
	volatile int wfinish;		/* no more bytes: close when the queue is empty */
	volatile int wcancel;		/* stop now, drop the queue */
	volatile int werr;		/* a write failed */
	volatile int wrun;		/* the writer thread runs */
	unsigned shown_at;		/* ticks: the progress last shown */
};

static struct gui_download_window *dl_list;
static int dl_next_id = 1;

static void dl_publish(void);

/* ---- the name ------------------------------------------------------------------------------ */

/* A character past ASCII as ASCII: an accented letter without its accent ("e" for U+00E9),
 * a few marks folded; NULL: none (it becomes '_') */
static const char *dl_fold(unsigned c)
{
	static const char *const latin1[64] = {	/* U+00C0 .. U+00FF */
		"A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
		"D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
		"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
		"d", "n", "o", "o", "o", "o", "o", NULL, "o", "u", "u", "u", "u", "y", "th", "y" };

	if (c >= 0xC0 && c <= 0xFF)
		return latin1[c - 0xC0];
	switch (c) {
	case 0xA0: return " ";
	case 0x152: return "OE";
	case 0x153: return "oe";
	case 0x2018: case 0x2019: return "'";
	case 0x2013: case 0x2014: return "-";
	case 0x20AC: return "EUR";
	}
	return NULL;
}

/* The UTF-8 name made a file name of the card -- ASCII (the card's names are code page 850
 * bytes on the Pi, UTF-16 on Windows: ASCII reads the same everywhere; accents dropped), no
 * / \ : * ? " < > | nor control characters, no leading / trailing dots or spaces, at most
 * DL_NAME_MAX bytes (the extension kept); an extension from the type when it has none. */
static void dl_safe_name(const char *in, const char *mime, char *out, size_t cap)
{
	static const struct { const char *mime, *ext; } exts[] = {
		{ "text/html", ".html" }, { "text/plain", ".txt" }, { "text/css", ".css" },
		{ "text/csv", ".csv" }, { "application/pdf", ".pdf" }, { "application/zip", ".zip" },
		{ "application/json", ".json" }, { "application/xml", ".xml" }, { "text/xml", ".xml" },
		{ "image/png", ".png" }, { "image/jpeg", ".jpg" }, { "image/gif", ".gif" },
		{ "image/webp", ".webp" }, { "image/svg+xml", ".svg" }, { "audio/mpeg", ".mp3" },
		{ "audio/wav", ".wav" }, { "video/mp4", ".mp4" }, { "application/gzip", ".gz" },
		{ "application/x-tar", ".tar" }, { "application/javascript", ".js" },
		{ "text/javascript", ".js" },
	};
	const unsigned char *p = (const unsigned char *) (in != NULL ? in : "");
	char buf[512];
	size_t n = 0, i, dot, extlen;

	while (*p != '\0' && n < sizeof buf - 3) {
		unsigned c = *p++;
		if (c >= 0x80) {	/* UTF-8: folded to ASCII */
			int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
			const char *f;
			c = more == 0 ? c : c & (0x3F >> more);
			for (; more > 0 && (*p & 0xC0) == 0x80; more--)
				c = c << 6 | (*p++ & 0x3F);
			f = dl_fold(c);
			if (f == NULL)
				buf[n++] = '_';
			for (; f != NULL && *f && n < sizeof buf - 3; f++)
				buf[n++] = *f;
			continue;
		}
		if (c < 0x20 || c == 0x7F || strchr("/\\:*?\"<>|", (int) c) != NULL)
			c = '_';
		buf[n++] = (char) c;
	}
	buf[n] = '\0';
	/* (trimmed: spaces and dots at the ends -- FAT drops them) */
	while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '.'))
		buf[--n] = '\0';
	for (i = 0; buf[i] == ' ' || buf[i] == '.'; i++)
		;
	if (i > 0) {
		memmove(buf, buf + i, n - i + 1);
		n -= i;
	}
	if (n == 0) {
		strcpy(buf, "download");
		n = 8;
	}
	/* no extension: one from the type */
	if (strrchr(buf, '.') == NULL && mime != NULL) {
		for (i = 0; i < sizeof exts / sizeof exts[0]; i++)
			if (strcasecmp(mime, exts[i].mime) == 0) {
				if (n + strlen(exts[i].ext) < sizeof buf) {
					strcpy(buf + n, exts[i].ext);
					n += strlen(exts[i].ext);
				}
				break;
			}
	}
	/* too long: the stem cut, the extension (up to 10) kept */
	if (n > DL_NAME_MAX) {
		const char *e = strrchr(buf, '.');
		extlen = e != NULL ? strlen(e) : 0;
		if (extlen > 10)
			extlen = 0;
		dot = DL_NAME_MAX - extlen;
		if (extlen > 0)
			memmove(buf + dot, e, extlen + 1);
		else
			buf[dot] = '\0';
		n = strlen(buf);
	}
	snprintf(out, cap, "%s", buf);
}

/* ---- the writer thread ------------------------------------------------------------------ */

static int dl_write_all(void *out, const uint8_t *p, size_t n)
{
	while (n > 0) {
		unsigned piece = n > DL_PIECE ? DL_PIECE : (unsigned) n;
		int w = kapi_stream_write(out, p, piece);
		if (w <= 0)
			return -1;
		p += w;
		n -= (size_t) w;
	}
	return 0;
}

/* (the UI thread, from the pump: the writer ended) */
static void dl_writer_ended(void *ctx, long value);

static int dl_writer(void *arg)
{
	struct gui_download_window *dw = arg;

	for (;;) {
		struct dl_chunk *c;
		bool fin;

		kapi_lock(&dw->lk);
		c = dw->wcancel ? NULL : dw->qh;
		if (c != NULL) {
			dw->qh = c->next;
			if (dw->qh == NULL)
				dw->qt = NULL;
			dw->queued -= c->len;
		}
		fin = dw->wfinish && dw->qh == NULL;
		kapi_unlock(&dw->lk);
		if (dw->wcancel)
			break;
		if (c != NULL) {
			if (!dw->werr && dl_write_all(dw->out, c->data, c->len) != 0)
				dw->werr = 1;
			else
				dw->written += c->len;
			free(c);
			continue;
		}
		if (fin || dw->werr)
			break;
		kapi_msleep(15);	/* (nothing queued: the next bytes) */
	}
	kapi_stream_close(dw->out);
	dw->out = NULL;
	/* the queue's rest (a cancel, an error) */
	kapi_lock(&dw->lk);
	while (dw->qh != NULL) {
		struct dl_chunk *c = dw->qh;
		dw->qh = c->next;
		free(c);
	}
	dw->qt = NULL;
	dw->queued = 0;
	kapi_unlock(&dw->lk);
	kapi__dmb();
	dw->wrun = 0;
	kapi_post(dl_writer_ended, dw, 0);
	return 0;
}

static bool dl_threads(void)
{
	return KT->version >= 67 && KT->thread_create != 0 && KT->post != 0;
}

/* The bytes to the file: queued for the writer (or, without threads, written here). */
static bool dl_put(struct gui_download_window *dw, const void *data, size_t n)
{
	struct dl_chunk *c;

	if (n == 0)
		return true;
	if (!dw->wrun && dw->out != NULL) {	/* (no thread: the UI thread writes) */
		if (dl_write_all(dw->out, data, n) != 0)
			return false;
		dw->written += n;
		return true;
	}
	if (dw->queued > DL_PENDING_MAX)
		return false;			/* (the card far slower than the network) */
	c = malloc(sizeof *c + n);
	if (c == NULL)
		return false;
	c->next = NULL;
	c->len = n;
	memcpy(c->data, data, n);
	kapi_lock(&dw->lk);
	if (dw->qt != NULL)
		dw->qt->next = c;
	else
		dw->qh = c;
	dw->qt = c;
	dw->queued += n;
	kapi_unlock(&dw->lk);
	return true;
}

/* ---- the states ---------------------------------------------------------------------------- */

static void dl_show_error(void *p);

static void dl_log(struct gui_download_window *dw, const char *what)
{
	printf("ONYX-DOWNLOAD %s id=%d name=%s path=%s got=%llu total=%llu%s%s\n", what,
	       dw->id, dw->name, dw->path, dw->got, dw->total,
	       dw->err[0] ? " error=" : "", dw->err);
	fflush(stdout);
	NSLOG(netsurf, INFO, "download %s: %s (%s) %llu / %llu %s", what, dw->name,
	      dw->path, dw->got, dw->total, dw->err);
}

/* the core's context let go (its fetch aborted first when it still runs) */
static void dl_drop_ctx(struct gui_download_window *dw, bool abort)
{
	if (dw->ctx == NULL)
		return;
	if (abort)
		download_context_abort(dw->ctx);
	download_context_destroy(dw->ctx);
	dw->ctx = NULL;
}

/* (the core's context let go from the main loop: not inside its own callback) */
static void dl_drop_later(void *p)
{
	dl_drop_ctx(p, true);
}

/* It ends: done (every byte written), failed or cancelled -- the file closed, removed
 * unless done. in_cb: inside the core's data callback (its context let go later). */
static void dl_end_cb(struct gui_download_window *dw, int state, const char *err, bool in_cb)
{
	if (dw->state != ONYX_DL_ASK && dw->state != ONYX_DL_RUNNING)
		return;
	dw->state = state;
	if (err != NULL)
		snprintf(dw->err, sizeof dw->err, "%s", err);
	free(dw->pend);
	dw->pend = NULL;
	dw->plen = dw->pcap = 0;
	if (state != ONYX_DL_DONE) {
		if (in_cb)
			framebuffer_schedule(0, dl_drop_later, dw);
		else
			dl_drop_ctx(dw, true);
		if (dw->wrun) {
			dw->wcancel = 1;	/* (the writer closes; dl_writer_ended removes) */
		} else {
			if (dw->out != NULL) {
				kapi_stream_close(dw->out);
				dw->out = NULL;
			}
			if (dw->path[0] != '\0')
				kapi_remove(dw->path);
		}
	}
	dl_log(dw, state == ONYX_DL_DONE ? "done" : state == ONYX_DL_FAILED ? "failed" :
	       "cancelled");
	if (state == ONYX_DL_DONE) {
		char msg[600];
		snprintf(msg, sizeof msg, "%s saved in %s", dw->name, dw->path);
		onyx_chrome_notify("Download complete", msg);
	} else if (state == ONYX_DL_FAILED) {
		framebuffer_schedule(0, dl_show_error, dw);	/* (not inside a callback) */
	}
	dl_publish();
}

static void dl_end(struct gui_download_window *dw, int state, const char *err)
{
	dl_end_cb(dw, state, err, false);
}

static void dl_show_error(void *p)
{
	struct gui_download_window *dw = p;
	char msg[800];

	snprintf(msg, sizeof msg, "%s could not be downloaded:\n%s", dw->name,
		 dw->err[0] ? dw->err : "an error");
	onyx_chrome_message("Download failed", msg);
}

/* Every byte came and the writer has them: the file complete once the writer ends. */
static void dl_finish_writing(struct gui_download_window *dw)
{
	if (dw->wrun) {
		kapi_lock(&dw->lk);
		dw->wfinish = 1;
		kapi_unlock(&dw->lk);
		return;			/* (dl_writer_ended: done) */
	}
	if (dw->out != NULL) {
		kapi_stream_close(dw->out);
		dw->out = NULL;
	}
	dl_end(dw, ONYX_DL_DONE, NULL);
}

static void dl_writer_ended(void *ctx, long value)
{
	struct gui_download_window *dw = ctx;

	(void) value;
	if (dw->state == ONYX_DL_RUNNING) {
		if (dw->werr)
			dl_end(dw, ONYX_DL_FAILED, "the file could not be written (the card full?)");
		else if (dw->finished)
			dl_end(dw, ONYX_DL_DONE, NULL);
	} else if (dw->state != ONYX_DL_DONE && dw->path[0] != '\0') {
		kapi_remove(dw->path);		/* (cancelled or failed: no partial file) */
	}
	dl_publish();
}

/* The Save dialog, from the main loop: where, then the file opened and the bytes so far
 * written. */
static void dl_ask(void *p)
{
	struct gui_download_window *dw = p;
	void *d;
	char path[512];

	if (dw->state != ONYX_DL_ASK)
		return;
	d = kapi_opendir(DL_DIR);
	if (d != NULL) {
		kapi_closedir(d);
	} else {
		int r = kapi_mkdir(DL_DIR);
		printf("ONYX-DOWNLOAD mkdir %s %s\n", DL_DIR, r == 0 ? "made" : "failed");
		fflush(stdout);
	}
	printf("ONYX-DOWNLOAD ask dir=%s name=%s\n", DL_DIR, dw->name);
	fflush(stdout);
	if (!onyx_chrome_save_dialog(DL_DIR, dw->name, path, sizeof path)) {
		dl_end(dw, ONYX_DL_CANCELLED, NULL);
		return;
	}
	if (dw->state != ONYX_DL_ASK)
		return;		/* (it failed meanwhile) */
	snprintf(dw->path, sizeof dw->path, "%s", path);
	{	/* (the name shown: the one chosen) */
		const char *slash = strrchr(path, '/');
		snprintf(dw->name, sizeof dw->name, "%s", slash != NULL ? slash + 1 : path);
	}
	dw->out = kapi_file_out(dw->path, 0);
	if (dw->out == NULL) {
		dl_end(dw, ONYX_DL_FAILED, "the file could not be made there");
		return;
	}
	dw->state = ONYX_DL_RUNNING;
	dl_log(dw, "save");
	if (dl_threads()) {
		dw->wrun = 1;
		if (kapi_thread_create(dl_writer, dw, 64 * 1024, "jet:download") < 0)
			dw->wrun = 0;
	}
	if (dw->plen > 0 && !dl_put(dw, dw->pend, dw->plen)) {
		dl_end(dw, ONYX_DL_FAILED, "the file could not be written");
		return;
	}
	free(dw->pend);
	dw->pend = NULL;
	dw->plen = dw->pcap = 0;
	if (dw->finished)
		dl_finish_writing(dw);
	dl_publish();
}

/* ---- the list, for the chrome -------------------------------------------------------------- */

static void dl_publish(void)
{
	struct onyx_dl_info info[DL_LIST_MAX];
	struct gui_download_window *dw;
	int n = 0;

	for (dw = dl_list; dw != NULL && n < DL_LIST_MAX; dw = dw->next) {
		info[n].id = dw->id;
		info[n].name = dw->name;
		info[n].path = dw->path;
		info[n].error = dw->err;
		info[n].got = dw->got;
		info[n].total = dw->total;
		info[n].state = dw->state;
		n++;
	}
	onyx_chrome_downloads(info, n);
}

/* the progress, now and then (the toolbar's button, the status bar) */
static void dl_progress(struct gui_download_window *dw)
{
	unsigned now = kapi_get_ticks();	/* (10 ms) */

	if (now - dw->shown_at >= 25) {
		dw->shown_at = now;
		dl_publish();
	}
}

static struct gui_download_window *dl_new(const char *name, const char *mime,
		unsigned long long total, const char *url)
{
	struct gui_download_window *dw = calloc(1, sizeof *dw), **pp;
	int count = 0;

	if (dw == NULL)
		return NULL;
	dw->id = dl_next_id++;
	dw->state = ONYX_DL_ASK;
	dw->total = total;
	dl_safe_name(name, mime, dw->name, sizeof dw->name);
	snprintf(dw->url, sizeof dw->url, "%s", url != NULL ? url : "");
	/* (the newest first; the oldest finished dropped past DL_LIST_MAX) */
	dw->next = dl_list;
	dl_list = dw;
	for (pp = &dl_list; *pp != NULL; ) {
		struct gui_download_window *o = *pp;
		count++;
		if (count > DL_LIST_MAX && o->state != ONYX_DL_ASK &&
		    o->state != ONYX_DL_RUNNING && !o->wrun) {
			*pp = o->next;
			free(o);
			continue;
		}
		pp = &o->next;
	}
	printf("ONYX-DOWNLOAD start id=%d url=%s name=%s total=%llu\n", dw->id, dw->url,
	       dw->name, total);
	fflush(stdout);
	dl_publish();
	framebuffer_schedule(0, dl_ask, dw);
	return dw;
}

/* ---- the core's table ---------------------------------------------------------------------- */

static struct gui_download_window *
onyx_download_create(download_context *ctx, struct gui_window *parent)
{
	struct gui_download_window *dw;
	nsurl *url = download_context_get_url(ctx);

	(void) parent;
	dw = dl_new(download_context_get_filename(ctx), download_context_get_mime_type(ctx),
		    download_context_get_total_length(ctx), url != NULL ? nsurl_access(url) : "");
	if (dw != NULL)
		dw->ctx = ctx;
	return dw;
}

static nserror onyx_download_data(struct gui_download_window *dw, const char *data,
		unsigned int size)
{
	if (dw->state == ONYX_DL_ASK) {
		/* (the dialog not answered yet: kept) */
		if (dw->plen + size > dw->pcap) {
			size_t cap = dw->pcap ? dw->pcap : 65536;
			uint8_t *nb;
			while (cap < dw->plen + size)
				cap *= 2;
			nb = cap > DL_PENDING_MAX ? NULL : realloc(dw->pend, cap);
			if (nb == NULL) {
				dl_end_cb(dw, ONYX_DL_FAILED, "out of memory", true);
				return NSERROR_NOMEM;
			}
			dw->pend = nb;
			dw->pcap = cap;
		}
		memcpy(dw->pend + dw->plen, data, size);
		dw->plen += size;
	} else if (dw->state == ONYX_DL_RUNNING) {
		if (!dl_put(dw, data, size)) {
			dl_end_cb(dw, ONYX_DL_FAILED, "the file could not be written", true);
			return NSERROR_SAVE_FAILED;
		}
	} else {
		return NSERROR_SAVE_FAILED;	/* (cancelled: the core aborts) */
	}
	dw->got += size;
	if (dw->total != 0 && dw->got > dw->total)
		dw->total = dw->got;
	dl_progress(dw);
	return NSERROR_OK;
}

static void onyx_download_error(struct gui_download_window *dw, const char *error_msg)
{
	/* (the core's context is done with: destroyed here, as the other frontends do) */
	if (dw->ctx != NULL) {
		download_context_destroy(dw->ctx);
		dw->ctx = NULL;
	}
	dl_end(dw, ONYX_DL_FAILED, error_msg != NULL && error_msg[0] ? error_msg :
	       "the connection failed");
}

static void onyx_download_done(struct gui_download_window *dw)
{
	if (dw->ctx != NULL) {
		download_context_destroy(dw->ctx);
		dw->ctx = NULL;
	}
	dw->finished = true;
	if (dw->total == 0 || dw->total < dw->got)
		dw->total = dw->got;
	if (dw->state == ONYX_DL_RUNNING)
		dl_finish_writing(dw);
	/* (still asked: dl_ask finishes it) */
	dl_publish();
}

static struct gui_download_table download_table = {
	.create = onyx_download_create,
	.data = onyx_download_data,
	.error = onyx_download_error,
	.done = onyx_download_done,
};

struct gui_download_table *onyx_download_table = &download_table;

/* ---- a script's bytes (a blob: <a download>), the menu's commands -------------------------- */

static void onyx_download_bytes(const void *data, size_t len, const char *name,
		const char *mime, const char *url)
{
	struct gui_download_window *dw = dl_new(name != NULL && name[0] ? name : "download",
			mime, len, url);

	if (dw == NULL)
		return;
	dw->pend = malloc(len > 0 ? len : 1);
	if (dw->pend == NULL) {
		dl_end(dw, ONYX_DL_FAILED, "out of memory");
		return;
	}
	memcpy(dw->pend, data, len);
	dw->plen = dw->pcap = len;
	dw->got = len;
	dw->finished = true;
}

void onyx_browser_download_cancel(int id)
{
	struct gui_download_window *dw;

	for (dw = dl_list; dw != NULL; dw = dw->next)
		if (dw->id == id && (dw->state == ONYX_DL_ASK || dw->state == ONYX_DL_RUNNING))
			dl_end(dw, ONYX_DL_CANCELLED, NULL);
}

void onyx_browser_downloads_clear(void)
{
	struct gui_download_window **pp = &dl_list;

	while (*pp != NULL) {
		struct gui_download_window *o = *pp;
		if (o->state != ONYX_DL_ASK && o->state != ONYX_DL_RUNNING && !o->wrun) {
			*pp = o->next;
			free(o);
			continue;
		}
		pp = &o->next;
	}
	dl_publish();
}

void onyx_download_init(void)
{
	onyx_download_bytes_hook = onyx_download_bytes;
}

void onyx_download_finalise(void)
{
	struct gui_download_window *dw;
	int i;

	for (dw = dl_list; dw != NULL; dw = dw->next)
		if (dw->state == ONYX_DL_ASK || dw->state == ONYX_DL_RUNNING)
			dl_end(dw, ONYX_DL_CANCELLED, "the browser closed");
	/* (the writers told: they close their files -- a moment for them) */
	for (i = 0; i < 50; i++) {
		bool busy = false;
		for (dw = dl_list; dw != NULL; dw = dw->next)
			busy |= dw->wrun != 0;
		if (!busy)
			break;
		kapi_msleep(10);
	}
	for (dw = dl_list; dw != NULL; dw = dw->next)
		if (dw->state == ONYX_DL_CANCELLED && dw->path[0] != '\0')
			kapi_remove(dw->path);
}
