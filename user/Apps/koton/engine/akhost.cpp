//
// akhost.cpp -- Koton on a PC (Koton for Windows, the tests, the screenshots): no shared library there,
// so AudioKit's pure part (the mixing, the soft limiter, the notes: audiokit/akmix.cpp) is compiled
// into the program. On Onyx it comes from SD:/lib/audiokit.so and this file is empty.
//
#ifndef __aarch64__
#include "../../../audiokit/akmix.cpp"
#endif
