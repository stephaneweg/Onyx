//
// ftpfs.h -- talk to /bin/ftpfs (the FTP: / FTPS: file-system provider) and read its list of
// remembered servers. Header-only, no libc (C and C++, freestanding apps and newlib tools).
//
//   ftpfs_login ("ftp.example.com", "me", "secret", remember)  -> 1 if delivered
//       hand a login over IPC (starting ftpfs if needed), so the password never has to be
//       written into an FTP: path. remember = also save it in SD:/etc/ftpfs.ini.
//   ftpfs_login_site (&site, remember)   same, with the port / FTPS / start folder too
//       (the File Viewer's Connect dialog: they pre-fill the form next time).
//   ftpfs_forget ("ftp.example.com")     drop a login (memory + the file).
//   ftpfs_load_sites (sites, max)        the remembered servers (the Connect dialog's list).
//
// SD:/etc/ftpfs.ini holds one line per server (or .ini sections, see ftpfs_load_sites):
//     host user hexpass [port tls folder]        ("-" = empty user / password)
// hexpass = the password XOR a fixed key, in hex: OBFUSCATED, NOT ENCRYPTED.
// IPC: service "ftpfs"; message type 1 = "host\0user\0pass\0[port\0tls\0folder\0]",
// 2 = the same + remember, 3 = "host\0" forget (see user/BinUtils/ftpfs.cpp).
//
#ifndef _ftpfs_h
#define _ftpfs_h
#include "appkit/appkit.h"
#include "nk_api.h"


#define FTPFS_SITES_FILE	"SD:/etc/ftpfs.ini"
#define FTPFS_MAXSITES		16

struct ftpfs_site
{
	char host[128], user[64], pass[64], port[8], folder[128];
	int  tls;			// 1 = FTPS
};
NK_API void ftpfs__cpy (char *d, const char *s, int cap);

NK_API void ftpfs_obf_hex (const char *in, char *out, int cap);
NK_API int ftpfs__hv (char c);
NK_API void ftpfs_unobf_hex (const char *in, char *out, int cap);
// Parse one file line; 1 if it is a server.
NK_API int ftpfs_parse_site (const char *line, struct ftpfs_site *s);
// Format one file line (with '\n'); returns its length.
NK_API int ftpfs_format_site (const struct ftpfs_site *s, char *out, int cap);
// A text file saved by a Windows editor: drop a UTF-8 BOM, turn UTF-16 (Notepad) into
// 8-bit. In place; returns the new length.
NK_API int ftpfs_text_fix (char *b, int n);
NK_API int ftpfs__ieq (const char *a, const char *b);
// The remembered servers, in file order. Returns how many. Also reads the same data
// written by hand in .ini style:
//     [ftp.example.com]
//     user = me
//     password = secret        (plain; or hexpass = <obfuscated>)
//     port = 21   tls = 1   folder = /www      (one per line, all optional)
NK_API int ftpfs_load_sites (struct ftpfs_site *sites, int max);
NK_API int ftpfs__pid (void);
NK_API int ftpfs__send (int type, const char *const *part, int nparts);
NK_API int ftpfs_login (const char *host, const char *user, const char *pass, int remember);
NK_API int ftpfs_login_site (const struct ftpfs_site *s, int remember);
NK_API int ftpfs_forget (const char *host);

#if defined (NK_BODIES_INLINE) && !defined (NK_IMPL)
#include "ftpfs.inc"
#endif

#endif
