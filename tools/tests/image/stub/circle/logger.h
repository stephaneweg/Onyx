// host stub of Circle's CLogger (tools/tests/run_image_test.sh): LoadELF's error line, kept quiet
#ifndef _circle_logger_h
#define _circle_logger_h
enum TLogSeverity { LogPanic, LogError, LogWarning, LogNotice, LogDebug };
extern const char *g_pLastLog;			// (imagetest.cpp: the last message's argument)
class CLogger
{
public:
	static CLogger *Get (void)		{ static CLogger s; return &s; }
	void Write (const char *, TLogSeverity, const char *, const char *pArg = 0)	{ g_pLastLog = pArg; }
};
#endif
