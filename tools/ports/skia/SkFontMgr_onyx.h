// SkFontMgr_onyx.h -- Onyx's Skia font manager (tools/ports/skia; docs/POSIX-PLAN.md "Ports for WebKit": the font
// decision). Instead of fontconfig: Skia's custom directory font manager over the card's fonts (SD:/res/fonts/),
// with what WebKit asks fontconfig for --
//   - the CSS generic families and the usual web family names, mapped to the card's fonts (sans-serif ->
//     DejaVu Sans, serif -> DejaVu Serif, monospace -> DejaVu Sans Mono, Arial / Helvetica -> Liberation Sans,
//     Times New Roman -> Liberation Serif, Georgia -> Gelasio, Segoe UI -> Selawik...);
//   - family names matched without regard to case (CSS);
//   - a fallback font for a character (matchFamilyStyleCharacter: what WebKit's FontCache asks on Android and
//     Windows -- the family asked first, then the card's families in a fixed order, the closest style; cached).
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is hereby granted, free
// of charge, to any person obtaining a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice
// and this permission notice shall be included in all copies or substantial portions of the Software. THE
// SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
#ifndef SkFontMgr_onyx_DEFINED
#define SkFontMgr_onyx_DEFINED

#include "include/core/SkFontMgr.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"

// The font manager over a directory of TrueType / OpenType files (nullptr: SD:/res/fonts/).
SK_API sk_sp<SkFontMgr> SkFontMgr_New_Onyx (const char *dir = nullptr);

// The family a CSS / web family name maps to on the card ("sans-serif" -> "DejaVu Sans"), or nullptr when the
// name is not an alias (it may still be an installed family).
SK_API const char *SkFontMgr_Onyx_Alias (const char *family);

#endif
