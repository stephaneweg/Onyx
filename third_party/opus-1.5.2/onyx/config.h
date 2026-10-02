/*
 * third_party/opus-1.5.2/onyx/config.h -- libopus's config.h for Onyx's builds (the Pi, the PC
 * bench, Windows), written by hand in place of its configure (README.onyx): the floating-point
 * build, C only (no intrinsics, no run-time CPU detection), no deep PLC / DRED / OSCE.
 */
#ifndef ONYX_OPUS_CONFIG_H
#define ONYX_OPUS_CONFIG_H
#define OPUS_BUILD 1
#define VAR_ARRAYS 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1
#define HAVE_STDINT_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define PACKAGE_VERSION "1.5.2"
#endif
