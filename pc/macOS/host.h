//
// pc/macOS/host.h -- between the two halves of the Onyx kernel's table on a host: hostkapi.cpp (POSIX:
// files, time, processes, threads; built on macOS and, for the checks, on Linux) and the screen's half
// (cocoa.mm on macOS; headless.cpp for the checks on Linux): the window, its events, its menu bar, the
// clipboard.
//
#ifndef _onyx_mac_host_h
#define _onyx_mac_host_h

#include <string>
#include "appkit/appkit.h"

// An Onyx path -> the host's ("" when it names nothing on the host). The card ("SD:/...") is two
// folders: the writable one (the user's, ONYX_SD) over the read-only one in the application's bundle
// (ONYX_SD_BASE): read from the first that has the file, written in the first.
std::string host_path (const char *onyx, bool forWrite = false);
// A host path -> the Onyx one the apps see ("/Users/me/a.ledger" -> "MAC:/Users/me/a.ledger").
std::string onyx_path (const std::string &host);
// A file or a folder shown by macOS (`open`: its own application, the Finder) -> false: could not.
bool host_open (const std::string &hostPath);
// The ticks of the kapi (10 ms).
unsigned host_ticks ();

// the screen's half: fills the window's, the events', the menu's and the clipboard's slots
void gui_setup (TKApiTable *T);
// a fatal message (the screen's half shows it if it can), then the process ends
void gui_fatal (const char *msg);

#endif
