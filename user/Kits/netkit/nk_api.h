//
// nk_api.h -- how NetKit's headers declare their functions.
// A program for Onyx: the library's functions, by name (SD:/lib/netkit.so; link lib/netkit.imp.a).
// A PC build, or NK_INLINE: the code inline in the program (the .inc beside each header) -- no shared
// library there.
//
#ifndef _nk_api_h
#define _nk_api_h
#if defined (__aarch64__) && !defined (NK_INLINE)
#ifdef __cplusplus
#define NK_API	extern "C"
#else
#define NK_API
#endif
#else
#define NK_API	static inline
#define NK_BODIES_INLINE	1
#endif
#endif
