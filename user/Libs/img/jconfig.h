/*
 * jconfig.h -- IJG libjpeg build configuration for Onyx (aarch64-none-elf + newlib).
 *
 * Derived from the upstream jconfig.txt for a modern 64-bit ANSI C compiler. The
 * Makefile copies this next to the libjpeg sources before compiling (libjpeg's *.c
 * do  #include "jconfig.h"). jmorecfg.h / jpeglib.h ship with the source tarball.
 */
#define HAVE_PROTOTYPES 1
#define HAVE_UNSIGNED_CHAR 1
#define HAVE_UNSIGNED_SHORT 1
#undef CHAR_IS_UNSIGNED
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1
#undef NEED_BSD_STRINGS
#undef NEED_SYS_TYPES_H
#undef NEED_FAR_POINTERS
#undef NEED_SHORT_EXTERNAL_NAMES
#undef INCOMPLETE_TYPES_BROKEN
#ifdef JPEG_INTERNALS
#undef RIGHT_SHIFT_IS_UNSIGNED
#endif
