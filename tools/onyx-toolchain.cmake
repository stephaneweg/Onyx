# onyx-toolchain.cmake -- CMake toolchain file: build third-party code for Onyx, against the POSIX
# sysroot (libonyxposix: docs/03 §5.4 and "Building a third-party library for Onyx").
#
#   make -C user/libc/posix install                    # the sysroot (default out/sysroot)
#   cmake -S <src> -B <build> -DCMAKE_TOOLCHAIN_FILE=<onyx>/tools/onyx-toolchain.cmake \
#         -DBUILD_SHARED_LIBS=OFF [-DONYX_SYSROOT=<dir>]
#
# What it sets: the system "Onyx" (tools/cmake/Platform/Onyx.cmake: UNIX, static only), the
# aarch64 compilers (ONYX_TOOLCHAIN_PREFIX, default aarch64-none-elf- -- WP-TC's toolchain:
# aarch64-onyx-elf-), the flags (Cortex-A72, sections for --gc-sections, the sysroot's headers
# first: -isystem), the link (onyx.specs: crt0posix, onyx-posix.ld, libonyxposix + newlib), and
# the search paths: libraries, headers and CMake packages from the sysroot only, programs from
# the host. Executables link and are Onyx ELFs (try_run cannot run them: answer its questions
# with cache variables).
#
# Note: CMAKE_SYSROOT is NOT set. The interim toolchain (aarch64-none-elf) keeps newlib in its own
# tree; a --sysroot would hide it. The sysroot here is an overlay (-isystem, -L, the specs).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

set(CMAKE_SYSTEM_NAME Onyx)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSTEM_VERSION 75)		# the kapi ABI version the sysroot targets
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# The sysroot: -DONYX_SYSROOT, else $ONYX_SYSROOT, else <onyx>/out/sysroot.
if(NOT ONYX_SYSROOT)
	if(DEFINED ENV{ONYX_SYSROOT})
		set(ONYX_SYSROOT "$ENV{ONYX_SYSROOT}")
	else()
		get_filename_component(ONYX_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../out/sysroot" ABSOLUTE)
	endif()
endif()
set(ONYX_SYSROOT "${ONYX_SYSROOT}" CACHE PATH "The Onyx POSIX sysroot (make -C user/libc/posix install)")
if(NOT EXISTS "${ONYX_SYSROOT}/lib/onyx.specs")
	message(FATAL_ERROR "No Onyx sysroot at ${ONYX_SYSROOT}: run  make -C user/libc/posix install SYSROOT=${ONYX_SYSROOT}")
endif()
# (try_compile projects get these too)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ONYX_SYSROOT ONYX_TOOLCHAIN_PREFIX)

# The compilers: ONYX_TOOLCHAIN_PREFIX (on the PATH, or the Arm GNU toolchain in /opt/toolchains)
if(NOT ONYX_TOOLCHAIN_PREFIX)
	if(DEFINED ENV{ONYX_TOOLCHAIN_PREFIX})
		set(ONYX_TOOLCHAIN_PREFIX "$ENV{ONYX_TOOLCHAIN_PREFIX}")
	else()
		set(ONYX_TOOLCHAIN_PREFIX "aarch64-none-elf-")
	endif()
endif()
find_program(ONYX_CC "${ONYX_TOOLCHAIN_PREFIX}gcc"
	PATHS /opt/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin
	      /opt/toolchains/aarch64-onyx-elf-14.2/bin NO_CMAKE_FIND_ROOT_PATH)
if(NOT ONYX_CC)
	message(FATAL_ERROR "${ONYX_TOOLCHAIN_PREFIX}gcc not found: put the toolchain's bin on the PATH")
endif()
get_filename_component(ONYX_TOOLCHAIN_BIN "${ONYX_CC}" DIRECTORY)
set(CMAKE_C_COMPILER "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_AR "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}strip" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${ONYX_TOOLCHAIN_BIN}/${ONYX_TOOLCHAIN_PREFIX}objcopy" CACHE FILEPATH "")

# The flags (the _INIT values: a project's own flags are added to them)
set(ONYX_COMMON_FLAGS "-mcpu=cortex-a72 -fno-pic -fno-pie -ffunction-sections -fdata-sections -isystem ${ONYX_SYSROOT}/include -DFD_SETSIZE=1024")
set(CMAKE_C_FLAGS_INIT "${ONYX_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ONYX_COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-a72")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=${ONYX_SYSROOT}/lib/onyx.specs -L${ONYX_SYSROOT}/lib")

# Search: the sysroot for libraries, headers, packages; the host for programs
set(CMAKE_FIND_ROOT_PATH "${ONYX_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_PREFIX_PATH "${ONYX_SYSROOT}")
set(CMAKE_INSTALL_PREFIX "${ONYX_SYSROOT}" CACHE PATH "Install into the Onyx sysroot")

# pkg-config: the sysroot's .pc files only
set(ENV{PKG_CONFIG_LIBDIR} "${ONYX_SYSROOT}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Onyx programs are static")
