// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
// applib.h -- the applib.h helpers telnetd.c uses, for the PC test (run_telnetd_test.sh).
#ifndef MOCK_TELNETD_APPLIB_H
#define MOCK_TELNETD_APPLIB_H
static inline void ax_puts (const char *s) { (void) s; }
static inline void ax_putln (const char *s) { (void) s; }
static inline void ax_itoa (int v, char *b) { sprintf (b, "%d", v); }
#endif
