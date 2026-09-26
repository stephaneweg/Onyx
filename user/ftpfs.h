//
// ftpfs.h -- hand a login to /bin/ftpfs (the FTP: / FTPS: file-system provider) over IPC,
// starting it if needed, so the password never has to be written into an FTP: path.
//   ftpfs_login ("ftp.example.com", "me", "secret", remember)  -> 1 if delivered (C, C++)
// remember = also save it in SD:/etc/ftpfs.ini (obfuscated, not encrypted) for next boots.
// Used by the File Viewer's Connect dialog and /bin/ftp. (Protocol: service "ftpfs",
// message type 1 = "host\0user\0pass\0", 2 = the same + remember; see user/bin/ftpfs.cpp.)
//
#ifndef _ftpfs_h
#define _ftpfs_h

#include "kapi.h"

static inline int ftpfs_login (const char *host, const char *user, const char *pass, int remember)
{
	int pid = kapi_ipc_lookup ("ftpfs");
	if (pid == 0)
	{
		kapi_exec ("SD:/bin/ftpfs", "");
		for (int i = 0; i < 300 && pid == 0; i++) { kapi_msleep (10); pid = kapi_ipc_lookup ("ftpfs"); }
	}
	if (pid == 0) return 0;
	char msg[200]; int n = 0;
	const char *part[3] = { host, user, pass };
	for (int k = 0; k < 3; k++) { for (int i = 0; part[k][i] && n < 196; i++) msg[n++] = part[k][i]; msg[n++] = '\0'; }
	return kapi_mailbox_send (pid, remember ? 2 : 1, msg, (unsigned) n) != 0;	// MSG_LOGIN(_SAVE)
}

#endif
