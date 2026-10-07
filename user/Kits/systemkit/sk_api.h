//
// sk_api.h -- how SystemKit's headers declare their functions.
// A program for Onyx: the library's functions, by name (SD:/lib/systemkit.so; link lib/systemkit.imp.a).
// A PC build, or SK_INLINE: the code inline in the program (the .inc beside each header) -- no shared
// library there.
//
#ifndef _sk_api_h
#define _sk_api_h
#if defined (__aarch64__) && !defined (SK_INLINE)
#ifdef __cplusplus
#define SK_API	extern "C"
#else
#define SK_API
#endif
#else
#define SK_API	static inline
#define SK_BODIES_INLINE	1
#endif
#endif
