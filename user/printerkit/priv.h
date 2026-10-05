//
// printerkit/priv.h -- inside the print library (printerkit/printerkit.cpp, printerkit/dialog.cpp): not for the programs.
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (the notice: printerkit/printerkit.h).
//
#ifndef ONYX_PRINT_PRIV_H
#define ONYX_PRINT_PRIV_H

#include "printerkit/printers.h"

bool print__need_ft ();			// FreeType / uikit opened (their import stubs usable) -> false: not installed
bool print__need_uikit ();
void print__app_name (char *out, int cap);
// a request to printd (started if it does not run), its answer -> false: no answer in time
bool print__request (int type, PdReq &rq, void *answer, unsigned cap, unsigned *got, unsigned wait_ms);

#endif
