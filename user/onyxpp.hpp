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

#include "umm.h"			// umm_malloc / umm_free (heap over kapi_sbrk)

typedef __SIZE_TYPE__ onyx_size_t;

// __attribute__((used)) forces emission of ALL these operators in the TU that includes
// this header (an app includes it once, in main.o) -- so a SEPARATELY-COMPILED library
// (e.g. wtk/libwtk.a) resolves operator new/delete from the app at link time, even the
// variants (like sized delete in a deleting destructor) the app never calls directly.
inline __attribute__ ((used)) void *operator new      (onyx_size_t n)            { return umm_malloc (n); }
inline __attribute__ ((used)) void *operator new[]    (onyx_size_t n)            { return umm_malloc (n); }
inline __attribute__ ((used)) void  operator delete   (void *p) noexcept         { umm_free (p); }
inline __attribute__ ((used)) void  operator delete[] (void *p) noexcept         { umm_free (p); }
inline __attribute__ ((used)) void  operator delete   (void *p, onyx_size_t) noexcept { umm_free (p); }	// sized
inline __attribute__ ((used)) void  operator delete[] (void *p, onyx_size_t) noexcept { umm_free (p); }
inline void *operator new      (onyx_size_t, void *p) noexcept { return p; }	// placement
inline void *operator new[]    (onyx_size_t, void *p) noexcept { return p; }

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
