//
// applaunch.h
//
// Spawn an app by folder name (apps/<name>.app/main) as a new process (at EL0, kern/el0.h).
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
struct TProcInfo;
// pInfo (v75, kern/procx.h: spawn_ex's argv / environment) is the child's from the call on, also
// on a failure (freed); 0: made from pElfPath + pArgs and the caller's environment.
CProcess *SpawnProcess (const char *pElfPath, const char *pArgs,
			CStream *pStdin, CStream *pStdout, const char *pCwd = 0,
			unsigned nParentPid = 0, TProcInfo *pInfo = 0);

// Run an ELF by absolute path with an argv string, fire-and-forget (no stdio, no
// wait handle). Task name is derived from the path. Defined in kernel.cpp.
boolean ExecPath (const char *pElfPath, const char *pArgs, const char *pName = 0);	// pName: the process' name (0: from the path)

// (v77) Preload the program at pCanonPath (its canonical path, kern/image.h): a kernel task loads
// its image and pins it; returns at once. Kept already: 0, nothing done. -> 0 / -KAPI_ENOENT (no
// such file) / -KAPI_ENOMEM. Defined in kernel.cpp (kapi_image_preload).
int ProgramPreload (const char *pCanonPath);

// (v83) The shared library at pCanonPath (its canonical path) mapped in pAS -- loaded now by the
// calling task if no process has it and it is not preloaded -> 0 and *pTable (its export table in
// pAS), or -KAPI_E* (-KAPI_ENOTSUP: its table's version is below nMinVersion). Defined in
// kernel.cpp (kapi_lib_open).
class CAddressSpace;
int LibraryOpen (const char *pCanonPath, unsigned nMinVersion, CAddressSpace *pAS, u64 *pTable);

// (v97) The same, then the library found under pCanonAlias too, for nOwner (the graphics server's pid:
// the caller checked the role) -- kern/image.h's alias; asked again (or an orphaned alias taken over
// with the same real path): the image the alias names, whatever its file did since. -> 0 and *pTable,
// or -KAPI_E* (ImageAliasAllowed's, ImageSetAlias's, LibraryOpen's). Defined in kernel.cpp
// (kapi_lib_open_as).
int LibraryOpenAs (const char *pCanonPath, const char *pCanonAlias, unsigned nMinVersion, CAddressSpace *pAS,
		   u64 *pTable, unsigned nOwner);

// Keyboard layout control (defined in kernel.cpp): switch the live keyboard to a
// compiled-in country map and read the current layout name. Declared here (a plain
// C++ header) so sys/kapi.cpp sees C++ linkage, matching the kernel.cpp definitions.
boolean KernelSetKeyMap (const char *pName);
boolean KernelSetKeyMapData (const char *pName, const void *pData, unsigned nLen);	// .kmap file
const char *KernelGetKeyMap (void);
boolean KernelKeyboardReady (void);		// is a USB keyboard attached? (kapi_kbd_ready)
struct kapi_pad;
boolean KernelPadState (int nIndex, struct kapi_pad *pOut);	// a USB gamepad's raw state (kapi_pad_state)
struct kapi_midi_event;
int KernelMidiRead (struct kapi_midi_event *pEv, int nMax);	// queued USB MIDI events (kapi_midi_read)
int KernelMidiDevices (void);					// MIDI devices attached

// Verbose-logging flag (defined in kernel.cpp): gates kernel lifecycle logs. The
// extern lets other TUs gate their own logs with `if (g_bVerbose)`.
extern boolean g_bVerbose;
void KernelSetVerbose (boolean bOn);
boolean KernelGetVerbose (void);

#endif // _kern_applaunch_h
