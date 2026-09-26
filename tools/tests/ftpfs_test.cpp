// ftpfs_test -- host test of user/bin/ftpfs.cpp's FTP client logic (plain FTP), against
// a real server. Usage: ftpfs_test <FTP:user:pass@host:port> ; the server's root must hold
// readme.txt ("hello onyx\n") and an empty folder "sub". See run_ftpfs_test.sh.
static int mock_session (char *) { return 0; }
static const char *g_payload; static unsigned g_payloadLen;
#include "ftpfs.cpp"
int kapi_vfs_req_data (unsigned, void *b, unsigned cap, unsigned off)
{ unsigned n = off >= g_payloadLen ? 0 : g_payloadLen - off; if (n > cap) n = cap; memcpy (b, g_payload + off, n); return (int) n; }
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAIL %d: %s\n", __LINE__, #c); fails++; } else printf ("ok   %s\n", #c); } while (0)
static bool listed (const char *name, bool *dir = 0)
{
	for (int p = 0; p < g_outLen; )
	{
		const char *n = (const char *) g_out + p + 5;
		if (!strcmp (n, name)) { if (dir) *dir = g_out[p + 4]; return true; }
		p += 6 + (int) strlen (n);
	}
	return false;
}
int main (int argc, char **argv)
{
	char base[200]; snprintf (base, sizeof base, "%s", argc > 1 ? argv[1] : "FTP:tester:secret@127.0.0.1:2121");
	char p[300], p2[300];
	g_outCap = 4096; g_out = (unsigned char *) malloc (g_outCap);
	snprintf (p, sizeof p, "%s/", base);
	g_outLen = 0; int n = op_list (p); bool d = false;
	CHECK (n >= 2); CHECK (listed ("readme.txt")); CHECK (listed ("sub", &d) && d);
	snprintf (p, sizeof p, "%s/readme.txt", base);
	unsigned size = 0; int fid = op_open (p, &size);
	CHECK (fid >= 0 && size == 11 && !memcmp (g_files[fid].data, "hello onyx\n", 11));
	static char blob[300000]; for (unsigned i = 0; i < sizeof blob; i++) blob[i] = (char) (i * 7);
	g_payload = blob; g_payloadLen = sizeof blob;
	struct kapi_vfs_req q; memset (&q, 0, sizeof q); snprintf (q.path, sizeof q.path, "%s/sub/blob.bin", base); q.in_len = sizeof blob;
	CHECK (op_save (q) == (int) sizeof blob);
	unsigned bsz = 0; int bf = op_open (q.path, &bsz);
	CHECK (bf >= 0 && bsz == sizeof blob && !memcmp (g_files[bf].data, blob, sizeof blob));
	snprintf (p, sizeof p, "%s/newdir", base);
	CHECK (op_simple (VFS_OP_MKDIR, p, 0) == 0);
	snprintf (p, sizeof p, "%s/sub/blob.bin", base); snprintf (p2, sizeof p2, "%s/newdir/moved.bin", base);
	CHECK (op_simple (VFS_OP_RENAME, p, p2) == 0);
	snprintf (p, sizeof p, "%s/newdir/", base);
	g_outLen = 0; op_list (p); CHECK (listed ("moved.bin"));
	CHECK (op_simple (VFS_OP_REMOVE, p2, 0) == 0);
	snprintf (p, sizeof p, "%s/newdir", base);
	CHECK (op_simple (VFS_OP_REMOVE, p, 0) == 0);			// (DELE fails, RMD works)
	snprintf (p, sizeof p, "%s/", base);
	g_outLen = 0; op_list (p); CHECK (!listed ("newdir"));
	snprintf (p, sizeof p, "%s/nothere.txt", base);
	CHECK (op_open (p, &size) < 0);
	// A dropped control connection is re-established once.
	for (int i = 0; i < MAXSRV; i++) if (g_srv[i].used) { kapi_tcp_close (g_srv[i].ctl.sock); }
	snprintf (p, sizeof p, "%s/", base);
	g_outLen = 0; CHECK (op_list (p) >= 2);
	printf (fails ? "%d FAILURE(S)\n" : "ftpfs: all checks passed\n", fails);
	return fails != 0;
}
