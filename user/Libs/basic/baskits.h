//
// basic/baskits.h -- Onyx BASIC's kits on Onyx (#import): where the compiler reads a kit's description
// and how the runtime opens the kit. The portable core (bas.h) only knows bas::setKitSource and
// bas::Host::kitOpen; a program that compiles or runs BASIC on Onyx gives it these (baskits.cpp).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _baskits_h
#define _baskits_h

namespace bas {

// The text of SD:/lib/<name>.bi (tools/kitbi/kitbi.py) as a new[] buffer, its length; 0: no such kit.
// For bas::setKitSource.
char *onyxKitSource (const char *name, int *len);
// SD:/lib/<name>.so opened for this program and started, its table having at least minVersion entries
// -> its entries; 0, and `why` says it. Opened already (the program is linked with it): the same table.
// For bas::Host::kitOpen.
void *const *onyxKitOpen (const char *name, int minVersion, char *why, int cap);

} // namespace bas

#endif
