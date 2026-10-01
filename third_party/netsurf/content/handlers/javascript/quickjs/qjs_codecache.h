/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: the code cache (qjs_codecache.c) -- the bytecode of scripts kept on the card.
 */

#ifndef NETSURF_QJS_CODECACHE_H
#define NETSURF_QJS_CODECACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "quickjs.h"

/** A global script compiled (JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY): read from the
 * code cache when the cache has this source's bytecode, else compiled and its bytecode
 * written there (sources of QJS_CC_MIN bytes and more). strip: the functions' source text
 * left out (Function.prototype.toString then shows none: the preludes only). */
JSValue qjs_cc_compile(JSContext *ctx, const char *src, size_t len, const char *name,
		bool strip);

/** The bytecode the cache has for this source (malloc'd, *bclen bytes), or NULL. */
uint8_t *qjs_cc_load(const char *src, size_t len, size_t *bclen);

/** Keep this source's bytecode in the cache (written by a thread of its own on the Pi). */
void qjs_cc_store(const char *src, size_t len, const uint8_t *bc, size_t bclen);

/** Onyx (docs/06 §33): the cache's folder ("RAM:/jet/jscache/", the default; or the card's,
 * Choices' cache_on_card) and its size (0: 32 MB) -- before the first script. */
void qjs_cc_set_dir(const char *dir, size_t budget);

/** the smallest source worth caching (parsing less is quick) */
#define QJS_CC_MIN 8192

#endif
