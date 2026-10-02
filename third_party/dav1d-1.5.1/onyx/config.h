/*
 * third_party/dav1d-1.5.1/onyx/config.h -- dav1d's config.h for Onyx's three builds, written by
 * hand in place of its meson configure (README.onyx): 8-bit only, no logging.
 *   - the Pi (aarch64-none-elf + newlib, __ONYX_DAV1D_PI): the AArch64 NEON assembly, no
 *     threads (onyx/pthread/pthread.h), memalign;
 *   - the PC bench (x86-64 Linux) and Windows (MinGW): the C code alone (no nasm).
 */
#pragma once

#if defined(__aarch64__)
#define ARCH_AARCH64 1
#define ARCH_X86 0
#define ARCH_X86_32 0
#define ARCH_X86_64 0
#define HAVE_ASM 1
#define HAVE_AS_FUNC 0
#define HAVE_AS_ARCH_DIRECTIVE 1
#define AS_ARCH_LEVEL armv8-a
#define HAVE_AS_ARCHEXT_DOTPROD_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_I8MM_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_SVE_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_SVE2_DIRECTIVE 0
#else
#define ARCH_AARCH64 0
#define ARCH_X86 0	/* (x86's assembly is nasm's: not built; 0 keeps the x86 init code out) */
#define ARCH_X86_32 0
#define ARCH_X86_64 0
#define HAVE_ASM 0
#define HAVE_AS_FUNC 0
#define HAVE_AS_ARCH_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_DOTPROD_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_I8MM_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_SVE_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_SVE2_DIRECTIVE 0
#endif
#define ARCH_ARM 0
#define ARCH_LOONGARCH 0
#define ARCH_LOONGARCH32 0
#define ARCH_LOONGARCH64 0
#define ARCH_PPC64LE 0
#define ARCH_RISCV 0
#define ARCH_RV32 0
#define ARCH_RV64 0

#define CONFIG_16BPC 0
#define CONFIG_8BPC 1
#define CONFIG_LOG 0
#define CONFIG_MACOS_KPERF 0
#define ENDIANNESS_BIG 0
#define TRIM_DSP_FUNCTIONS 1
#define HAVE_C11_GENERIC 1

/* the Cortex-A72 (ARMv8.0): no dot product, no i8mm, no SVE */
#define HAVE_DOTPROD 0
#define HAVE_I8MM 0
#define HAVE_SVE 0
#define HAVE_SVE2 0

#define HAVE_DLSYM 0
#define HAVE_ELF_AUX_INFO 0
#define HAVE_GETAUXVAL 0
#define HAVE_PTHREAD_GETAFFINITY_NP 0
#define HAVE_PTHREAD_SETAFFINITY_NP 0
#define HAVE_PTHREAD_NP_H 0
#define HAVE_PTHREAD_SETNAME_NP 0
#define HAVE_PTHREAD_SET_NAME_NP 0
#define HAVE_SYS_TYPES_H 1

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef __USE_MINGW_ANSI_STDIO
#define __USE_MINGW_ANSI_STDIO 1
#endif
#define HAVE_IO_H 1
#define HAVE_UNISTD_H 0
#define HAVE_CLOCK_GETTIME 0
#define HAVE_POSIX_MEMALIGN 0
#define HAVE_MEMALIGN 0
#define HAVE_ALIGNED_ALLOC 0
#elif defined(__ONYX_DAV1D_PI)
/* newlib: memalign (freed by free), no sysconf (the thread count: always 1 here) */
#define HAVE_IO_H 0
#define HAVE_UNISTD_H 1
#define HAVE_CLOCK_GETTIME 0
#define HAVE_POSIX_MEMALIGN 0
#define HAVE_MEMALIGN 1
#define HAVE_ALIGNED_ALLOC 0
#define sysconf onyx_dav1d_sysconf
#else
#define HAVE_IO_H 0
#define HAVE_UNISTD_H 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_MEMALIGN 1
#define HAVE_ALIGNED_ALLOC 1
#endif
