/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: calc() / min() / max() / clamp() folded at cascade time (select/onyx_calc.c).
 */

#ifndef css_select_onyx_calc_h_
#define css_select_onyx_calc_h_

#include "select/select.h"

/**
 * Fold the calc expression following a *_CALC opv (its unit kind and its expression's
 * string index, both consumed from the bytecode) into one value.
 *
 * \param style   The bytecode, at the words after the opv
 * \param state   The selection state (its unit context: rem, vw, vh)
 * \param length  Receives the value
 * \param unit    Receives its bytecode unit: UNIT_PX, UNIT_EM, UNIT_PCT, or
 *                UNIT_CALC_NUMBER for a plain number
 * \return CSS_OK, or CSS_INVALID when the expression cannot be folded now (a mix of
 *         px / em / %, an unknown unit): the declaration is then ignored.
 */
css_error css__onyx_calc_fold(css_style *style, const css_select_state *state,
		css_fixed *length, uint32_t *unit);

#endif
