/* Onyx: expat's configuration for Jet Browser's builds (the Pi's bare-metal newlib, the PC
 * bench, Windows' MinGW) -- written by hand, no configure. See ../README.onyx. */
#ifndef EXPAT_CONFIG_H
#define EXPAT_CONFIG_H 1

#define BYTEORDER 1234
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define STDC_HEADERS 1
#define PACKAGE "expat"
#define VERSION "2.7.1"

/* no entropy source on the Pi: the hash salt is set by the caller (XML_SetHashSalt,
 * NetSurf's onyx_xml.c), the time-based fallback is only a fallback */
#define XML_POOR_ENTROPY 1

#define XML_CONTEXT_BYTES 1024
#define XML_DTD 1
#define XML_GE 1
#define XML_NS 1

#endif
