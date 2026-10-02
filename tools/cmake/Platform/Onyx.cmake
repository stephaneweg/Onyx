# Platform/Onyx.cmake -- CMake's description of the Onyx system (tools/onyx-toolchain.cmake sets
# CMAKE_SYSTEM_NAME Onyx and puts this folder on CMAKE_MODULE_PATH).
#
# A Unix-like target with static programs only: no shared libraries, no dlopen, no rpath.
# `if(ONYX)` tells a project's CMake that it builds for Onyx.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

set(UNIX 1)
set(ONYX 1)

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)

set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".a")		# (never made: TARGET_SUPPORTS_SHARED_LIBS is off)
set(CMAKE_EXECUTABLE_SUFFIX "")		# (on the card: no extension)
set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(CMAKE_DL_LIBS "")
set(CMAKE_LINK_LIBRARY_SUFFIX "")

set(CMAKE_C_OUTPUT_EXTENSION ".o")
set(CMAKE_CXX_OUTPUT_EXTENSION ".o")
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "")
set(CMAKE_SHARED_LIBRARY_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")
set(CMAKE_C_COMPILE_OPTIONS_PIE "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIE "")
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

