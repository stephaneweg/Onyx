//
// buildstamp.cpp -- when and from what this kernel image was built: compiled again at every link
// (kernel/Makefile: it depends on the kernel's other objects and libraries), so the date is the
// image's, not that of the last change to one file. Read by kapi v79 kernel_info (/bin/uname) and
// the boot log.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#ifndef ONYX_BUILD_REV
#define ONYX_BUILD_REV "?"
#endif
extern const char g_BuildStamp[];
extern const char g_BuildRev[];
const char g_BuildStamp[] = __DATE__ " " __TIME__;
const char g_BuildRev[] = ONYX_BUILD_REV;		// the source's git revision, "+": with changes
