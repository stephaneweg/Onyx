// the two applib.h helpers ftpc.c uses
static inline int ax_strlen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static inline void ax_puts (const char *s) { kapi_stdout_write (s, (unsigned) ax_strlen (s)); }
static inline void ax_putln (const char *s) { ax_puts (s); kapi_stdout_write ("\n", 1); }
