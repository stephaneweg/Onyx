//
// v3d/shaders.h -- ready-made QPU programs for kapi v61 (gpu_program / gpu_render2), built with
// the run-time builder (qpu.h). The vertex layout they expect: the clip-space position x y z w
// first, then the floats handed on as varyings.
//
//   passVS (p, n)   vertex shader: a vertex of n floats (4..64); writes Xs Ys (24.8 fixed
//                   point) Zs 1/Wc, then the n - 4 other floats as varyings, in order.
//                   Uniforms: x scale, y scale, z scale, z offset (viewUniforms).
//   passCS (p)      coordinate shader: reads x y z w, writes Xc Yc Zc Wc Xs Ys (csOutputs 6).
//                   Uniforms: x scale, y scale.
//   flatFS (p, final)          fragment shader: the colour of 4 float uniforms (r g b a).
//   varyFS (p, n, final)       fragment shader: reads n varyings (n >= 4), the first 4 = r g b a.
//   texFS (p)                  fragment shader: reads 2 varyings s t, one texture (its p0 p1:
//                              uniforms 0 and 1), writes the texel.
// final: the fragment shader has no thread switch before its end (KAPI_GPU_P_FS_FINAL), else it
// switches once before the TLB writes, as Mesa's shaders do. texFS switches (not final).
//
#ifndef _v3d_shaders_h
#define _v3d_shaders_h
#include "v3d/qpu.h"

namespace qpu
{
void passVS (Prog &p, int nInputs);
void passCS (Prog &p);
void flatFS (Prog &p, bool bFinal);
void varyFS (Prog &p, int nVaryings, bool bFinal);
void texFS (Prog &p);

// the vertex shader's uniforms for a w x h target: x scale, y scale, z scale, z offset (as
// 32-bit words; the coordinate shader takes the first 2)
void viewUniforms (int w, int h, unsigned out[4]);
}

#endif
