/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: @supports, @layer, @container (src/parse/onyx_atrules.c).
 */

#ifndef css_css__parse_onyx_atrules_h_
#define css_css__parse_onyx_atrules_h_

#include <parserutils/utils/vector.h>

#include "parse/language.h"
#include "parse/mq.h"

/** Does the @supports condition at *ctx (to the end of the vector) hold? */
bool css__onyx_supports(css_language *c, const parserutils_vector *vector, int32_t *ctx);

/** An @container's query (after its optional name), parsed as a media query list. */
css_error css__onyx_container_media(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_mq_query **media);

#endif
