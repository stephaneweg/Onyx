// host stub of Circle's CLogger for tools/tests/run_ofile_test.sh
#ifndef _circle_logger_h
#define _circle_logger_h
#include <stdio.h>
#include <stdarg.h>
enum TLogSeverity { LogPanic, LogError, LogWarning, LogNotice, LogDebug };
class CLogger
{
public:
	static CLogger *Get (void)		{ static CLogger s; return &s; }
	void Write (const char *pSource, TLogSeverity, const char *pFormat, ...)
	{
		va_list v; va_start (v, pFormat);
		printf ("  [%s] ", pSource); vprintf (pFormat, v); printf ("\n");
		va_end (v);
	}
};
#endif
