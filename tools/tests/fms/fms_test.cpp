//
// fms_test.cpp -- fmtracker's file formats on a PC: every .FMS given is parsed, written
// back and compared byte for byte; every .FMI is parsed and written back and re-parsed.
//   fms_test file.FMS ... file.FMI ...
//
#include <cstdio>
#include <cstring>
#include "../../../user/Apps/fmtracker/fms.h"

static unsigned char *slurp (const char *path, int *n)
{
	FILE *f = fopen (path, "rb"); if (!f) return 0;
	fseek (f, 0, SEEK_END); *n = (int) ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *b = new unsigned char[*n + 1];
	*n = (int) fread (b, 1, *n, f); b[*n] = 0; fclose (f); return b;
}

int main (int argc, char **argv)
{
	int bad = 0, fms = 0, fmi = 0;
	for (int i = 1; i < argc; i++)
	{
		int n; unsigned char *d = slurp (argv[i], &n);
		if (!d) { printf ("cannot read %s\n", argv[i]); bad++; continue; }
		int l = (int) strlen (argv[i]);
		if (l > 4 && (argv[i][l - 1] == 'S' || argv[i][l - 1] == 's'))
		{
			static FmsSong s; s.npat = 0;
			if (!fms_parse (d, n, &s)) { printf ("PARSE FAIL %s\n", argv[i]); bad++; }
			else
			{
				unsigned char *o = new unsigned char[fms_size (&s)];
				int m = fms_write (&s, o);
				if (m != n || memcmp (o, d, n)) { printf ("ROUNDTRIP DIFF %s (%d vs %d)\n", argv[i], m, n); bad++; }
				delete [] o;
				fms++;
			}
			fms_clear (&s);
		}
		else
		{
			FmsIns a, b;
			if (!fmi_parse ((const char *) d, &a)) { printf ("FMI PARSE FAIL %s\n", argv[i]); bad++; }
			else
			{
				char t[512]; fmi_write (&a, t);
				if (!fmi_parse (t, &b) || memcmp (&a, &b, sizeof a)) { printf ("FMI ROUNDTRIP DIFF %s\n", argv[i]); bad++; }
				fmi++;
			}
		}
		delete [] d;
	}
	char t[4]; fms_note_text (fms_make_note (1, 1, 4), t);
	printf ("%d FMS, %d FMI ok, %d bad; C#4 = %s, A4 = %u mHz, E#3 -> ", fms, fmi, bad, t, fms_note_mhz (fms_make_note (6, 0, 4)));
	fms_note_text (fms_make_note (3, 1, 3), t); printf ("%s\n", t);
	return bad ? 1 : 0;
}
