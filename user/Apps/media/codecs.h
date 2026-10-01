//
// Apps/media/codecs.h -- the audio decoders of Media Player, from third_party (single-file libraries):
// minimp3 (MP3, CC0), stb_vorbis (Ogg Vorbis, public domain / MIT), dr_flac and dr_wav (FLAC, WAV,
// public domain / MIT-0). Their declarations here; their code is compiled once in codecs.c (C, the
// FPU on). No stdio: every file is read through callbacks over the kapi (decode.h).
//
#ifndef _media_codecs_h
#define _media_codecs_h

#define MINIMP3_NO_STDIO
#define DR_FLAC_NO_STDIO
#define DR_WAV_NO_STDIO
#define DR_FLAC_NO_OGG
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY

#include "minimp3/minimp3_ex.h"
#include "dr_libs/dr_flac.h"
#include "dr_libs/dr_wav.h"
#include "stb_vorbis/stb_vorbis.c"

#endif
