/*
 * Copyright 2010 John-Mark Bell <jmb@netsurf-browser.org>
 *
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
 * \file desktop/download.c
 * \brief Core download context implementation
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>	/* (Onyx: strncasecmp) */

#include "content/llcache.h"
#include "utils/corestrings.h"
#include "utils/http.h"
#include "utils/utils.h"
#include "utils/log.h"
#include "desktop/download.h"
#include "netsurf/download.h"
#include "desktop/gui_internal.h"
#include "netsurf/onyx_jet.h"	/* Onyx: the name a link asks for, a script's bytes */

/**
 * A context for a download
 */
struct download_context {
	llcache_handle *llcache;		/**< Low-level cache handle */
	struct gui_window *parent;		/**< Parent window */

	lwc_string *mime_type;			/**< MIME type of download */
	unsigned long long int total_length;	/**< Length of data, in bytes */
	char *filename;				/**< Suggested filename */
	char *hint;				/**< Onyx: <a download="name">'s name */

	struct gui_download_window *window;	/**< GUI download window */
};

/* Onyx: the name the next download context takes (<a download="name">) */
static char *download_next_hint;

void download_onyx_hint(const char *name)
{
	free(download_next_hint);
	download_next_hint = (name != NULL && name[0] != '\0') ? strdup(name) : NULL;
}

/* Onyx: a script's bytes saved as a download (the frontend's: onyx_download.c) */
void (*onyx_download_bytes_hook)(const void *data, size_t len, const char *name,
		const char *mime, const char *url) = NULL;

static int download_hex(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
		c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/**
 * Onyx: a %-encoded string decoded (n bytes of s) -> malloc'd; latin1: its bytes are
 * ISO-8859-1, made UTF-8.
 */
static char *download_unescape(const char *s, size_t n, bool latin1)
{
	char *out = malloc(n * 2 + 1);
	size_t i, j = 0;

	if (out == NULL)
		return NULL;
	for (i = 0; i < n; i++) {
		unsigned c = (unsigned char) s[i];
		if (c == '%' && i + 2 < n && download_hex(s[i + 1]) >= 0 &&
		    download_hex(s[i + 2]) >= 0) {
			c = download_hex(s[i + 1]) * 16 + download_hex(s[i + 2]);
			i += 2;
		}
		if (latin1 && c >= 0x80) {
			out[j++] = (char) (0xC0 | c >> 6);
			out[j++] = (char) (0x80 | (c & 0x3F));
		} else {
			out[j++] = (char) c;
		}
	}
	out[j] = '\0';
	return out;
}

/**
 * Onyx: the file name a Content-Disposition gives -- filename*=charset'lang'%-encoded
 * (RFC 6266 / 5987: UTF-8 or ISO-8859-1) first, else filename= (a token or a quoted
 * string); its last path segment. NULL when none.
 */
static char *download_disposition_filename(const char *h)
{
	char *plain = NULL, *ext = NULL, *r;
	const char *p = h;

	p += strcspn(p, ";");
	while (*p == ';') {
		const char *name;
		size_t nlen;
		char buf[1024];
		size_t bl = 0;
		p++;
		while (*p == ' ' || *p == '\t')
			p++;
		name = p;
		while (*p && *p != '=' && *p != ';')
			p++;
		nlen = p - name;
		while (nlen > 0 && (name[nlen - 1] == ' ' || name[nlen - 1] == '\t'))
			nlen--;
		if (*p != '=')
			continue;
		p++;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '"') {
			for (p++; *p && *p != '"'; p++) {
				if (*p == '\\' && p[1] != '\0')
					p++;
				if (bl < sizeof buf - 1)
					buf[bl++] = *p;
			}
			if (*p == '"')
				p++;
			while (*p && *p != ';')
				p++;
		} else {
			while (*p && *p != ';') {
				if (bl < sizeof buf - 1)
					buf[bl++] = *p;
				p++;
			}
			while (bl > 0 && (buf[bl - 1] == ' ' || buf[bl - 1] == '\t'))
				bl--;
		}
		buf[bl] = '\0';
		if (nlen == 9 && strncasecmp(name, "filename*", 9) == 0 && ext == NULL) {
			/* charset'language'value */
			char *q1 = strchr(buf, '\''), *q2 = q1 != NULL ? strchr(q1 + 1, '\'') : NULL;
			if (q2 != NULL) {
				bool latin1 = (q1 - buf == 10 && strncasecmp(buf, "iso-8859-1", 10) == 0);
				ext = download_unescape(q2 + 1, strlen(q2 + 1), latin1);
			}
		} else if (nlen == 8 && strncasecmp(name, "filename", 8) == 0 && plain == NULL) {
			/* (a quoted UTF-8 name, as the browsers take it; %-escapes as Chrome) */
			plain = download_unescape(buf, bl, false);
		}
	}
	if (ext != NULL) {
		r = ext;
		free(plain);
	} else {
		r = plain;
	}
	if (r != NULL) {
		char *slash = strrchr(r, '/'), *bs = strrchr(r, '\\'), *t;
		if (bs != NULL && (slash == NULL || bs > slash))
			slash = bs;
		if (slash != NULL) {
			t = strdup(slash + 1);
			free(r);
			r = t;
		}
		if (r != NULL && r[0] == '\0') {
			free(r);
			r = NULL;
		}
	}
	return r;
}

/**
 * Compute a default filename for a download -- Onyx: the address's last path segment,
 * %-decoded ("download" when it has none: the frontend makes it safe and may add an
 * extension)
 *
 * \param url  URL of item being fetched
 * \return Default filename, or NULL on memory exhaustion
 */
static char *download_default_filename(nsurl *url)
{
	lwc_string *path = nsurl_get_component(url, NSURL_PATH);
	lwc_string *scheme = nsurl_get_component(url, NSURL_SCHEME);
	char *name = NULL;
	bool data = scheme != NULL && strcasecmp(lwc_string_data(scheme), "data") == 0;

	if (path != NULL && !data) {
		const char *s = lwc_string_data(path), *slash = strrchr(s, '/');
		const char *seg = slash != NULL ? slash + 1 : s;
		if (seg[0] != '\0')
			name = download_unescape(seg, strlen(seg), false);
	}
	if (path != NULL)
		lwc_string_unref(path);
	if (scheme != NULL)
		lwc_string_unref(scheme);
	if (name == NULL || name[0] == '\0') {
		free(name);
		name = strdup("download");
	}
	return name;
}

/**
 * Process fetch headers for a download context.
 * Extracts MIME type, total length, and creates gui_download_window
 *
 * \param ctx  Context to process
 * \return NSERROR_OK on success, appropriate error otherwise
 */
static nserror download_context_process_headers(download_context *ctx)
{
	const char *http_header;
	http_content_type *content_type;
	unsigned long long int length;
	nserror error;

	/* Retrieve and parse Content-Type */
	http_header = llcache_handle_get_header(ctx->llcache, "Content-Type");
	if (http_header == NULL)
		http_header = "text/plain";

	error = http_parse_content_type(http_header, &content_type);
	if (error != NSERROR_OK)
		return error;

	/* Retrieve and parse Content-Length */
	http_header = llcache_handle_get_header(ctx->llcache, "Content-Length");
	if (http_header == NULL) {
		length = 0;
	} else {
		length = strtoull(http_header, NULL, 10);
	}

	/* Retrieve and parse Content-Disposition -- Onyx: filename* too; its name wins over
	 * the link's <a download="name"> */
	http_header = llcache_handle_get_header(ctx->llcache,
			"Content-Disposition");
	if (http_header != NULL)
		ctx->filename = download_disposition_filename(http_header);
	if (ctx->filename == NULL && ctx->hint != NULL)
		ctx->filename = strdup(ctx->hint);

	ctx->mime_type = lwc_string_ref(content_type->media_type);
	ctx->total_length = length;
	if (ctx->filename == NULL) {
		ctx->filename = download_default_filename(
				llcache_handle_get_url(ctx->llcache));
	}

	http_content_type_destroy(content_type);

	if (ctx->filename == NULL) {
		lwc_string_unref(ctx->mime_type);
		ctx->mime_type = NULL;
		return NSERROR_NOMEM;
	}

	/* Create the frontend window */
	ctx->window = guit->download->create(ctx, ctx->parent);
	if (ctx->window == NULL) {
		free(ctx->filename);
		ctx->filename = NULL;
		lwc_string_unref(ctx->mime_type);
		ctx->mime_type = NULL;
		return NSERROR_NOMEM;
	}

	return NSERROR_OK;
}

/**
 * Callback for low-level cache events
 *
 * \param handle  Low-level cache handle
 * \param event   Event object
 * \param pw      Our context
 * \return NSERROR_OK on success, appropriate error otherwise
 */
static nserror download_callback(llcache_handle *handle,
		const llcache_event *event, void *pw)
{
	download_context *ctx = pw;
	nserror error = NSERROR_OK;

	switch (event->type) {
	case LLCACHE_EVENT_GOT_CERTS:
		/* Nominally not interested in these */
		break;
	case LLCACHE_EVENT_HAD_HEADERS:
		error = download_context_process_headers(ctx);
		if (error != NSERROR_OK) {
			llcache_handle_abort(handle);
			download_context_destroy(ctx);
		}

		break;

	case LLCACHE_EVENT_HAD_DATA:
		/* If we didn't know up-front that this fetch was for download,
		 * then we won't receive the HAD_HEADERS event. Catch up now.
		 */
		if (ctx->window == NULL) {
			error = download_context_process_headers(ctx);
			if (error != NSERROR_OK) {
				llcache_handle_abort(handle);
				download_context_destroy(ctx);
			}
		}

		if (error == NSERROR_OK) {
			/** \todo Lose ugly cast */
			error = guit->download->data(ctx->window,
					(char *) event->data.data.buf,
					event->data.data.len);
			if (error != NSERROR_OK)
				llcache_handle_abort(handle);
		}

		break;

	case LLCACHE_EVENT_DONE:
		/* There may be no associated window if there was no data or headers */
		if (ctx->window != NULL)
			guit->download->done(ctx->window);
		else
			download_context_destroy(ctx);

		break;

	case LLCACHE_EVENT_ERROR:
		if (ctx->window != NULL)
			guit->download->error(ctx->window, event->data.error.msg);
		else
			download_context_destroy(ctx);

		break;

	case LLCACHE_EVENT_PROGRESS:
		break;

	case LLCACHE_EVENT_REDIRECT:
		break;
	}

	return error;
}

/* See download.h for documentation */
nserror download_context_create(llcache_handle *llcache, 
		struct gui_window *parent)
{
	download_context *ctx;

	ctx = malloc(sizeof(*ctx));
	if (ctx == NULL)
		return NSERROR_NOMEM;

	ctx->llcache = llcache;
	ctx->parent = parent;
	ctx->mime_type = NULL;
	ctx->total_length = 0;
	ctx->filename = NULL;
	ctx->window = NULL;
	ctx->hint = download_next_hint;		/* (Onyx: <a download="name">) */
	download_next_hint = NULL;

	llcache_handle_change_callback(llcache, download_callback, ctx);

	return NSERROR_OK;
}

/* See download.h for documentation */
void download_context_destroy(download_context *ctx)
{
	llcache_handle_release(ctx->llcache);

	lwc_string_unref(ctx->mime_type);

	free(ctx->filename);
	free(ctx->hint);

	/* Window is not owned by us, so don't attempt to destroy it */

	free(ctx);
}

/* See download.h for documentation */
void download_context_abort(download_context *ctx)
{
	llcache_handle_abort(ctx->llcache);
}

/* See download.h for documentation */
nsurl *download_context_get_url(const download_context *ctx)
{
	return llcache_handle_get_url(ctx->llcache);
}

/* See download.h for documentation */
const char *download_context_get_mime_type(const download_context *ctx)
{
	return lwc_string_data(ctx->mime_type);
}

/* See download.h for documentation */
unsigned long long int
download_context_get_total_length(const download_context *ctx)
{
	return ctx->total_length;
}

/* See download.h for documentation */
const char *download_context_get_filename(const download_context *ctx)
{
	return ctx->filename;
}

