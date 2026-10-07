# CMakeLists.txt -- Skia for Onyx (third_party/skia-m154: WebKit's own copy of Skia, BSD-3): libskia.a, its
# headers (include/skia: Skia's include/ and the module headers it needs) and skia.pc into the POSIX
# sysroot. tools/ports/skia/build.sh runs it with tools/onyx-toolchain.cmake.
#
# The source list (sources.cmake) is WebKit's (Source/ThirdParty/skia/CMakeLists.txt, the WebKit commit in
# third_party/skia-m154/README.onyx) for the CPU raster back end only: WebKit's list without Ganesh
# (src/gpu/ganesh, SK_GANESH / SK_GL) and without the x86 / LoongArch optimisations (AArch64: NEON is
# compiled in, SKVX), plus what WebKit's port blocks add on Linux (the FreeType font host, the encoders,
# WebP decoding) and, instead of fontconfig, Skia's custom font managers (a directory, an empty one,
# embedded fonts) with Onyx's own over them (SkFontMgr_onyx.cpp: SD:/res/fonts, CSS family aliases,
# fallback per character). FreeType, libpng, libjpeg-turbo and libwebp come from the sysroot; HarfBuzz is
# not linked by Skia here (WebKit shapes with HarfBuzz itself and hands Skia glyph ids: SkTextBlob).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this file): Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including without
# limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
# of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
# following conditions: The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
# ANY KIND, EXPRESS OR IMPLIED.
cmake_minimum_required(VERSION 3.20)
project(onyx_skia LANGUAGES C CXX)

set(SKIA_SRC "" CACHE PATH "Skia's tree (third_party/skia-m154)")
if(NOT EXISTS "${SKIA_SRC}/include/core/SkCanvas.h")
	message(FATAL_ERROR "SKIA_SRC: no Skia at '${SKIA_SRC}'")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")
list(TRANSFORM SKIA_SOURCES PREPEND "${SKIA_SRC}/")
# + Onyx's font manager (SkFontMgr_onyx.cpp: the directory font manager with CSS aliases and fallback)
add_library(skia STATIC ${SKIA_SOURCES} "${CMAKE_CURRENT_LIST_DIR}/SkFontMgr_onyx.cpp")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SKDEPS REQUIRED freetype2 libpng16 libjpeg libwebp libwebpdemux libwebpmux)
target_include_directories(skia PRIVATE "${SKIA_SRC}" ${SKDEPS_INCLUDE_DIRS})

# as WebKit's CMakeLists, without the GPU: the public definitions (a client must see them: skia.pc)
set(SKIA_PUBLIC_DEFS
	SK_BUILD_FOR_UNIX			# Onyx's GCC does not define __unix__ (SkFeatures.h)
	SK_R32_SHIFT=16				# N32 = BGRA, the Onyx canvas' 0x00RRGGBB in memory
	SK_DISABLE_LEGACY_IMAGE_READBUFFER
	SK_DISABLE_LEGACY_INIT_DECODERS
	SK_DISABLE_LEGACY_PNG_WRITEBUFFER
	SK_DISABLE_TRACING
	SK_DISABLE_WEBGL_INTERFACE
	SK_RELEASE)
target_compile_definitions(skia PUBLIC ${SKIA_PUBLIC_DEFS})
target_compile_definitions(skia PRIVATE
	SKIA_IMPLEMENTATION=1
	SK_CODEC_DECODES_PNG
	SK_CODEC_DECODES_JPEG
	SK_CODEC_DECODES_WEBP
	SK_CODEC_ENCODES_PNG
	SK_CODEC_ENCODES_JPEG
	SK_CODEC_ENCODES_WEBP
	SK_GAMMA_APPLY_TO_A8
	SK_TYPEFACE_FACTORY_FREETYPE
	SK_FONTMGR_FREETYPE_DIRECTORY_AVAILABLE
	SK_FONTMGR_FREETYPE_EMBEDDED_AVAILABLE
	SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE
	SKCMS_DISABLE_HSW
	SKCMS_DISABLE_SKX)
target_compile_options(skia PRIVATE -O2 -ffp-contract=off -fstrict-aliasing -w)

install(TARGETS skia ARCHIVE DESTINATION lib)
# the headers: Skia's include/ under include/skia (Skia's own sources and headers say "include/core/...",
# so a client adds -I<sysroot>/include/skia), and the module headers its public headers include
install(DIRECTORY "${SKIA_SRC}/include" DESTINATION include/skia FILES_MATCHING PATTERN "*.h")
install(FILES "${CMAKE_CURRENT_LIST_DIR}/SkFontMgr_onyx.h" DESTINATION include/skia/include/ports)
install(FILES "${SKIA_SRC}/modules/skcms/skcms.h" DESTINATION include/skia/modules/skcms)
if(EXISTS "${SKIA_SRC}/modules/skcms/src/skcms_public.h")
	install(FILES "${SKIA_SRC}/modules/skcms/src/skcms_public.h" DESTINATION include/skia/modules/skcms/src)
endif()

list(TRANSFORM SKIA_PUBLIC_DEFS PREPEND "-D" OUTPUT_VARIABLE SKIA_PC_DEFS)
list(JOIN SKIA_PC_DEFS " " SKIA_PC_DEFS)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/skia.pc" "prefix=${CMAKE_INSTALL_PREFIX}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: skia
Description: Skia 2D graphics, the CPU raster back end (WebKit's copy, milestone 154)
Version: 154
Requires.private: freetype2 libpng16 libjpeg libwebp libwebpdemux libwebpmux
Libs: -L\${libdir} -lskia
Cflags: -I\${includedir}/skia ${SKIA_PC_DEFS}
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/skia.pc" DESTINATION lib/pkgconfig)
