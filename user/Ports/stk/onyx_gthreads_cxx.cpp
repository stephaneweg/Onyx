//
// onyx_gthreads_cxx.cpp -- the members of std::thread and std::condition_variable that libstdc++
// keeps in its library (src/c++11/thread.cc, condition_variable.cc) and that the toolchain's
// --disable-threads libstdc++.a does not have, written for compat/bits/gthr-default.h (Onyx kapi
// threads). Built with the same flags as the rest of the port (_GLIBCXX_HAS_GTHREADS, -Icompat).
// And std::call_once's helpers for a target without TLS (src/c++11/mutex.cc: the callable in a
// global std::function under a global mutex).
//
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <system_error>
#include <cerrno>

namespace std _GLIBCXX_VISIBILITY(default)
{
	thread::_State::~_State () = default;

	void thread::_M_start_thread (_State_ptr state, void (*) ())
	{
		// the new thread's body (a lambda: a member's context may name the private _State)
		void *(*run) (void *) = [] (void *p) -> void * {
			_State_ptr owned (static_cast<_State *> (p));
			owned->_M_run ();
			return nullptr;
		};
		int e = __gthread_create (&_M_id._M_thread, run, state.get ());
		if (e)
			__throw_system_error (e);
		state.release ();		// the new thread owns it now
	}

	void thread::join ()
	{
		if (!joinable ())
			__throw_system_error (EINVAL);
		int e = __gthread_join (_M_id._M_thread, nullptr);
		if (e)
			__throw_system_error (e);
		_M_id = id ();
	}

	void thread::detach ()
	{
		if (!joinable ())
			__throw_system_error (EINVAL);
		__gthread_detach (_M_id._M_thread);
		_M_id = id ();
	}

	unsigned int thread::hardware_concurrency () noexcept
	{
		return 1;			// every process's threads run on core 0 (docs/03 §5.2)
	}

	condition_variable::condition_variable () noexcept = default;
	condition_variable::~condition_variable () noexcept = default;

	void condition_variable::wait (unique_lock<mutex> &lock)
	{
		_M_cond.wait (*lock.mutex ());
	}

	void condition_variable::notify_one () noexcept
	{
		_M_cond.notify_one ();
	}

	void condition_variable::notify_all () noexcept
	{
		_M_cond.notify_all ();
	}

	// ---- std::call_once without TLS (libstdc++'s mutex.cc) ----
	function<void ()> __once_functor;
	static mutex s_onceMutex;			// constexpr-constructed: no guard needed
	static unique_lock<mutex> *s_onceLock;

	mutex &__get_once_mutex () { return s_onceMutex; }
	void __set_once_functor_lock_ptr (unique_lock<mutex> *p) { s_onceLock = p; }
}

extern "C" void __once_proxy (void)
{
	std::function<void ()> call = std::move (std::__once_functor);
	if (std::unique_lock<std::mutex> *l = std::s_onceLock)
	{
		l->unlock ();			// other call_once's may start now
		std::s_onceLock = nullptr;
	}
	call ();
}
