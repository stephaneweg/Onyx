//
// math3d.h -- the game's small 3D maths: vectors, 4 x 4 matrices in the GPU's layout (row by row,
// clip = M * (x y z 1)), the camera's projection and view. Plain C++ (the Pi and the PC tests).
//
// Conventions: y up, right-handed, a model faces +Z (its nose), 1 unit = 1 metre; the camera looks
// along its -Z; front faces are counter-clockwise (the kernel's KAPI_GPU_B_CULL_BACK).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_MATH3D_H
#define CZ_MATH3D_H

#include <math.h>

namespace cz {

static const float PI = 3.14159265358979f;

struct V3
{
	float x, y, z;
	V3 () : x (0), y (0), z (0) {}
	V3 (float a, float b, float c) : x (a), y (b), z (c) {}
	V3 operator+ (const V3 &o) const { return V3 (x + o.x, y + o.y, z + o.z); }
	V3 operator- (const V3 &o) const { return V3 (x - o.x, y - o.y, z - o.z); }
	V3 operator* (float k) const { return V3 (x * k, y * k, z * k); }
	V3 operator- () const { return V3 (-x, -y, -z); }
	V3 &operator+= (const V3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
	V3 &operator-= (const V3 &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
	V3 &operator*= (float k) { x *= k; y *= k; z *= k; return *this; }
};
static inline float dot (const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross (const V3 &a, const V3 &b) { return V3 (a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float len (const V3 &a) { return sqrtf (dot (a, a)); }
static inline V3 norm (const V3 &a) { float l = len (a); return l > 1e-9f ? a * (1.0f / l) : V3 (0, 1, 0); }
static inline V3 lerp (const V3 &a, const V3 &b, float f) { return a + (b - a) * f; }
static inline float clampf (float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf (float a, float b, float f) { return a + (b - a) * f; }
static inline float approach (float v, float target, float step) { return v < target ? (v + step > target ? target : v + step) : (v - step < target ? target : v - step); }
static inline float smooth01 (float t) { t = clampf (t, 0, 1); return t * t * (3 - 2 * t); }
static inline float wrapAngle (float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

// A 4 x 4 matrix, row by row (m[r * 4 + c]); applied to column vectors.
struct M4
{
	float m[16];
	static M4 identity () { M4 r; for (int i = 0; i < 16; i++) r.m[i] = (i % 5) ? 0.0f : 1.0f; return r; }
	M4 operator* (const M4 &o) const
	{
		M4 r;
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				r.m[i * 4 + j] = m[i * 4] * o.m[j] + m[i * 4 + 1] * o.m[4 + j] + m[i * 4 + 2] * o.m[8 + j] + m[i * 4 + 3] * o.m[12 + j];
		return r;
	}
	V3 point (const V3 &p) const { return V3 (m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]); }
	V3 dir (const V3 &p) const { return V3 (m[0] * p.x + m[1] * p.y + m[2] * p.z, m[4] * p.x + m[5] * p.y + m[6] * p.z, m[8] * p.x + m[9] * p.y + m[10] * p.z); }
	// (x y z 1) -> clip x y z w
	void clip (const V3 &p, float out[4]) const
	{
		for (int r = 0; r < 4; r++) out[r] = m[r * 4] * p.x + m[r * 4 + 1] * p.y + m[r * 4 + 2] * p.z + m[r * 4 + 3];
	}
	// A frame: the columns are right, up, forward (the model's x, y, z axes in the world), then the position;
	// each axis scaled.
	static M4 frame (const V3 &right, const V3 &up, const V3 &fwd, const V3 &pos, float sx = 1, float sy = 1, float sz = 1)
	{
		M4 r = identity ();
		r.m[0] = right.x * sx; r.m[1] = up.x * sy; r.m[2] = fwd.x * sz; r.m[3] = pos.x;
		r.m[4] = right.y * sx; r.m[5] = up.y * sy; r.m[6] = fwd.y * sz; r.m[7] = pos.y;
		r.m[8] = right.z * sx; r.m[9] = up.z * sy; r.m[10] = fwd.z * sz; r.m[11] = pos.z;
		return r;
	}
	static M4 translate (const V3 &p) { return frame (V3 (1, 0, 0), V3 (0, 1, 0), V3 (0, 0, 1), p); }
	static M4 scale (float sx, float sy, float sz) { return frame (V3 (1, 0, 0), V3 (0, 1, 0), V3 (0, 0, 1), V3 (), sx, sy, sz); }
	static M4 rotY (float a) { float c = cosf (a), s = sinf (a); return frame (V3 (c, 0, -s), V3 (0, 1, 0), V3 (s, 0, c), V3 ()); }
	static M4 rotX (float a) { float c = cosf (a), s = sinf (a); return frame (V3 (1, 0, 0), V3 (0, c, s), V3 (0, -s, c), V3 ()); }
	static M4 rotZ (float a) { float c = cosf (a), s = sinf (a); return frame (V3 (c, s, 0), V3 (-s, c, 0), V3 (0, 0, 1), V3 ()); }
	// OpenGL's perspective (z -w..w), fovy in radians
	static M4 perspective (float fovy, float aspect, float zn, float zf)
	{
		M4 r; for (int i = 0; i < 16; i++) r.m[i] = 0;
		float f = 1.0f / tanf (fovy * 0.5f);
		r.m[0] = f / aspect; r.m[5] = f;
		r.m[10] = (zf + zn) / (zn - zf); r.m[11] = 2 * zf * zn / (zn - zf);
		r.m[14] = -1;
		return r;
	}
	// The view from eye toward target (up: roughly up)
	static M4 lookAt (const V3 &eye, const V3 &target, const V3 &upHint)
	{
		V3 f = norm (target - eye), s = norm (cross (f, upHint)), u = cross (s, f);
		M4 r = identity ();
		r.m[0] = s.x; r.m[1] = s.y; r.m[2] = s.z; r.m[3] = -dot (s, eye);
		r.m[4] = u.x; r.m[5] = u.y; r.m[6] = u.z; r.m[7] = -dot (u, eye);
		r.m[8] = -f.x; r.m[9] = -f.y; r.m[10] = -f.z; r.m[11] = dot (f, eye);
		return r;
	}
};

// ---- colours: 0xRRGGBB ---------------------------------------------------------------------------------------------
// a colour more (k > 1) or less saturated, around its own grey
static inline unsigned rgbVivid (unsigned c, float k)
{
	float r = (float) ((c >> 16) & 255), g = (float) ((c >> 8) & 255), b = (float) (c & 255);
	float y = 0.30f * r + 0.59f * g + 0.11f * b;
	int R = (int) (y + (r - y) * k + 0.5f), G = (int) (y + (g - y) * k + 0.5f), B = (int) (y + (b - y) * k + 0.5f);
	R = R < 0 ? 0 : R > 255 ? 255 : R; G = G < 0 ? 0 : G > 255 ? 255 : G; B = B < 0 ? 0 : B > 255 ? 255 : B;
	return (unsigned) (R << 16 | G << 8 | B);
}
static inline unsigned rgbMix (unsigned a, unsigned b, float f)
{
	f = clampf (f, 0, 1);
	int r = (int) ((float) ((a >> 16) & 255) * (1 - f) + (float) ((b >> 16) & 255) * f);
	int g = (int) ((float) ((a >> 8) & 255) * (1 - f) + (float) ((b >> 8) & 255) * f);
	int bl = (int) ((float) (a & 255) * (1 - f) + (float) (b & 255) * f);
	return (unsigned) (r << 16 | g << 8 | bl);
}
static inline unsigned rgbScale (unsigned a, float k)
{
	int r = (int) ((float) ((a >> 16) & 255) * k), g = (int) ((float) ((a >> 8) & 255) * k), b = (int) ((float) (a & 255) * k);
	r = r > 255 ? 255 : r < 0 ? 0 : r; g = g > 255 ? 255 : g < 0 ? 0 : g; b = b > 255 ? 255 : b < 0 ? 0 : b;
	return (unsigned) (r << 16 | g << 8 | b);
}

// ---- a small random generator (xorshift32): the same races on the Pi and the PC ----------------------------------
struct Rng
{
	unsigned s;
	explicit Rng (unsigned seed = 0x9E3779B9u) : s (seed ? seed : 0x9E3779B9u) {}
	unsigned next () { unsigned x = s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return s = x; }
	int n (int k) { return k > 0 ? (int) (next () % (unsigned) k) : 0; }
	float f () { return (float) (next () >> 8) * (1.0f / 16777216.0f); }
	float range (float a, float b) { return a + (b - a) * f (); }
};

} // namespace cz

#endif
