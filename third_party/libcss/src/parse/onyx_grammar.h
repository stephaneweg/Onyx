/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: the CSS specifications' value grammars (the "Value Definition Syntax" of every
 * property and at-rule descriptor, from the W3C's webref data: onyx_grammar_gen.py makes
 * onyx_grammar_tables.c) and a matcher that checks a declaration's value against them.
 *
 * libcss parses the properties it computes with its own parsers; a property it does not
 * compute (anchor-name, scroll-snap-type, mask-image, text-wrap...), or a value its parser
 * does not take for one it does (a newer syntax), is checked against the property's official
 * grammar here: a valid value is kept as an opaque declaration (CSS_ONYX_OP_GENERIC: no
 * computed effect), an invalid one is dropped as a browser drops it. CSS.supports(),
 * @supports and element.style then answer as a browser does.
 */

#ifndef css_parse_onyx_grammar_h_
#define css_parse_onyx_grammar_h_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <parserutils/utils/vector.h>

#include "lex/lex.h"

/* a node of the grammar graph (op: ONYX_G_*) */
typedef struct {
	uint8_t op, flags;
	uint16_t a, b, c;
} onyx_gnode;

/* a named grammar: its name in onyx_grammar_strings, its root node */
typedef struct {
	uint16_t name, root;
} onyx_gname;

enum onyx_gop {
	ONYX_G_KW = 1,		/* a keyword: a = its name (lower case) */
	ONYX_G_CHAR = 2,	/* a delimiter: a = the character */
	ONYX_G_PRIM = 3,	/* a primitive type: a = ONYX_P_*, b = a range (onyx_grammar_ranges) */
	ONYX_G_SEQ = 4,		/* a b c: the kids a .. a + b - 1 */
	ONYX_G_ALL = 5,		/* a && b */
	ONYX_G_ANY = 6,		/* a || b */
	ONYX_G_ONE = 7,		/* a | b */
	ONYX_G_MULT = 8,	/* a{b,c} (c 0xffff: no maximum); flags 1: '#' (comma separated) */
	ONYX_G_FUNC = 9,	/* a( b ): a = the name, b = the arguments (0xffff: none) */
	ONYX_G_REF = 10,	/* a reference: a = the target (0xffff: a grammar that could not be built) */
	ONYX_G_BANG = 11,	/* [ a ]!: a group that must not be empty */
	ONYX_G_NUM = 12		/* a numeric literal (0, 90deg): a = its text */
};

/* the primitive types (onyx_grammar_gen.py's PRIMS, in that order) */
enum onyx_prim {
	ONYX_P_LENGTH, ONYX_P_PERCENTAGE, ONYX_P_LENGTH_PERCENTAGE, ONYX_P_NUMBER,
	ONYX_P_INTEGER, ONYX_P_ANGLE, ONYX_P_ANGLE_PERCENTAGE, ONYX_P_TIME,
	ONYX_P_TIME_PERCENTAGE, ONYX_P_FREQUENCY, ONYX_P_FREQUENCY_PERCENTAGE,
	ONYX_P_RESOLUTION, ONYX_P_FLEX, ONYX_P_DIMENSION, ONYX_P_STRING, ONYX_P_URL,
	ONYX_P_IDENT, ONYX_P_CUSTOM_IDENT, ONYX_P_DASHED_IDENT, ONYX_P_HEX_COLOR,
	ONYX_P_URANGE, ONYX_P_ANY_VALUE, ONYX_P_DECLARATION_VALUE, ONYX_P_ZERO, ONYX_P_ID,
	ONYX_P_DECIBEL, ONYX_P_SEMITONES, ONYX_P_NUMBER_PERCENTAGE,
	ONYX_P_CUSTOM_PROPERTY_NAME, ONYX_P_AN_PLUS_B, ONYX_P_FUNCTION_TOKEN,
	ONYX_P_UNICODE_RANGE_TOKEN, ONYX_P_DASHED_FUNCTION, ONYX_P_RATIO_NUMBER,
	ONYX_P_CALC_SUM
};

extern const onyx_gnode onyx_grammar_nodes[];
extern const uint16_t onyx_grammar_kids[];
extern const char onyx_grammar_strings[];
extern const float onyx_grammar_ranges[][2];
extern const onyx_gname onyx_grammar_props[];
extern const unsigned onyx_grammar_nprops;
extern const onyx_gname onyx_grammar_descs[];
extern const unsigned onyx_grammar_ndescs;

/* The index of the property named name (any case) in onyx_grammar_props, or -1. */
int css__onyx_grammar_property(const char *name, size_t len);

/* The index of the descriptor "@rule/name" (any case) in onyx_grammar_descs, or -1. */
int css__onyx_grammar_descriptor(const char *rule_desc, size_t len);

/* Do the tokens [start, end) of the vector (whitespace ignored) match the grammar whose root
 * is root? */
bool css__onyx_grammar_match(uint16_t root, const parserutils_vector *vector,
		int32_t start, int32_t end);

/* The math functions (calc(), min(), round(), sin()...): is this function token one? */
bool css__onyx_is_math_function(const css_token *t);

/* The value types of CSS's arithmetic (a math function's result) */
enum onyx_mtype {
	ONYX_MT_NUMBER, ONYX_MT_LENGTH, ONYX_MT_ANGLE, ONYX_MT_TIME, ONYX_MT_FREQ,
	ONYX_MT_RES, ONYX_MT_FLEX, ONYX_MT_PERCENT, ONYX_MT_BAD = 0xff
};

/* The kind (ONYX_MT_*) of a dimension's unit (any case), else ONYX_MT_BAD. */
int css__onyx_unit_kind(const char *unit, size_t len);

/* The type of the math function whose tokens (whitespace ignored) are [start, end) of the
 * vector -- the function token to its ')' --, a percentage standing for the type pct
 * (ONYX_MT_PERCENT: a percentage; ONYX_MT_BAD: not allowed). ONYX_MT_BAD if invalid. */
int css__onyx_math_type(const parserutils_vector *vector, int32_t start, int32_t end,
		int pct);

#endif
