/*
 * pc/Jet/compat/regex.h -- POSIX regex for Jet Browser's Windows build: MinGW has none (nor has the Pi's
 * newlib an implementation): the types NetSurf uses, and stubs (pc/Jet/winkapi.cpp) that match nothing.
 */
#ifndef JET_COMPAT_REGEX_H
#define JET_COMPAT_REGEX_H
#include <stddef.h>
#include <sys/types.h>

#define REG_EXTENDED 1
#define REG_ICASE (1 << 1)
#define REG_NEWLINE (1 << 2)
#define REG_NOSUB (1 << 3)
#define REG_NOMATCH 1
#define REG_BADPAT 2

typedef long long regoff_t;
typedef struct { size_t re_nsub; void *re_priv; } regex_t;
typedef struct { regoff_t rm_so, rm_eo; } regmatch_t;

#ifdef __cplusplus
extern "C" {
#endif
int regcomp(regex_t *preg, const char *regex, int cflags);
int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);
size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size);
void regfree(regex_t *preg);
#ifdef __cplusplus
}
#endif
#endif
