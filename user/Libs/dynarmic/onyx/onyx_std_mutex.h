//
// onyx_std_mutex.h -- std::mutex for a libstdc++ built without threads (the bare-metal toolchain's: newlib, no
// gthreads -- <mutex> then has lock_guard and unique_lock but no mutex). Dynarmic keeps one around its cache
// invalidations. A spin lock: right between an emulator's cores too, and never held long. Included before
// every source of Dynarmic's build (-include).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef ONYX_STD_MUTEX_H
#define ONYX_STD_MUTEX_H
#ifdef __cplusplus
#include <atomic>
#include <mutex>
#ifndef _GLIBCXX_HAS_GTHREADS
namespace std {
class mutex
{
public:
	constexpr mutex () noexcept = default;
	mutex (const mutex &) = delete;
	mutex &operator= (const mutex &) = delete;
	void lock () noexcept { while (m_held.test_and_set (memory_order_acquire)) { } }
	bool try_lock () noexcept { return !m_held.test_and_set (memory_order_acquire); }
	void unlock () noexcept { m_held.clear (memory_order_release); }
private:
	atomic_flag m_held = ATOMIC_FLAG_INIT;
};
}
#endif
#endif
#endif
