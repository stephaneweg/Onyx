//
// applaunch.h
//
// Spawn an app by folder name (apps/<name>.app/main.elf) as a new EL1 process.
// Defined in kernel.cpp; declared here so kapi.cpp's kapi_launch (called by the
// shell) can start another app. Safe to call from any cooperative task context:
// it only loads the ELF file and registers a new task -- the new address space is
// built later, in that task's own context.
//
#ifndef _kern_applaunch_h
#define _kern_applaunch_h

#include <circle/types.h>

boolean LaunchAppByName (const char *pName);

// Spawn a console process (ELF at pElfPath) with stdin/stdout streams + argv.
// Returns a CProcess* handle, or 0 on failure. Defined in kernel.cpp.
class CStream;
struct CProcess;
CProcess *SpawnProcess (const char *pElfPath, const char *pArgs,
			CStream *pStdin, CStream *pStdout);

#endif // _kern_applaunch_h
