/*
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
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

/** \file
 * Interface to javascript engine functions.
 */

#ifndef NETSURF_JAVASCRIPT_JS_H_
#define NETSURF_JAVASCRIPT_JS_H_

#include "utils/errors.h"

struct dom_event;
struct dom_document;
struct dom_node;
struct dom_element;
struct dom_string;

/**
 * JavaScript interpreter heap
 *
 * In order to try and be moderately performant, we create a heap
 * per browser window.  This heap is shared by all browsing contexts
 * we end up creating in that window.
 */
typedef struct jsheap jsheap;

/**
 * JavaScript interpreter thread
 *
 * When we create a browsing context itself (window+content) we have
 * to create a JS thread to attach to the browsing context.
 *
 * JS threads are associated with heaps and will be destroyed when
 * the heap is destroyed.  They can be shut down manually though
 * and should be for object lifetime safety reasons.
 */
typedef struct jsthread jsthread;

/**
 * Initialise javascript interpreter
 */
void js_initialise(void);

/**
 * finalise javascript interpreter
 */
void js_finalise(void);

/**
 * Create a new javascript heap.
 *
 * There is usually one heap per browser window.
 *
 * \param timeout elapsed wallclock time (in seconds) before \a callback is called
 * \param heap Updated to the created JS heap
 * \return NSERROR_OK on success, appropriate error otherwise.
 */
nserror js_newheap(int timeout, jsheap **heap);

/**
 * Destroy a previously created heap.
 *
 * \param heap The heap to destroy
 */
void js_destroyheap(jsheap *heap);

/**
 * Onyx: a heap shared by another window -- a tab's iframes run in its heap (the same
 * QuickJS runtime: their windows reach each other's objects). Each share is given back
 * by js_destroyheap; the heap goes with the last.
 *
 * \param heap The heap to share (NULL: none)
 * eturn the heap
 */
jsheap *js_heap_share(jsheap *heap);

/**
 * Create a new javascript thread
 *
 * This is called once for a page with javascript script tags on
 * it. It constructs a fresh global window object and prepares the JS
 * browsing context.  It's important that threads are shut down cleanly
 * when the browsing context is going to be cleaned up.
 *
 * \param heap The heap to create the thread within
 * \param win_priv The value to give to the Window constructor as the window
 * \param doc_priv The value to give to the Document constructor as the document
 * \param thread Updated to the created thread
 * \return NSERROR_OK on success, appropriate error otherwise
 */
nserror js_newthread(jsheap *heap, void *win_priv, void *doc_priv, jsthread **thread);

/**
 * Close a javascript thread
 *
 * This should be called when the HTML content which owns the thread is
 * being closed.  This is a separate process from destroying the thread
 * and merely disconnects any callbacks and thus hopefully stops
 * additional JS things from triggering.  If any code runs and attempts to
 * register callbacks after closedown, they will fail.
 *
 * \param thread The thread to close down
 * \return NSERROR_OK on success, appropriate error otherwise
 */
nserror js_closethread(jsthread *thread);

/**
 * Destroy a javascript thread
 *
 * This should be called when the browsing context is done with the thread.
 *
 * This will be called when the HTML content associated with the browsing
 * context is being destroyed.  The thread should have already been closed
 * down during the HTML content close.
 *
 * \param thread The thread to be destroyed
 */
void js_destroythread(jsthread *thread);

/**
 * execute some javascript in a context
 */
bool js_exec(jsthread *thread, const uint8_t *txt, size_t txtlen, const char *name);

/**
 * Onyx: the <script> element whose code js_exec is about to run (document.currentScript),
 * NULL after it. Not a reference: set around the one call.
 */
void js_set_current_script(jsthread *thread, struct dom_node *node);

/**
 * Onyx: a <script type="module"> met by the parser: the scripts (dom.js) fetch it and its
 * imports and run it once the document is parsed (a module is deferred).
 */
void js_module_script(jsthread *thread, struct dom_node *node);

/**
 * fire an event at a dom node
 */
bool js_fire_event(jsthread *thread, const char *type, struct dom_document *doc, struct dom_node *target);

bool
js_dom_event_add_listener(jsthread *thread,
			  struct dom_document *document,
			  struct dom_node *node,
			  struct dom_string *event_type_dom,
			  void *js_funcval);

/*** New Events ***/

/**
 * Handle a new element being created.
 *
 * This is called once an element is inserted into the DOM document handled
 * by the context provided.  The JS implementation must then scan the element
 * for on* attributes and register appropriate listeners for those handlers.
 */
void js_handle_new_element(jsthread *thread, struct dom_element *node);

/**
 * Handle an event propagation finished callback.
 *
 * This is called once an event finishes propagating, no matter how it
 * finishes.  The intent here is that the JS context can perform any cleanups
 * it may need to perform before the DOM finishes and the event may end up
 * freed.
 */
void js_event_cleanup(jsthread *thread, struct dom_event *evt);

/**
 * Onyx: what an event dispatched by the browser carries (a mouse's, a key's).
 */
struct js_event_init {
	int x, y;		/**< the pointer, in the page (scripts get it in the viewport) */
	int button;		/**< the mouse button (0: the main one) */
	const char *key;	/**< a key's name ("a", "Enter", "ArrowLeft"...) */
	int key_code;		/**< its legacy keyCode */
	bool shift, ctrl, alt;
	int delta_x, delta_y;	/**< Onyx: a wheel's, in px (down / right: positive) */
};

/**
 * Onyx: an event the browser dispatches to the page's scripts -- a click, a key, the
 * window's scroll -- through their listeners (capture, target, bubble) and on*
 * attributes / properties.
 *
 * \param thread the document's thread
 * \param type the event's type ("click")
 * \param target its node; NULL: the window
 * \param init the mouse's / key's data, or NULL
 * 
eturn false when a listener cancelled it (preventDefault): no default action
 */
bool js_dispatch_event(jsthread *thread, const char *type, struct dom_node *target,
		const struct js_event_init *init);

/**
 * Onyx: an animation's event (html/onyx_anim.c): on a node, a TransitionEvent
 * ("transitionrun", "transitionstart", "transitionend", "transitioncancel": name the
 * property) or an AnimationEvent ("animationstart", "animationiteration", "animationend",
 * "animationcancel": name the animation's), elapsed in seconds; without a node, an
 * animation's state ("finish", "cancel") for its Animation object. id: the animation's.
 */
void js_dispatch_anim_event(jsthread *thread, const char *type, struct dom_node *target,
		const char *name, double elapsed, int id);

/**
 * Onyx: an animation frame -- the requestAnimationFrame callbacks run (now: the monotonic
 * clock's ms, performance.now()'s).
 */
void js_animation_frame(jsthread *thread, double now);

#endif /* NETSURF_JAVASCRIPT_JS_H_ */
