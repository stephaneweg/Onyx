/*
 * Apps/media/codecs.c -- the code of Media Player's audio decoders (codecs.h): minimp3, dr_flac and dr_wav
 * compiled once, as C, with the FPU (user/Makefile's media rule). stb_vorbis: vorbis.c (its static
 * names would clash with minimp3's).
 */
#define MINIMP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION
#include "codecs.h"
