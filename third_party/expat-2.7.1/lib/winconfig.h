/*
 * winconfig.h -- expat's Windows configuration (as upstream's lib/winconfig.h, expat 2.7.1).
 *
 * Copyright (c) 1997-2000 Thai Open Source Software Center Ltd
 * Copyright (c) 2000-2025 Expat development team
 * Licensed under the MIT license (third_party/expat-2.7.1/COPYING).
 *
 * Onyx: only pc/Jet's Windows build (mingw-w64) reads it; the Pi and the PC bench do not.
 */
#ifndef WINCONFIG_H
#define WINCONFIG_H

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

#include <memory.h>
#include <string.h>

#endif /* ndef WINCONFIG_H */
