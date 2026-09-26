#ifndef ONYX_MOCK_KAPI_H
#define ONYX_MOCK_KAPI_H
// mock_kapi.h -- host mock of the file kapi (trash.h / fsutil.h tests): "SD:/x" maps to
// $ROOT/x. Same return conventions as the kernel: mkdir / remove / rename 0 = ok, -1 = error.
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
struct kapi_dirent { char name[128]; unsigned size; int is_dir; };
static const char *mock_root = "/tmp/onyx_mock";
static void mp (const char *p, char *o) { if (!strncmp (p, "SD:", 3)) p += 3; snprintf (o, 512, "%s/%s", mock_root, p); }
static inline void *kapi_open (const char *p) { char o[512]; mp (p, o); struct stat s; if (stat (o, &s) || S_ISDIR (s.st_mode)) return 0; return fopen (o, "rb"); }
static inline int kapi_read (void *f, void *b, unsigned n) { return (int) fread (b, 1, n, (FILE *) f); }
static inline void kapi_close (void *f) { fclose ((FILE *) f); }
static inline int kapi_fsize (void *f) { long c = ftell ((FILE *) f); fseek ((FILE *) f, 0, SEEK_END); long n = ftell ((FILE *) f); fseek ((FILE *) f, c, SEEK_SET); return (int) n; }
static inline int kapi_save_file (const char *p, const void *b, unsigned n) { char o[512]; mp (p, o); FILE *f = fopen (o, "wb"); if (!f) return -1; fwrite (b, 1, n, f); fclose (f); return (int) n; }
static inline void *kapi_opendir (const char *p) { char o[512]; mp (p, o); return opendir (o); }
static inline int kapi_readdir (void *d, struct kapi_dirent *e)
{
	struct dirent *x;
	while ((x = readdir ((DIR *) d)) && (!strcmp (x->d_name, ".") || !strcmp (x->d_name, ".."))) ;
	if (!x) return 0;
	strncpy (e->name, x->d_name, 127); e->name[127] = 0; e->is_dir = x->d_type == DT_DIR; e->size = 0;
	return 1;
}
static inline void kapi_closedir (void *d) { closedir ((DIR *) d); }
static inline int kapi_mkdir (const char *p) { char o[512]; mp (p, o); return mkdir (o, 0755) == 0 ? 0 : -1; }
static inline int kapi_remove (const char *p) { char o[512]; mp (p, o); return remove (o) == 0 ? 0 : -1; }
static inline int kapi_rename (const char *a, const char *b) { char o[512], q[512]; mp (a, o); mp (b, q); return rename (o, q) == 0 ? 0 : -1; }
#endif
