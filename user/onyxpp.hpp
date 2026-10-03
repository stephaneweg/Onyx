//
// onyxpp.hpp -- minimal C++ runtime support for freestanding Onyx apps.
//
// Provides operator new/delete (backed by the umm user allocator over kapi_sbrk) and
// the handful of runtime symbols g++ references when built -nostdlib with
// -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit. No STL, no
// exceptions, no RTTI -- just classes, inheritance, virtuals, new/delete. Include
// once in a C++ app (single translation unit). Global constructors run via crt0.S
// (.init_array); their destructors are NOT run (the app exits and its address space
// is reclaimed), so __cxa_atexit / atexit are accepted and ignored.
//
#ifndef ONYX_CPP_HPP
#define ONYX_CPP_HPP

typedef __SIZE_TYPE__ onyx_size_t;

// ONYX_HOSTED_NEW: a hosted program (the POSIX toolchain: libstdc++, newlib's malloc) that
// includes this header through wtk keeps libstdc++'s operator new / delete. The ones below would
// replace them for the WHOLE program (they are the app's own definitions), on a second heap that
// calls kapi_sbrk itself -- a kernel call wherever a new grows it: on an app core (kapi_core_run:
// no kernel call there) the job is stopped. Web (tools/webkit/build-web.sh) rasterises there.
#ifdef ONYX_HOSTED_NEW
#include "kapi.h"
#else
#include "umm.h"			// umm_malloc / umm_free (heap over kapi_sbrk)

// __attribute__((used)) forces emission of ALL these operators in the TU that includes
// this header (an app includes it once, in main.o) -- so a SEPARATELY-COMPILED library
// (e.g. wtk/libwtk.a) resolves operator new/delete from the app at link time, even the
// variants (like sized delete in a deleting destructor) the app never calls directly.
// A failed new never returns 0. g++ drops the "p == 0" tests after a plain new (it assumes
// new throws), and an Onyx app runs at EL1 with the kernel's RAM mapped: the writes that
// followed a failed allocation went to address 0 and up -- over the kernel's code, every core
// stopped, no crash report (the Spreadsheet's freeze). The app ends instead, with a kmsg line
// ("app: out of memory: ...") that says the size and where from (addr2line -e <app>.elf).
__attribute__ ((weak, noinline, noreturn)) void onyx_out_of_memory (onyx_size_t n, void *pFrom)
{
	char Buf[128]; unsigned k = 0;
	const char *s = "out of memory: new of ";
	while (*s) Buf[k++] = *s++;
	char d[24]; int i = 0; do { d[i++] = (char) ('0' + n % 10); n /= 10; } while (n != 0);
	while (i > 0) Buf[k++] = d[--i];
	s = " bytes failed, from 0x"; while (*s) Buf[k++] = *s++;
	unsigned long a = (unsigned long) pFrom;
	for (int b = 36; b >= 0; b -= 4) Buf[k++] = "0123456789abcdef"[(a >> b) & 15];
	s = "; the app is stopped"; while (*s) Buf[k++] = *s++;
	kapi_write (1, Buf, k);
	kapi_exit (137);
	for (;;) {}
}
inline void *onyx_new (onyx_size_t n, void *pFrom)
{
	void *p = umm_malloc (n);
	if (__builtin_expect (p == 0, 0)) onyx_out_of_memory (n, pFrom);
	return p;
}
inline __attribute__ ((used)) void *operator new      (onyx_size_t n)            { return onyx_new (n, __builtin_return_address (0)); }
inline __attribute__ ((used)) void *operator new[]    (onyx_size_t n)            { return onyx_new (n, __builtin_return_address (0)); }
inline __attribute__ ((used)) void  operator delete   (void *p) noexcept         { umm_free (p); }
inline __attribute__ ((used)) void  operator delete[] (void *p) noexcept         { umm_free (p); }
inline __attribute__ ((used)) void  operator delete   (void *p, onyx_size_t) noexcept { umm_free (p); }	// sized
inline __attribute__ ((used)) void  operator delete[] (void *p, onyx_size_t) noexcept { umm_free (p); }
#endif // !ONYX_HOSTED_NEW
#ifdef ONYX_HOSTED_NEW
#include <new>				// (placement new is libstdc++'s too: the program may include <string>, <vector>...)
#else
inline void *operator new      (onyx_size_t, void *p) noexcept { return p; }	// placement
inline void *operator new[]    (onyx_size_t, void *p) noexcept { return p; }
#endif

// These four runtime symbols need a real (emitted, address-takeable) definition, but
// they are also pulled into any SEPARATELY-COMPILED translation unit that includes this
// header transitively (e.g. a wtk library .cpp that includes bmp.hpp). Marking them weak
// lets those duplicate-but-identical definitions merge at link time instead of clashing
// with the app's main.o copy.
extern "C" {
	// Pure-virtual called (should never happen): stop the app cleanly. An abstract
	// class's vtable takes this symbol's address, so it needs a real definition.
	__attribute__ ((weak)) void __cxa_pure_virtual (void) { kapi_exit (127); for (;;) {} }
	// Static-destructor registration -- accepted and ignored (see header note).
	__attribute__ ((weak)) void *__dso_handle = 0;
	// A function-local static with a non-trivial destructor emits a real CALL to atexit
	// (we build -fno-use-cxa-atexit), so these need emitted definitions.
	__attribute__ ((weak)) int __cxa_atexit (void (*) (void *), void *, void *) { return 0; }
	__attribute__ ((weak)) int atexit (void (*) (void)) { return 0; }
}

#endif // ONYX_CPP_HPP
