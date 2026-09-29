//
// kbase.h -- Koton's small C++ base: a growable array (Vec), an owned string (Str), .NET's seeded
// Random (so the generators draw exactly the numbers Koton for Windows draws: same seed, same
// music), and a few integer helpers. No STL (Onyx apps build with -fno-exceptions -fno-rtti and
// no libstdc++ containers); allocation through new / delete (newlib's malloc in the app, the
// host's under AddressSanitizer in the PC tests).
//
#ifndef _koton_kbase_h
#define _koton_kbase_h

#include <stddef.h>
#include <string.h>
#ifndef ONYX_CPP_HPP
#include <new>			// (an Onyx app's onyxpp.hpp gives placement new itself)
#endif

namespace kt {

template <class T> struct RemRef { typedef T type; };
template <class T> struct RemRef<T &> { typedef T type; };
template <class T> struct RemRef<T &&> { typedef T type; };
template <class T> inline typename RemRef<T>::type &&move (T &&t) { return static_cast<typename RemRef<T>::type &&> (t); }
template <class T> inline void swap (T &a, T &b) { T t (move (a)); a = move (b); b = move (t); }

inline int imin (int a, int b) { return a < b ? a : b; }
inline int imax (int a, int b) { return a > b ? a : b; }
inline int iclamp (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int iabs (int v) { return v < 0 ? -v : v; }
inline int imod (int v, int m) { return ((v % m) + m) % m; }		// ((v % m) + m) % m, as the C# does
inline double dmin (double a, double b) { return a < b ? a : b; }
inline double dmax (double a, double b) { return a > b ? a : b; }
inline double dabs (double v) { return v < 0 ? -v : v; }
// Math.Round (banker's rounding: .NET's default MidpointRounding.ToEven) -> int
inline int iround (double v)
{
	double f = (double) (long long) v;			// truncation toward 0
	if (v < 0 && f != v) f -= 1;				// floor
	double r = v - f;
	long long n = (long long) f;
	if (r > 0.5 || (r == 0.5 && (n & 1))) n++;
	return (int) n;
}
inline int ifloor (double v) { long long n = (long long) v; if (v < 0 && (double) n != v) n--; return (int) n; }
inline int iceil (double v) { long long n = (long long) v; if (v > 0 && (double) n != v) n++; return (int) n; }

// ---- Vec: a growable array --------------------------------------------------------------------------------
template <class T>
class Vec
{
	T *m_p; int m_n, m_cap;
	void grow (int need)
	{
		if (need <= m_cap) return;
		int nc = m_cap ? m_cap * 2 : 8;
		while (nc < need && nc < (1 << 28)) nc *= 2;
		if (nc < need || nc > (1 << 28)) nc = need < (1 << 28) ? need : (1 << 28);	// (a bound the compiler can see)
		T *np = (T *) ::operator new (sizeof (T) * (size_t) nc);
		for (int i = 0; i < m_n; i++) { new (np + i) T (move (m_p[i])); m_p[i].~T (); }
		::operator delete (m_p);
		m_p = np; m_cap = nc;
	}
public:
	Vec () : m_p (0), m_n (0), m_cap (0) {}
	Vec (const Vec &o) : m_p (0), m_n (0), m_cap (0) { reserve (o.m_n); for (int i = 0; i < o.m_n; i++) new (m_p + i) T (o.m_p[i]); m_n = o.m_n; }
	Vec (Vec &&o) : m_p (o.m_p), m_n (o.m_n), m_cap (o.m_cap) { o.m_p = 0; o.m_n = o.m_cap = 0; }
	~Vec () { clear (); ::operator delete (m_p); }
	Vec &operator= (const Vec &o) { if (this != &o) { clear (); reserve (o.m_n); for (int i = 0; i < o.m_n; i++) new (m_p + i) T (o.m_p[i]); m_n = o.m_n; } return *this; }
	Vec &operator= (Vec &&o) { if (this != &o) { clear (); ::operator delete (m_p); m_p = o.m_p; m_n = o.m_n; m_cap = o.m_cap; o.m_p = 0; o.m_n = o.m_cap = 0; } return *this; }

	int size () const { return m_n; }
	bool empty () const { return m_n == 0; }
	T *data () { return m_p; }
	const T *data () const { return m_p; }
	T &operator[] (int i) { return m_p[i]; }
	const T &operator[] (int i) const { return m_p[i]; }
	T &back () { return m_p[m_n - 1]; }
	const T &back () const { return m_p[m_n - 1]; }
	T *begin () { return m_p; }
	T *end () { return m_p + m_n; }
	const T *begin () const { return m_p; }
	const T *end () const { return m_p + m_n; }

	void reserve (int n) { grow (n); }
	void push (const T &v) { if (m_n == m_cap) { T tmp (v); grow (m_n + 1); new (m_p + m_n) T (move (tmp)); } else new (m_p + m_n) T (v); m_n++; }
	void push (T &&v) { if (m_n == m_cap) { T tmp (move (v)); grow (m_n + 1); new (m_p + m_n) T (move (tmp)); } else new (m_p + m_n) T (move (v)); m_n++; }
	T &add () { grow (m_n + 1); new (m_p + m_n) T (); return m_p[m_n++]; }
	void pop () { if (m_n) m_p[--m_n].~T (); }
	void clear () { for (int i = 0; i < m_n; i++) m_p[i].~T (); m_n = 0; }
	void resize (int n) { if (n < m_n) { for (int i = n; i < m_n; i++) m_p[i].~T (); m_n = n; } else { grow (n); for (int i = m_n; i < n; i++) new (m_p + i) T (); m_n = n; } }
	void resize (int n, const T &v) { int o = m_n; resize (n); for (int i = o; i < n; i++) m_p[i] = v; }
	void insert (int at, const T &v)
	{
		T tmp (v);
		grow (m_n + 1);
		if (at < 0) at = 0;
		if (at > m_n) at = m_n;
		if (at == m_n) { new (m_p + m_n) T (move (tmp)); m_n++; return; }
		new (m_p + m_n) T (move (m_p[m_n - 1]));
		for (int i = m_n - 1; i > at; i--) m_p[i] = move (m_p[i - 1]);
		m_p[at] = move (tmp);
		m_n++;
	}
	void removeAt (int at)
	{
		if (at < 0 || at >= m_n) return;
		for (int i = at; i + 1 < m_n; i++) m_p[i] = move (m_p[i + 1]);
		m_p[--m_n].~T ();
	}
	void append (const Vec &o) { reserve (m_n + o.m_n); for (int i = 0; i < o.m_n; i++) push (o.m_p[i]); }
	int indexOf (const T &v) const { for (int i = 0; i < m_n; i++) if (m_p[i] == v) return i; return -1; }
	bool contains (const T &v) const { return indexOf (v) >= 0; }

	// a STABLE sort (insertion for small arrays, merge otherwise): List.Sort in .NET is unstable, but
	// every Koton comparison that matters is total, so stable gives the same order
	template <class Less> void sort (Less less)
	{
		if (m_n < 2) return;
		if (m_n <= 24)
		{
			for (int i = 1; i < m_n; i++)
			{
				T v (move (m_p[i])); int j = i;
				while (j > 0 && less (v, m_p[j - 1])) { m_p[j] = move (m_p[j - 1]); j--; }
				m_p[j] = move (v);
			}
			return;
		}
		T *tmp = (T *) ::operator new (sizeof (T) * (size_t) m_n);
		for (int i = 0; i < m_n; i++) new (tmp + i) T ();
		msort (m_p, tmp, 0, m_n, less);
		for (int i = 0; i < m_n; i++) tmp[i].~T ();
		::operator delete (tmp);
	}
private:
	template <class Less> static void msort (T *a, T *tmp, int lo, int hi, Less &less)
	{
		if (hi - lo < 2) return;
		int mid = (lo + hi) / 2;
		msort (a, tmp, lo, mid, less); msort (a, tmp, mid, hi, less);
		int i = lo, j = mid, k = lo;
		while (i < mid && j < hi) tmp[k++] = less (a[j], a[i]) ? move (a[j++]) : move (a[i++]);
		while (i < mid) tmp[k++] = move (a[i++]);
		while (j < hi) tmp[k++] = move (a[j++]);
		for (k = lo; k < hi; k++) a[k] = move (tmp[k]);
	}
};

// ---- Str: an owned, NUL-terminated string -----------------------------------------------------------------
class Str
{
	char *m_s;
public:
	Str () : m_s (0) {}
	Str (const char *s) : m_s (0) { set (s); }
	Str (const char *s, int n) : m_s (0) { set (s, n); }
	Str (const Str &o) : m_s (0) { set (o.m_s); }
	Str (Str &&o) : m_s (o.m_s) { o.m_s = 0; }
	~Str () { delete [] m_s; }
	Str &operator= (const Str &o) { if (this != &o) set (o.m_s); return *this; }
	Str &operator= (Str &&o) { if (this != &o) { delete [] m_s; m_s = o.m_s; o.m_s = 0; } return *this; }
	Str &operator= (const char *s) { set (s); return *this; }
	void set (const char *s) { set (s, s ? (int) strlen (s) : 0); }
	void set (const char *s, int n)
	{
		if (s && s >= m_s && m_s && s < m_s + strlen (m_s) + 1) { Str t (s, n); *this = move (t); return; }
		delete [] m_s; m_s = 0;
		if (!s) return;
		m_s = new char[n + 1]; memcpy (m_s, s, n); m_s[n] = 0;
	}
	const char *c () const { return m_s ? m_s : ""; }
	operator const char * () const { return c (); }
	bool null () const { return m_s == 0; }
	bool empty () const { return !m_s || !m_s[0]; }
	int len () const { return m_s ? (int) strlen (m_s) : 0; }
	bool operator== (const char *s) const { return strcmp (c (), s ? s : "") == 0; }
	bool operator== (const Str &o) const { return strcmp (c (), o.c ()) == 0; }
	bool operator!= (const char *s) const { return !(*this == s); }
	void append (const char *s) { int a = len (), b = s ? (int) strlen (s) : 0; char *n = new char[a + b + 1]; if (a) memcpy (n, m_s, a); if (b) memcpy (n + a, s, b); n[a + b] = 0; delete [] m_s; m_s = n; }
};

// ---- .NET's System.Random (the seeded, "Net5CompatSeedImpl" algorithm: Knuth's subtractive
// generator). Koton seeds it for cadences, humanisation, the emergent melody...: reproducing it
// bit for bit makes a .sq sound the same here as on Windows. ----------------------------------------------
class NetRandom
{
	// C#'s int arithmetic wraps: done on unsigned (signed overflow is undefined in C++)
	static int wsub (int a, int b) { return (int) ((unsigned) a - (unsigned) b); }
	int m_seedArray[56];
	int m_inext, m_inextp;
public:
	explicit NetRandom (int seed)
	{
		int subtraction = (seed == (-2147483647 - 1)) ? 2147483647 : (seed < 0 ? -seed : seed);
		int mj = wsub (161803398, subtraction);
		m_seedArray[55] = mj;
		int mk = 1;
		int ii = 0;
		for (int i = 1; i < 55; i++)
		{
			if ((ii += 21) >= 55) ii -= 55;
			m_seedArray[ii] = mk;
			mk = wsub (mj, mk);
			if (mk < 0) mk += 2147483647;
			mj = m_seedArray[ii];
		}
		for (int k = 1; k < 5; k++)
			for (int i = 1; i < 56; i++)
			{
				int n = i + 30;
				if (n >= 55) n -= 55;
				m_seedArray[i] = wsub (m_seedArray[i], m_seedArray[1 + n]);
				if (m_seedArray[i] < 0) m_seedArray[i] += 2147483647;
			}
		m_inext = 0;
		m_inextp = 21;
		m_seedArray[0] = 0;
	}
	int internalSample ()
	{
		int locINext = m_inext, locINextp = m_inextp;
		if (++locINext >= 56) locINext = 1;
		if (++locINextp >= 56) locINextp = 1;
		int retVal = wsub (m_seedArray[locINext], m_seedArray[locINextp]);
		if (retVal == 2147483647) retVal--;
		if (retVal < 0) retVal += 2147483647;
		m_seedArray[locINext] = retVal;
		m_inext = locINext;
		m_inextp = locINextp;
		return retVal;
	}
	double sample () { return internalSample () * (1.0 / 2147483647); }
	double nextDouble () { return sample (); }
	int next () { return internalSample (); }
	int next (int maxValue) { return maxValue <= 0 ? 0 : (int) (sample () * maxValue); }
	int next (int minValue, int maxValue)
	{
		if (minValue >= maxValue) return minValue;
		long long range = (long long) maxValue - minValue;
		if (range <= 2147483647) return (int) (sample () * range) + minValue;
		return (int) ((long long) (largeSample () * range) + minValue);
	}
private:
	double largeSample ()
	{
		int result = internalSample ();
		bool negative = internalSample () % 2 == 0;
		if (negative) result = -result;
		double d = result;
		d += 2147483646;
		d /= 2 * 2147483647.0 - 1;
		return d;
	}
};

} // namespace kt

#endif
