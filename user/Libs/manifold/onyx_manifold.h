/*
 * onyx_manifold.h -- what Manifold (third_party/manifold-3.5.4) and Clipper2 need to build with the
 * bare-metal toolchain; included before every one of their files, and before <manifold/...> in a program
 * (user/Makefile: MF_INC).
 *
 * - The toolchain's libstdc++ has no threads: <mutex> gives lock_guard but no mutex. Manifold's headers
 *   name std::mutex, std::recursive_mutex and std::scoped_lock although this build is sequential
 *   (MANIFOLD_PAR=-1): empty ones here. A program that calls Manifold from several threads must lock
 *   around it itself.
 * - newlib's <ctype.h> defines _U _L _N _S _P _C _X _B; Manifold uses such names (svd.h). <ios> first --
 *   libstdc++'s ctype tables read them --, then they are removed.
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#pragma once
#ifdef __cplusplus
#include <ios>
#include <mutex>
#if !defined(_GLIBCXX_HAS_GTHREADS)
namespace std {
struct mutex { void lock () {} void unlock () {} bool try_lock () { return true; } };
struct recursive_mutex { void lock () {} void unlock () {} bool try_lock () { return true; } };
template <class... M> struct scoped_lock { explicit scoped_lock (M &...) {} };
}
#endif
#undef _U
#undef _L
#undef _N
#undef _S
#undef _P
#undef _C
#undef _X
#undef _B
#endif
