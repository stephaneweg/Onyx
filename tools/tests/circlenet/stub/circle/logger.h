// Host stub (tools/tests/circlenet): the log lines to stderr when CIRCLENET_LOG is set.
#ifndef _circle_logger_h
#define _circle_logger_h
#include <circle/types.h>
enum TLogSeverity { LogPanic, LogError, LogWarning, LogNotice, LogDebug };
class CLogger
{
public:
	static CLogger *Get (void);
	void Write (const char *pSource, TLogSeverity Severity, const char *pMessage, ...);
};
#define LOGMODULE(name)		static const char From[] = name
#define LOGPANIC(...)		CLogger::Get ()->Write (From, LogPanic, __VA_ARGS__)
#define LOGERR(...)		CLogger::Get ()->Write (From, LogError, __VA_ARGS__)
#define LOGWARN(...)		CLogger::Get ()->Write (From, LogWarning, __VA_ARGS__)
#define LOGNOTE(...)		CLogger::Get ()->Write (From, LogNotice, __VA_ARGS__)
#define LOGDBG(...)		CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#endif
