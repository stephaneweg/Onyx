/*
 * onyx_mucompat.h -- forced into every MuPDF source of the Onyx build (mupdf.mk, MU_ONYX=1: -include): newlib's
 * <sys/types.h> defines "quad" as "quad_t" (a BSD leftover) -- MuPDF's fz_stext_char has a member "quad"; newlib
 * has no timegm (onyx_mucompat.c).
 */
#ifndef ONYX_MUCOMPAT_H
#define ONYX_MUCOMPAT_H
#include <sys/types.h>
#include <time.h>
#undef quad
time_t timegm (struct tm *tm);
#endif
