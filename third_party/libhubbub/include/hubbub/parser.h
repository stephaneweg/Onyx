/*
 * This file is part of Hubbub.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007-8 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef hubbub_parser_h_
#define hubbub_parser_h_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <inttypes.h>

#include <hubbub/errors.h>
#include <hubbub/functypes.h>
#include <hubbub/tree.h>
#include <hubbub/types.h>

typedef struct hubbub_parser hubbub_parser;

/**
 * Hubbub parser option types
 */
typedef enum hubbub_parser_opttype {
	HUBBUB_PARSER_TOKEN_HANDLER,
	HUBBUB_PARSER_ERROR_HANDLER,
	HUBBUB_PARSER_CONTENT_MODEL,
	HUBBUB_PARSER_TREE_HANDLER,
	HUBBUB_PARSER_DOCUMENT_NODE,
	HUBBUB_PARSER_ENABLE_SCRIPTING,
	HUBBUB_PARSER_PAUSE,
	HUBBUB_PARSER_LAST_START_TAG,	/**< Onyx: the tokeniser's last start tag */
	HUBBUB_PARSER_FRAGMENT_CONTEXT	/**< Onyx: parse a fragment in this context */
} hubbub_parser_opttype;

/**
 * Hubbub parser option parameters
 */
typedef union hubbub_parser_optparams {
	struct {
		hubbub_token_handler handler;
		void *pw;
	} token_handler;		/**< Token handling callback */

	struct {
		hubbub_error_handler handler;
		void *pw;
	} error_handler;		/**< Error handling callback */

	struct {
		hubbub_content_model model;
	} content_model;		/**< Current content model */

	hubbub_tree_handler *tree_handler;	/**< Tree handling callbacks */

	void *document_node;		/**< Document node */

	bool enable_scripting;		/**< Whether to enable scripting */

	bool pause_parse;		/**< Pause parsing */

	const char *last_start_tag;	/**< Onyx: a start tag's name, or NULL */

	/** Onyx: the context element of a fragment (innerHTML, the tests): its namespace,
	 * its name (lower case for HTML), its node (the tree handler's) and the form element
	 * it is in, if any (node and form may be NULL) */
	struct {
		hubbub_ns ns;
		const char *name;
		void *node;
		void *form;
		bool html_integration_point;	/**< an annotation-xml with an HTML encoding */
		bool quirks;		/**< the context's document is in quirks mode */
	} fragment_context;
} hubbub_parser_optparams;

/* Create a hubbub parser */
hubbub_error hubbub_parser_create(const char *enc, bool fix_enc,
		hubbub_parser **parser);
/* Destroy a hubbub parser */
hubbub_error hubbub_parser_destroy(hubbub_parser *parser);

/* Configure a hubbub parser */
hubbub_error hubbub_parser_setopt(hubbub_parser *parser,
		hubbub_parser_opttype type,
		hubbub_parser_optparams *params);

/* Pass a chunk of data to a hubbub parser for parsing */
/* This data is encoded in the input charset */
hubbub_error hubbub_parser_parse_chunk(hubbub_parser *parser,
		const uint8_t *data, size_t len);

/**
 * Insert a chunk of data into a hubbub parser input stream
 *
 * This data is encoded in the input charset
 *
 * Inserts the given data into the input stream ready for parsing but
 * does not cause any additional processing of the input. This is
 * useful to allow hubbub callbacks to add computed data to the input.
 * 
 * \param parser  Parser instance to use
 * \param data    Data to parse (encoded in the input charset)
 * \param len     Length, in bytes, of data
 * \return HUBBUB_OK on success, appropriate error otherwise
 */
hubbub_error hubbub_parser_insert_chunk(hubbub_parser *parser,
					const uint8_t *data, size_t len);
/* Inform the parser that the last chunk of data has been parsed */
hubbub_error hubbub_parser_completed(hubbub_parser *parser);

/* Read the document charset */
const char *hubbub_parser_read_charset(hubbub_parser *parser,
		hubbub_charset_source *source);

#ifdef __cplusplus
}
#endif

#endif

