/* Copyright (c) 2011 The WebM project authors. All Rights Reserved. */
/*  */
/* Use of this source code is governed by a BSD-style license */
/* that can be found in the LICENSE file in the root of the source */
/* tree. An additional intellectual property rights grant can be found */
/* in the file PATENTS.  All contributing project authors may */
/* be found in the AUTHORS file in the root of the source tree. */
#include "vpx/vpx_codec.h"
static const char* const cfg = "--target=generic-gnu --disable-vp8-encoder --disable-vp9-encoder --disable-examples --disable-tools --disable-docs --disable-unit-tests --disable-multithread --disable-webm-io --disable-libyuv --disable-vp9-highbitdepth --disable-postproc --disable-vp9-postproc --enable-static --disable-shared --size-limit=8192x8192";
const char *vpx_codec_build_config(void) {return cfg;}
