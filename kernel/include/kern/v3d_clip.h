//
// kern/v3d_clip.h -- the triangles given to the V3D made well-formed (sys/v3d.cpp): clipped, in
// clip space, against the near plane (z >= -w, w > 0) and a guard band CLIP_GUARD x the screen.
// Plain C++ (no Circle): also built on the PC by tools/tests/run_v3d_clip_test.sh.
//
#ifndef _kern_v3d_clip_h
#define _kern_v3d_clip_h
#include <kern/kapi_abi.h>

#define CLIP_GUARD	4.0f
#define CLIP_EPS	1e-5f


static inline float V3DClipDist (const kapi_gpu_vertex3 &v, int nPlane)
{
	switch (nPlane)
	{
	case 0:  return v.z + v.w;				// near
	case 1:  return v.w - CLIP_EPS;			// in front of the eye
	case 2:  return CLIP_GUARD * v.w - v.x;
	case 3:  return CLIP_GUARD * v.w + v.x;
	case 4:  return CLIP_GUARD * v.w - v.y;
	default: return CLIP_GUARD * v.w + v.y;
	}
}
static inline unsigned char V3DLerpU8 (unsigned char a, unsigned char b, float t) { float v = (float) a + ((float) b - (float) a) * t; return (unsigned char) (v <= 0 ? 0 : v >= 255 ? 255 : (int) (v + 0.5f)); }
static inline void V3DLerpV (kapi_gpu_vertex3 &o, const kapi_gpu_vertex3 &a, const kapi_gpu_vertex3 &b, float t)
{
	o.x = a.x + (b.x - a.x) * t; o.y = a.y + (b.y - a.y) * t; o.z = a.z + (b.z - a.z) * t; o.w = a.w + (b.w - a.w) * t;
	o.s = a.s + (b.s - a.s) * t; o.t = a.t + (b.t - a.t) * t;
	o.r = V3DLerpU8 (a.r, b.r, t); o.g = V3DLerpU8 (a.g, b.g, t); o.b = V3DLerpU8 (a.b, b.b, t); o.a = V3DLerpU8 (a.a, b.a, t);
	o.r2 = V3DLerpU8 (a.r2, b.r2, t); o.g2 = V3DLerpU8 (a.g2, b.g2, t); o.b2 = V3DLerpU8 (a.b2, b.b2, t); o.a2 = V3DLerpU8 (a.a2, b.a2, t);
}

// One triangle (clip space) -> its clipped fan into pOut (room for 21 vertices): how many written.
static inline unsigned V3DClipTriangle (const kapi_gpu_vertex3 *pIn, kapi_gpu_vertex3 *pOut)
{
	bool bInside = true;
	for (int p = 0; p < 6 && bInside; p++)
		for (int k = 0; k < 3; k++) if (!(V3DClipDist (pIn[k], p) >= 0)) { bInside = false; break; }	// (NaN: not inside)
	if (bInside) { pOut[0] = pIn[0]; pOut[1] = pIn[1]; pOut[2] = pIn[2]; return 3; }
	kapi_gpu_vertex3 A[10], B[10]; unsigned nA = 3;
	A[0] = pIn[0]; A[1] = pIn[1]; A[2] = pIn[2];
	for (int p = 0; p < 6 && nA >= 3; p++)
	{
		unsigned nB = 0;
		for (unsigned k = 0; k < nA; k++)
		{
			const kapi_gpu_vertex3 &a = A[k], &b = A[(k + 1) % nA];
			float da = V3DClipDist (a, p), db = V3DClipDist (b, p);
			bool ina = da >= 0, inb = db >= 0;		// (NaN: out)
			if (ina && nB < 10) B[nB++] = a;
			if (ina != inb && nB < 10 && da == da && db == db)
			{
				kapi_gpu_vertex3 &o = B[nB++];
				V3DLerpV (o, a, b, da / (da - db));
				switch (p)						// exactly on the plane (float rounding)
				{
				case 0:  o.z = -o.w; break;
				case 1:  if (o.w < CLIP_EPS) o.w = CLIP_EPS; break;
				case 2:  o.x = CLIP_GUARD * o.w; break;
				case 3:  o.x = -CLIP_GUARD * o.w; break;
				case 4:  o.y = CLIP_GUARD * o.w; break;
				default: o.y = -CLIP_GUARD * o.w; break;
				}
			}
		}
		for (unsigned k = 0; k < nB; k++) A[k] = B[k];
		nA = nB;
	}
	if (nA < 3) return 0;
	unsigned n = 0;
	for (unsigned k = 1; k + 1 < nA && n + 3 <= 21; k++) { pOut[n++] = A[0]; pOut[n++] = A[k]; pOut[n++] = A[k + 1]; }
	return n;
}


#endif
