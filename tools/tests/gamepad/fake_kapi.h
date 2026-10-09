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
// the keyboard's held keys (gamepad.h's [keyboard]): fake_held[] the codes held
#ifndef KEY_UP
#define KEY_BACKSPACE 8
#define KEY_TAB 9
#define KEY_ENTER 13
#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103
#define KEY_HOME 0x104
#define KEY_END 0x105
#define KEY_PGUP 0x106
#define KEY_PGDN 0x107
#define KEY_DEL 0x108
#define KEY_F1 0x110
#define KEY_F12 0x11B
#endif
static int fake_held[8], fake_nheld;
static inline int kapi_key_held (int k) { for (int i = 0; i < fake_nheld; i++) if (fake_held[i] == k) return 1; return 0; }
static char fake_saved[8192]; static unsigned fake_saved_n;
static inline int kapi_save_file (const char *p, const void *b, unsigned n) { (void) p; if (n >= sizeof fake_saved) return -1; memcpy (fake_saved, b, n); fake_saved[n] = 0; fake_saved_n = n; fake_ini = fake_saved; return (int) n; }
