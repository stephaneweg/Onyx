// pc/macOS/onyxmac.h -- included first in every C++ file of the macOS build (build.sh: -include): the
// C++ runtime of macOS (its operator new / delete, atexit...) stands for Onyx's onyxpp.hpp, whose guard
// is taken here so that no Onyx source brings it in (as pc/Koton/onyxwin.h on Windows).
#ifndef ONYX_CPP_HPP
#define ONYX_CPP_HPP
#include <new>
#endif
