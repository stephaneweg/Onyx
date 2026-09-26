#pragma once
#include <stdio.h>
#include <string.h>
#include <kern/kapi_abi.h>
static const char *fake_ini = "";
static struct kapi_pad fake_pad; static int fake_there = 1;
static inline void *kapi_open (const char *p) { (void) p; return fake_ini[0] ? (void *) 1 : 0; }
static inline int kapi_read (void *f, void *b, unsigned n) { (void) f; unsigned l = strlen (fake_ini); if (l > n) l = n; memcpy (b, fake_ini, l); return (int) l; }
static inline void kapi_close (void *f) { (void) f; }
static inline int kapi_pad_state (int i, struct kapi_pad *o) { if (i || !fake_there) return 0; *o = fake_pad; return 1; }
