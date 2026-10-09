//
// nds/nds_jit.cpp -- the JIT: ARM / Thumb translated to AArch64 (filled in by D4).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

bool jitAvailable () { return false; }
bool jitPageHasCode (Machine *, const u8 *) { return false; }
void jitRun (Machine *m, Arm &c) { (void) m; c.run (); }
void jitInvalidate (Machine *, int, u32) {}
void jitFlushAll (Machine *) {}
bool Machine::jitEnable (void *(*codeAlloc) (u32 size)) { (void) codeAlloc; return false; }

} // namespace nds
