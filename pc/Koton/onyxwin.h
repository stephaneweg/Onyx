// pc/Koton/onyxwin.h -- included first in every C++ file of the Windows build (build.sh: -include): the
// C++ runtime of Windows (its operator new / delete, atexit...) stands for Onyx's onyxpp.hpp, whose guard
// is taken here so that no Onyx source brings it in.
#ifndef ONYX_CPP_HPP
#define ONYX_CPP_HPP
#include <new>
#endif
