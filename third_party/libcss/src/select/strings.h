/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_select_strings_h_
#define css_select_strings_h_

#include <libcss/errors.h>

/* Onyx: the strings of the pseudo-classes libcss matches since (select.c) */
enum {
	ONYX_STR_READ_ONLY,
	ONYX_STR_READ_WRITE,
	ONYX_STR_REQUIRED,
	ONYX_STR_OPTIONAL,
	ONYX_STR_PLACEHOLDER_SHOWN,
	ONYX_STR_DEFINED,
	ONYX_STR_SCOPE,
	ONYX_STR_DIR,
	ONYX_STR_LTR,
	ONYX_STR_INPUT,
	ONYX_STR_TEXTAREA,
	ONYX_STR_SELECT,
	ONYX_STR_READONLY,
	ONYX_STR_PLACEHOLDER,
	ONYX_STR_VALUE,
	ONYX_STR_TYPE,
	ONYX_STR_CONTENTEDITABLE,
	ONYX_STR_CHECKBOX,
	ONYX_STR_RADIO,
	ONYX_STR_SUBMIT,
	ONYX_STR_BUTTON,
	ONYX_STR_RESET,
	ONYX_STR_HIDDEN,
	ONYX_STR_IMAGE,
	ONYX_STR_FILE,
	ONYX_STR_COLOR,
	ONYX_STR_RANGE,
	ONYX_STR_HOST,			/* shadow DOM: :host, :host(), :host-context() */
	ONYX_STR_HOST_CONTEXT,
	ONYX_STR_SLOTTED,		/* ::slotted() */
	ONYX_STR_PART,			/* ::part() */
	ONYX_STR_FEATURELESS,		/* the name a featureless shadow host is looked up by */
	ONYX_STR_N
};

/** Useful interned strings */
typedef struct {
	lwc_string *universal;
	lwc_string *first_child;
	lwc_string *link;
	lwc_string *visited;
	lwc_string *hover;
	lwc_string *active;
	lwc_string *focus;
	lwc_string *nth_child;
	lwc_string *nth_last_child;
	lwc_string *nth_of_type;
	lwc_string *nth_last_of_type;
	lwc_string *last_child;
	lwc_string *first_of_type;
	lwc_string *last_of_type;
	lwc_string *only_child;
	lwc_string *only_of_type;
	lwc_string *root;
	lwc_string *empty;
	lwc_string *target;
	lwc_string *lang;
	lwc_string *enabled;
	lwc_string *disabled;
	lwc_string *checked;
	lwc_string *first_line;
	lwc_string *first_letter;
	lwc_string *before;
	lwc_string *after;

	lwc_string *width;
	lwc_string *height;
	lwc_string *prefers_color_scheme;

	lwc_string *onyx[ONYX_STR_N];		/* Onyx: ONYX_STR_* */
} css_select_strings;

css_error css_select_strings_intern(css_select_strings *str);
void css_select_strings_unref(css_select_strings *str);

#endif

