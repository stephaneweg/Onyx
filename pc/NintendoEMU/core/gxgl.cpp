//
// gxgl.cpp -- the GameCube's GX on the PC's GPU (OpenGL 3.3): gc::GxGpu for gc::Machine::gpu
// (user/Emulators/gc/gc_gxgpu.cpp hands it the draws as the game gave them, while the machine runs).
//
// The EFB is a framebuffer of ours (colour RGBA8 + depth 24 bits, at `scale` times 640 x 528), its
// rows the EFB's (row 0 at the top: GL's row 0). A draw: its vertices in model space, the XF memory
// (matrices, lights) and the GxState as two uniform blocks; the vertex shader transforms, lights (the
// two colour channels, the 8 lights) and makes the texture coordinates (the texgens, the dual
// texture); the fragment shader is the TEV (its 16 stages in integers as the hardware: the swap
// tables, the konst colours, the compare modes), the alpha test, the z texture, the fog; blending,
// depth, culling, the colour / alpha masks, the logic ops are the GPU's own state. Consecutive
// draws with the same state are one batch (one draw call). An EFB copy to a texture renders the
// EFB's rectangle into a texture of ours (its format converted as the texture decoder would read
// it: intensity, RGB565, RGB5A3, the depth's bytes...), the later draws reading that address
// sample it; a copy to the XFB is the picture present() shows.
//
#include "gc/gc.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string>
#include <unordered_map>

gc::GxGpu *gxgl_create (int scale, char *err, int cap);
void gxgl_destroy (gc::GxGpu *g);
bool gxgl_present (gc::GxGpu *g, int x, int y, int w, int h, int W, int H);
void gxgl_release (gc::GxGpu *g);
void gxgl_set_scale (gc::GxGpu *g, int scale);
int gxgl_read (gc::GxGpu *g, unsigned *px, int cap, int *w, int *h);
const char *gxgl_name (gc::GxGpu *g);

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>

using namespace gc;

typedef char GLchar; typedef ptrdiff_t GLsizeiptr; typedef ptrdiff_t GLintptr;
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_TEXTURE0 0x84C0
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_TEXTURE_MIN_LOD 0x813A
#define GL_TEXTURE_MAX_LOD 0x813B
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_LOD_BIAS 0x8501
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_SRC1_ALPHA 0x8589
#define GL_ONE_MINUS_SRC1_ALPHA 0x88FB
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#define GL_BGRA 0x80E1
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_NUM_EXTENSIONS 0x821D
#define GL_COMPLETION_STATUS 0x91B1
#define GL_MAP_WRITE_BIT 0x0002
#define GL_MAP_INVALIDATE_RANGE_BIT 0x0004
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#define GL_MAP_PERSISTENT_BIT 0x0040
#define GL_MAP_COHERENT_BIT 0x0080
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x0001
typedef struct __GLsync *GLsync; typedef unsigned long long GLuint64;

#define GXF(ret, name, args) typedef ret (APIENTRY *T_##name) args; static T_##name name;
GXF (void, glGenBuffers, (GLsizei, GLuint *))
GXF (void, glDeleteBuffers, (GLsizei, const GLuint *))
GXF (void, glBindBuffer, (GLenum, GLuint))
GXF (void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))
GXF (void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *))
GXF (void, glBindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr))
GXF (void, glGenVertexArrays, (GLsizei, GLuint *))
GXF (void, glDeleteVertexArrays, (GLsizei, const GLuint *))
GXF (void, glBindVertexArray, (GLuint))
GXF (void, glEnableVertexAttribArray, (GLuint))
GXF (void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))
GXF (void, glVertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void *))
GXF (GLuint, glCreateShader, (GLenum))
GXF (void, glDeleteShader, (GLuint))
GXF (void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))
GXF (void, glCompileShader, (GLuint))
GXF (void, glGetShaderiv, (GLuint, GLenum, GLint *))
GXF (void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
GXF (GLuint, glCreateProgram, (void))
GXF (void, glDeleteProgram, (GLuint))
GXF (void, glAttachShader, (GLuint, GLuint))
GXF (void, glLinkProgram, (GLuint))
GXF (void, glGetProgramiv, (GLuint, GLenum, GLint *))
GXF (void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
GXF (void, glUseProgram, (GLuint))
GXF (GLint, glGetUniformLocation, (GLuint, const GLchar *))
GXF (GLuint, glGetUniformBlockIndex, (GLuint, const GLchar *))
GXF (void, glUniformBlockBinding, (GLuint, GLuint, GLuint))
GXF (void, glUniform1i, (GLint, GLint))
GXF (void, glUniform2f, (GLint, GLfloat, GLfloat))
GXF (void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
GXF (void, glUniform4i, (GLint, GLint, GLint, GLint, GLint))
GXF (void, glGenFramebuffers, (GLsizei, GLuint *))
GXF (void, glDeleteFramebuffers, (GLsizei, const GLuint *))
GXF (void, glBindFramebuffer, (GLenum, GLuint))
GXF (void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
GXF (GLenum, glCheckFramebufferStatus, (GLenum))
GXF (void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))
GXF (void, glActiveTexture, (GLenum))
GXF (void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))
GXF (void, glBlendEquation, (GLenum))
GXF (void, glBindFragDataLocationIndexed, (GLuint, GLuint, GLuint, const GLchar *))
GXF (void, glDrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void *, GLint))
GXF (const GLubyte *, glGetStringi, (GLenum, GLuint))
GXF (void *, glMapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield))
GXF (GLboolean, glUnmapBuffer, (GLenum))
GXF (void, glBufferStorage, (GLenum, GLsizeiptr, const void *, GLbitfield))
GXF (GLsync, glFenceSync, (GLenum, GLbitfield))
GXF (GLenum, glClientWaitSync, (GLsync, GLbitfield, GLuint64))
GXF (void, glDeleteSync, (GLsync))
GXF (void, glGenSamplers, (GLsizei, GLuint *))
GXF (void, glDeleteSamplers, (GLsizei, const GLuint *))
GXF (void, glBindSampler, (GLuint, GLuint))
GXF (void, glSamplerParameteri, (GLuint, GLenum, GLint))
GXF (void, glSamplerParameterf, (GLuint, GLenum, GLfloat))
GXF (void, glMaxShaderCompilerThreadsKHR, (GLuint))

static void *glproc (const char *n)
{
	void *p = (void *) wglGetProcAddress (n);
	if (p == 0 || p == (void *) 1 || p == (void *) 2 || p == (void *) 3 || p == (void *) -1)
	{
		HMODULE m = GetModuleHandleA ("opengl32.dll");
		p = m ? (void *) GetProcAddress (m, n) : 0;
	}
	return p;
}

// ---- the shaders -----------------------------------------------------------------------------------------------
static const char *BLOCKS = R"(
layout(std140) uniform XF { vec4 xm[64]; vec4 xn[24]; vec4 xp[64]; vec4 xl[32]; };
layout(std140) uniform ST
{
	ivec4 vtx; ivec4 chan; uvec4 matAmb; ivec4 texgen[8]; vec4 proj[2]; vec4 vp;
	ivec4 gen; ivec4 tevC[4]; ivec4 tevA[4]; ivec4 tref[4]; ivec4 ksel[4]; ivec4 swp[4]; ivec4 regs[4]; ivec4 konst[4];
	ivec4 alphaT; ivec4 zenv; ivec4 fogI; vec4 fogF; vec4 fogColor; vec4 fogK[3];
	ivec4 ind[4]; ivec4 indMtx[3]; ivec4 indRef; vec4 texSize[8]; vec4 tcScale[8];
};
)";

static const char *VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 aC0;
layout(location = 3) in vec4 aC1;
layout(location = 4) in vec4 aT01;
layout(location = 5) in vec4 aT23;
layout(location = 6) in vec4 aT45;
layout(location = 7) in vec4 aT67;
layout(location = 8) in uvec4 aM0;
layout(location = 9) in uvec4 aM1;
layout(location = 10) in uvec4 aM2;
out vec4 vC0; out vec4 vC1;
out vec3 vT0; out vec3 vT1; out vec3 vT2; out vec3 vT3; out vec3 vT4; out vec3 vT5; out vec3 vT6; out vec3 vT7;

float nw (int w) { w = w % 96; return xn[w >> 2][w & 3]; }	// the normal matrices (XF 0x400-0x45F)
float lw (int w) { return xl[(w >> 2) & 31][w & 3]; }		// the lights (XF 0x600-0x67F)
vec4 rgba (uint c) { return vec4 (float (c >> 24), float ((c >> 16) & 255u), float ((c >> 8) & 255u), float (c & 255u)); }

// a colour channel: its colour (rgb, the COLORn control) or its alpha (a, ALPHAn), 0..255 -- the
// material (the register's or the vertex's) x (the ambient + the lights), as the XF computes it
vec4 channel (int ctl, vec4 regMat, vec4 regAmb, vec4 vcol, vec3 pos, vec3 n)
{
	vec4 mat = (ctl & 1) != 0 ? vcol : regMat;
	if ((ctl & 2) == 0) return mat;
	vec4 lacc = (ctl & 64) != 0 ? vcol : regAmb;
	int mask = ((ctl >> 2) & 15) | ((ctl >> 7) & 0xF0);
	int dfn = (ctl >> 7) & 3, atn = (ctl >> 9) & 3;
	for (int L = 0; L < 8; L++)
	{
		if ((mask & (1 << L)) == 0) continue;
		int b = L * 16;
		vec4 lcol = rgba (floatBitsToUint (lw (b + 3)));
		vec3 cosA = vec3 (lw (b + 4), lw (b + 5), lw (b + 6)), distA = vec3 (lw (b + 7), lw (b + 8), lw (b + 9));
		vec3 lpos = vec3 (lw (b + 10), lw (b + 11), lw (b + 12)), ldir = vec3 (lw (b + 13), lw (b + 14), lw (b + 15));
		vec3 ld = lpos - pos; float at = 1.0;
		if (atn == 3)						// spot: the angle, the distance
		{
			float d2 = dot (ld, ld), d = sqrt (d2);
			if (d > 0.0) ld /= d;
			float cs = max (0.0, dot (ld, ldir));
			float den = dot (distA, vec3 (1.0, d, d2));
			at = den != 0.0 ? max (0.0, cosA.x + cosA.y * cs + cosA.z * cs * cs) / den : 0.0;
		}
		else if (atn == 1)					// specular: the half angle
		{
			ld = dot (ld, ld) > 0.0 ? normalize (ld) : n;
			float cs = dot (n, ld) >= 0.0 ? max (0.0, dot (n, ldir)) : 0.0;
			vec3 da = dfn == 0 || dot (distA, distA) == 0.0 ? distA : normalize (distA);
			float den = dot (da, vec3 (1.0, cs, cs * cs));
			at = den != 0.0 ? max (0.0, dot (cosA, vec3 (1.0, cs, cs * cs))) / den : 0.0;
		}
		else ld = dot (ld, ld) > 0.0 ? normalize (ld) : n;
		float df = dfn == 0 ? 1.0 : dfn == 1 ? dot (ld, n) : max (0.0, dot (ld, n));
		lacc += round (at * df * lcol);
	}
	lacc = clamp (lacc, 0.0, 255.0);
	return floor (mat * (lacc + floor (lacc / 128.0)) / 256.0);
}

void main ()
{
	int pm = int (aM0.x);
	vec4 p = vec4 (aPos, 1.0);
	vec3 eye = vec3 (dot (xm[pm & 63], p), dot (xm[(pm + 1) & 63], p), dot (xm[(pm + 2) & 63], p));
	bool hasN = (vtx.x & 1) != 0;
	vec3 n = vec3 (0.0);
	if (hasN)
	{
		int nb = (pm & 31) * 3;
		n = vec3 (dot (vec3 (nw (nb), nw (nb + 1), nw (nb + 2)), aNrm), dot (vec3 (nw (nb + 3), nw (nb + 4), nw (nb + 5)), aNrm),
			dot (vec3 (nw (nb + 6), nw (nb + 7), nw (nb + 8)), aNrm));
		float l = length (n);
		if (l > 0.0) n /= l;
	}
	// the projection (GX: z from -w to 0) -> GL's clip space (z from -w to w; the depth range is the viewport's)
	vec4 clip;
	if (proj[1].z != 0.0) clip = vec4 (proj[0].x * eye.x + proj[0].y, proj[0].z * eye.y + proj[0].w, proj[1].x * eye.z + proj[1].y, 1.0);
	else clip = vec4 (proj[0].x * eye.x + proj[0].y * eye.z, proj[0].z * eye.y + proj[0].w * eye.z, proj[1].x * eye.z + proj[1].y, -eye.z);
	gl_Position = vec4 (clip.x * vp.x, clip.y * vp.y, 2.0 * clip.z + clip.w, clip.w);
	// the colour channels
	bool h0 = (vtx.x & 2) != 0, h1 = (vtx.x & 4) != 0;
	vec4 raw0 = floor (aC0 * 255.0 + 0.5), raw1 = floor (aC1 * 255.0 + 0.5);
	vec4 v0 = h0 ? raw0 : vec4 (255.0), v1 = h1 ? raw1 : v0;
	vec4 m0 = rgba (matAmb.x), m1 = rgba (matAmb.y), a0 = rgba (matAmb.z), a1 = rgba (matAmb.w);
	vec4 col0 = vec4 (channel (chan.x, m0, a0, v0, eye, n).rgb, channel (chan.z, m0, a0, v0, eye, n).a);
	vec4 col1 = vec4 (channel (chan.y, m1, a1, v1, eye, n).rgb, channel (chan.w, m1, a1, v1, eye, n).a);
	if (vtx.y == 0) col0 = h0 ? raw0 : vec4 (255.0);
	if (vtx.y < 2) col1 = h1 ? raw1 : col0;
	vC0 = col0 / 255.0; vC1 = col1 / 255.0;
	// the texture coordinates: the texgens (a matrix x the position / normal / a coordinate; the
	// colours), the dual texture's normalization and post matrix
	vec2 tin[8] = vec2[8] (aT01.xy, aT01.zw, aT23.xy, aT23.zw, aT45.xy, aT45.zw, aT67.xy, aT67.zw);
	int tmi[8] = int[8] (int (aM0.y), int (aM0.z), int (aM0.w), int (aM1.x), int (aM1.y), int (aM1.z), int (aM1.w), int (aM2.x));
	vec3 t[8];
	for (int i = 0; i < 8; i++)
	{
		t[i] = vec3 (0.0, 0.0, 1.0);
		if (i >= vtx.z) continue;
		int info = texgen[i].x;
		int src = (info >> 7) & 31, type = (info >> 4) & 7;
		vec4 inp = vec4 (0.0, 0.0, 1.0, 1.0);
		if (src == 0) inp.xyz = aPos;
		else if (src == 1) inp.xyz = hasN ? aNrm : vec3 (0.0);
		else if (src >= 5 && src <= 12) inp.xy = tin[src - 5];
		if ((info & 4) == 0) inp.z = 1.0;
		if (type == 0)
		{
			int r = tmi[i];
			t[i] = vec3 (dot (xm[r & 63], inp), dot (xm[(r + 1) & 63], inp), (info & 2) != 0 ? dot (xm[(r + 2) & 63], inp) : 1.0);
			if (vtx.w != 0)
			{
				int pinfo = texgen[i].y, pr = pinfo & 63;
				vec3 q = t[i];
				if ((pinfo & 256) != 0 && dot (q, q) > 0.0) q = normalize (q);
				t[i] = vec3 (dot (xp[pr & 63].xyz, q) + xp[pr & 63].w, dot (xp[(pr + 1) & 63].xyz, q) + xp[(pr + 1) & 63].w,
					dot (xp[(pr + 2) & 63].xyz, q) + xp[(pr + 2) & 63].w);
			}
		}
		else if (type == 1) t[i] = t[(info >> 12) & 7];		// (emboss: its source's, without the bump)
		else t[i] = vec3 (type == 2 ? vC0.xy : vC1.xy, 1.0);
	}
	vT0 = t[0]; vT1 = t[1]; vT2 = t[2]; vT3 = t[3]; vT4 = t[4]; vT5 = t[5]; vT6 = t[6]; vT7 = t[7];
}
)";

static const char *FS_HEAD = R"(
in vec4 vC0; in vec4 vC1;
in vec3 vT0; in vec3 vT1; in vec3 vT2; in vec3 vT3; in vec3 vT4; in vec3 vT5; in vec3 vT6; in vec3 vT7;
uniform sampler2D s0; uniform sampler2D s1; uniform sampler2D s2; uniform sampler2D s3;
uniform sampler2D s4; uniform sampler2D s5; uniform sampler2D s6; uniform sampler2D s7;
layout(location = 0, index = 0) out vec4 oCol;
layout(location = 0, index = 1) out vec4 oSrc;			// (the alpha the blending uses)

ivec4 prevR, c0R, c1R, c2R, texC, rasC, kC;

vec3 tcOf (int i)
{
	switch (i) { case 0: return vT0; case 1: return vT1; case 2: return vT2; case 3: return vT3; case 4: return vT4; case 5: return vT5; case 6: return vT6; }
	return vT7;
}
vec4 smp (int m, vec2 uv)
{
	switch (m)
	{
	case 0: return texture (s0, uv); case 1: return texture (s1, uv); case 2: return texture (s2, uv); case 3: return texture (s3, uv);
	case 4: return texture (s4, uv); case 5: return texture (s5, uv); case 6: return texture (s6, uv);
	}
	return texture (s7, uv);
}
ivec4 swz (ivec4 c, int t) { return ivec4 (c[SWPT[t].x], c[SWPT[t].y], c[SWPT[t].z], c[SWPT[t].w]); }
const int kfrac[8] = int[8] (255, 223, 191, 159, 128, 96, 64, 32);
ivec3 kcolour (int sel)
{
	if (sel < 8) return ivec3 (kfrac[sel]);
	if (sel >= 12 && sel < 16) return konst[sel - 12].rgb;
	if (sel >= 16) return ivec3 (konst[(sel - 16) & 3][(sel - 16) >> 2]);
	return ivec3 (0);
}
int kalpha (int sel)
{
	if (sel < 8) return kfrac[sel];
	if (sel >= 16) return konst[(sel - 16) & 3][(sel - 16) >> 2];
	return 0;
}
ivec3 cin (int s)
{
	switch (s)
	{
	case 0: return prevR.rgb; case 1: return ivec3 (prevR.a); case 2: return c0R.rgb; case 3: return ivec3 (c0R.a);
	case 4: return c1R.rgb; case 5: return ivec3 (c1R.a); case 6: return c2R.rgb; case 7: return ivec3 (c2R.a);
	case 8: return texC.rgb; case 9: return ivec3 (texC.a); case 10: return rasC.rgb; case 11: return ivec3 (rasC.a);
	case 12: return ivec3 (255); case 13: return ivec3 (128); case 14: return kC.rgb;
	}
	return ivec3 (0);
}
int ain (int s)
{
	switch (s) { case 0: return prevR.a; case 1: return c0R.a; case 2: return c1R.a; case 3: return c2R.a; case 4: return texC.a; case 5: return rasC.a; case 6: return kC.a; }
	return 0;
}
// (d + bias +- lerp (a, b, c)) x scale, as the hardware: c from 0..255 to 0..256, the scale inside the lerp
ivec3 tevReg (ivec3 a, ivec3 b, ivec3 c, ivec3 d, int bias, int sub, int scale)
{
	int sh = scale == 1 ? 1 : scale == 2 ? 2 : 0;
	int rnd = sub == 0 ? (scale == 3 ? 128 : 0) : (scale == 3 ? 0 : 127);
	ivec3 l = ((((a << 8) + (b - a) * (c + (c >> 7))) << sh) + rnd) >> 8;
	ivec3 r = ((d + (bias == 1 ? 128 : bias == 2 ? -128 : 0)) << sh) + (sub != 0 ? -l : l);
	return scale == 3 ? r >> 1 : r;
}
int tevRegA (int a, int b, int c, int d, int bias, int sub, int scale)
{
	return tevReg (ivec3 (a), ivec3 (b), ivec3 (c), ivec3 (d), bias, sub, scale).x;
}
int cmpKey (ivec3 v, int m) { return m == 0 ? v.r : m == 1 ? (v.g << 8) | v.r : (v.b << 16) | (v.g << 8) | v.r; }
ivec3 tevCmp (ivec3 a, ivec3 b, ivec3 c, int mode)
{
	bool eq = (mode & 1) != 0; int m = mode >> 1;
	if (m == 3) return ivec3 ((eq ? a.r == b.r : a.r > b.r) ? c.r : 0, (eq ? a.g == b.g : a.g > b.g) ? c.g : 0, (eq ? a.b == b.b : a.b > b.b) ? c.b : 0);
	int ka = cmpKey (a, m), kb = cmpKey (b, m);
	return (eq ? ka == kb : ka > kb) ? c : ivec3 (0);
}
int tevCmpA (ivec3 a, ivec3 b, int aa, int ab, int ac, int mode)
{
	bool eq = (mode & 1) != 0; int m = mode >> 1;
	if (m == 3) return (eq ? aa == ab : aa > ab) ? ac : 0;
	int ka = cmpKey (a, m), kb = cmpKey (b, m);
	return (eq ? ka == kb : ka > kb) ? ac : 0;
}
bool atest (int f, int a, int r)
{
	switch (f) { case 0: return false; case 1: return a < r; case 2: return a == r; case 3: return a <= r; case 4: return a > r; case 5: return a != r; case 6: return a >= r; }
	return true;
}
// ---- the indirect texturing: coordinates in texels x 128 (the rasterizer's fixed point)
ivec2 fixUV (int coord)
{
	vec3 t = tcOf (coord);
	vec2 uv = tcScale[coord].z != 0.0 && t.z != 0.0 ? t.xy / t.z : t.xy;
	return ivec2 (uv * tcScale[coord].xy * 128.0);
}
int s11i (int v) { v &= 0x7FF; return v >= 0x400 ? v - 0x800 : v; }
// an indirect matrix (BP 0x06 + 3 m): its two rows (s1.10), its shift
void indMatrix (int m, out ivec3 r0, out ivec3 r1, out int sh)
{
	int k = m * 3;
	int a = indMtx[k >> 2][k & 3], b = indMtx[(k + 1) >> 2][(k + 1) & 3], c = indMtx[(k + 2) >> 2][(k + 2) & 3];
	r0 = ivec3 (s11i (a), s11i (b), s11i (c));
	r1 = ivec3 (s11i (a >> 11), s11i (b >> 11), s11i (c >> 11));
	sh = 17 - (((a >> 22) & 3) | (((b >> 22) & 3) << 2) | (((c >> 22) & 1) << 4));
}
ivec2 shiftBy (ivec2 v, int sh) { return sh >= 0 ? v >> sh : v << (-sh); }
int wrapc (int v, int w) { return w == 0 || w == 7 ? v : w == 6 ? 0 : v & (((256 >> (w - 1)) << 7) - 1); }

)";

// the TEV's program: its start, a stage (CENV, AENV, TREF, KSEL, INDC, st: the ubershader's
// variables or a specialized one's constants), its end -- NSTAGES, NIND, AF0... the same way
static const char *FS_INIT = R"(
void main ()
{
	prevR = regs[0]; c0R = regs[1]; c1R = regs[2]; c2R = regs[3];
	texC = ivec4 (255);
	ivec4 ras0 = ivec4 (round (vC0 * 255.0)), ras1 = ivec4 (round (vC1 * 255.0));
	// the indirect stages' lookups (their texel's a, b, g: the offsets s, t, u)
	ivec3 iind[4] = ivec3[4] (ivec3 (0), ivec3 (0), ivec3 (0), ivec3 (0));
	for (int i = 0; i < 4; i++)
	{
		if (i >= NIND) break;
		int ir = indRef.x >> (6 * i), ss = (i < 2 ? indRef.y : indRef.z) >> ((i & 1) * 8);
		int map = ir & 7, coord = (ir >> 3) & 7;
		ivec2 c = fixUV (coord) >> ivec2 (ss & 15, (ss >> 4) & 15);
		vec4 t = smp (map, vec2 (c) / (texSize[map].xy * 128.0));
		iind[i] = ivec3 (round (t.a * 255.0), round (t.b * 255.0), round (t.g * 255.0));
	}
	ivec2 tevcoord = ivec2 (0);
	int alphabump = 0;
)";

static const char *FS_STAGE = R"(
	{
		int coord = (TREF >> 3) & 7, map = TREF & 7;
		if (INDC != 0)						// (an indirect command: the coordinate offset, the bump alpha)
		{
			ivec2 fuv = fixUV (coord), trans = ivec2 (0);
			int bt = INDC & 3, fmt = (INDC >> 2) & 3;
			if (bt < NIND)
			{
				ivec3 raw = iind[bt];
				ivec3 crd = raw & (fmt == 0 ? 255 : fmt == 1 ? 31 : fmt == 2 ? 15 : 7);
				int bias = (INDC >> 4) & 7, badd = fmt == 0 ? -128 : 1;
				if ((bias & 1) != 0) crd.x += badd;
				if ((bias & 2) != 0) crd.y += badd;
				if ((bias & 4) != 0) crd.z += badd;
				int bs = (INDC >> 7) & 3;
				if (bs != 0) alphabump = raw[bs - 1] & (fmt == 1 ? 224 : fmt == 2 ? 240 : 248);
				int mid = (INDC >> 9) & 15;
				ivec3 r0, r1; int sh;
				if (mid >= 1 && mid <= 3)
				{
					indMatrix (mid - 1, r0, r1, sh);
					trans = shiftBy (ivec2 (r0.x * crd.x + r0.y * crd.y + r0.z * crd.z, r1.x * crd.x + r1.y * crd.y + r1.z * crd.z) >> 3, sh);
				}
				else if (mid >= 5 && mid <= 7) { indMatrix (mid - 5, r0, r1, sh); trans = shiftBy ((fuv * crd.xx) >> 8, sh); }
				else if (mid >= 9 && mid <= 11) { indMatrix (mid - 9, r0, r1, sh); trans = shiftBy ((fuv * crd.yy) >> 8, sh); }
			}
			ivec2 wr = ivec2 (wrapc (fuv.x, (INDC >> 13) & 7), wrapc (fuv.y, (INDC >> 16) & 7));
			tevcoord = (INDC & 0x100000) != 0 ? tevcoord + wr + trans : wr + trans;
			tevcoord = (tevcoord << 8) >> 8;
		}
		if ((TREF & 64) != 0)
		{
			vec2 uv;
			if (INDC != 0) uv = vec2 (tevcoord) / (texSize[map].xy * 128.0);
			else
			{
				vec3 t = tcOf (coord);
				uv = (tcScale[coord].z != 0.0 && t.z != 0.0 ? t.xy / t.z : t.xy) * tcScale[coord].xy / texSize[map].xy;
			}
			texC = swz (ivec4 (round (smp (map, uv) * 255.0)), (AENV >> 2) & 3);
		}
		else texC = ivec4 (255);
		int chn = (TREF >> 7) & 7;
		ivec4 ras = chn == 0 ? ras0 : chn == 1 ? ras1 : chn == 5 ? ivec4 (alphabump) : chn == 6 ? ivec4 (alphabump | (alphabump >> 5)) : ivec4 (0);
		rasC = swz (ras, AENV & 3);
		kC = ivec4 (kcolour (KSEL & 31), kalpha ((KSEL >> 5) & 31));
		ivec3 a = cin ((CENV >> 12) & 15) & 255, b = cin ((CENV >> 8) & 15) & 255, c = cin ((CENV >> 4) & 15) & 255, d = cin (CENV & 15);
		int aa = ain ((AENV >> 13) & 7) & 255, ab = ain ((AENV >> 10) & 7) & 255, ac = ain ((AENV >> 7) & 7) & 255, ad = ain ((AENV >> 4) & 7);
		int cb = (CENV >> 16) & 3, co = (CENV >> 18) & 1, cs = (CENV >> 20) & 3;
		ivec3 rc = cb != 3 ? tevReg (a, b, c, d, cb, co, cs) : d + tevCmp (a, b, c, (cs << 1) | co);
		rc = (CENV & 0x80000) != 0 ? clamp (rc, 0, 255) : clamp (rc, -1024, 1023);
		int xb = (AENV >> 16) & 3, xo = (AENV >> 18) & 1, xs = (AENV >> 20) & 3;
		int ra = xb != 3 ? tevRegA (aa, ab, ac, ad, xb, xo, xs) : ad + tevCmpA (a, b, aa, ab, ac, (xs << 1) | xo);
		ra = (AENV & 0x80000) != 0 ? clamp (ra, 0, 255) : clamp (ra, -1024, 1023);
		if (st == NSTAGES - 1) prevR = ivec4 (rc, ra);		// (the last stage: the TEV's output)
		else
		{
			int cd = (CENV >> 22) & 3, xd = (AENV >> 22) & 3;
			if (cd == 0) prevR.rgb = rc; else if (cd == 1) c0R.rgb = rc; else if (cd == 2) c1R.rgb = rc; else c2R.rgb = rc;
			if (xd == 0) prevR.a = ra; else if (xd == 1) c0R.a = ra; else if (xd == 2) c1R.a = ra; else c2R.a = ra;
		}
	}
)";

static const char *FS_END = R"(
	prevR &= 255;
	// the alpha test
	bool p0 = atest (AF0, prevR.a, alphaT.z), p1 = atest (AF1, prevR.a, alphaT.w);
	bool pass = ALOGIC == 0 ? (p0 && p1) : ALOGIC == 1 ? (p0 || p1) : ALOGIC == 2 ? (p0 != p1) : (p0 == p1);
	if (!pass) discard;
	// the depth (24 bits), a z texture's
	int zc = min (int (gl_FragCoord.z * 16777216.0), 16777215);
#ifdef ZTEX
	{
		ivec4 w = ZFMT == 0 ? ivec4 (0, 0, 0, 1) : ZFMT == 1 ? ivec4 (256, 0, 0, 1) : ivec4 (65536, 256, 1, 0);
		int z = texC.r * w.x + texC.g * w.y + texC.b * w.z + texC.a * w.w + zenv.z;
		if (ZOP == 1) z += zc;
		zc = z & 0xFFFFFF;
		gl_FragDepth = float (zc) / 16777216.0;
	}
#endif
	// the fog
	if (FOGSEL != 0)
	{
		float ze = FOGPROJ == 0 ? (fogF.x * 16777216.0) / float (fogI.z - (zc >> fogI.w)) : fogF.x * float (zc) / 16777216.0;
		if (FOGRANGE)						// (the range: farther at the sides)
		{
			float off = 2.0 * (gl_FragCoord.x / vp.z / fogF.w) - 1.0 - fogF.z;
			float fi = clamp (9.0 - abs (off) * 9.0, 0.0, 9.0);
			int il = int (fi), iu = min (il + 1, 9);
			float k = mix (fogK[il >> 2][il & 3], fogK[iu >> 2][iu & 3], fract (fi));
			if (k != 0.0) ze *= sqrt (off * off + k * k) / k;
		}
		float f = clamp (ze - fogF.y, 0.0, 1.0);
		if (FOGSEL == 4) f = 1.0 - exp2 (-8.0 * f);
		else if (FOGSEL == 5) f = 1.0 - exp2 (-8.0 * f * f);
		else if (FOGSEL == 6) f = exp2 (-8.0 * (1.0 - f));
		else if (FOGSEL == 7) { f = 1.0 - f; f = exp2 (-8.0 * f * f); }
		int fi = int (round (f * 256.0));
		prevR.rgb = (prevR.rgb * (256 - fi) + ivec3 (fogColor.rgb) * fi) >> 8;
	}
	vec4 col = vec4 (prevR) / 255.0;
	if (EFBFMT == 1) col = vec4 (prevR >> 2) / 63.0;			// (RGBA6)
	else if (EFBFMT == 2) col = vec4 (vec3 (prevR.r >> 3, prevR.g >> 2, prevR.b >> 3) / vec3 (31.0, 63.0, 31.0), 1.0);
	oSrc = vec4 (0.0, 0.0, 0.0, col.a);
	if (DSTA) col.a = float (zenv.w & 255) / 255.0;		// (the destination alpha)
	oCol = col;
}
)";

// the ubershader's: the state's values from the uniform block, the stages in a loop
static const char *FS_UBER_DEFS = R"(
#define NSTAGES gen.x
#define NIND gen.y
#define ALOGIC gen.z
#define AF0 alphaT.x
#define AF1 alphaT.y
#define FOGSEL fogI.x
#define FOGPROJ fogI.y
#define FOGRANGE (fogK[2].z != 0.0)
#define EFBFMT gen.w
#define DSTA ((zenv.w & 0x100) != 0)
#define ZOP zenv.x
#define ZFMT zenv.y
#define SWPT swp
)";
static const char *FS_UBER_LOOP = R"(
	for (int st = 0; st < 16; st++)
	{
		if (st >= NSTAGES) break;
		int CENV = tevC[st >> 2][st & 3], AENV = tevA[st >> 2][st & 3], TREF = tref[st >> 2][st & 3], KSEL = ksel[st >> 2][st & 3], INDC = ind[st >> 2][st & 3];
)";

// a quad over the target (4 vertices, a strip): vUV 0..1 from its bottom left
static const char *QUAD_VS = R"(
out vec2 vUV;
void main () { vec2 p = vec2 ((gl_VertexID & 1) != 0 ? 1.0 : 0.0, (gl_VertexID & 2) != 0 ? 1.0 : 0.0); vUV = p; gl_Position = vec4 (p * 2.0 - 1.0, 0.0, 1.0); }
)";

// an EFB copy to a texture: the rectangle, its format converted as the texture decoder reads it
static const char *COPY_FS = R"(
uniform sampler2D src;
uniform vec4 rect;					// the source's x, y, w, h (the EFB texture's texels)
uniform vec2 srcSize;
uniform ivec4 mode;					// the format, the depth, intensity, the EFB has an alpha
in vec2 vUV;
out vec4 o;
float x4 (float v) { return floor (v / 16.0) * 17.0; }
float x5 (float v) { float q = floor (v / 8.0); return q * 8.0 + floor (q / 4.0); }
float x6 (float v) { float q = floor (v / 4.0); return q * 4.0 + floor (q / 16.0); }
float x3 (float v) { float q = floor (v / 32.0); return q * 32.0 + q * 4.0 + floor (q / 2.0); }
void main ()
{
	vec4 t = texture (src, (rect.xy + vUV * rect.zw) / srcSize);
	int f = mode.x;
	vec4 c;
	if (mode.y != 0)
	{
		uint z = uint (clamp (t.r, 0.0, 1.0) * 16777215.0 + 0.5);
		float hi = float (z >> 16), mid = float ((z >> 8) & 255u), lo = float (z & 255u);
		if (f == 0) c = vec4 (x4 (hi));
		else if (f == 1 || f == 8) c = vec4 (hi);
		else if (f == 2) c = vec4 (vec3 (x4 (mid)), x4 (hi));
		else if (f == 3) c = vec4 (vec3 (mid), hi);
		else if (f == 9) c = vec4 (mid);
		else if (f == 10) c = vec4 (lo);
		else if (f == 11 || f == 12) c = vec4 (vec3 (lo), mid);
		else c = vec4 (hi, mid, lo, 255.0);
	}
	else
	{
		vec4 e = floor (t * 255.0 + 0.5);
		if (mode.w == 0) e.a = 255.0;
		float y = min (255.0, floor ((66.0 * e.r + 129.0 * e.g + 25.0 * e.b + 128.0) / 256.0) + 16.0);
		float i = mode.z != 0 ? y : e.r;
		if (f == 0) c = vec4 (x4 (i));
		else if (f == 1 || f == 8) c = vec4 (i);
		else if (f == 2) c = vec4 (vec3 (x4 (i)), x4 (e.a));
		else if (f == 3) c = vec4 (vec3 (i), e.a);
		else if (f == 4) c = vec4 (x5 (e.r), x6 (e.g), x5 (e.b), 255.0);
		else if (f == 5) c = e.a >= 224.0 ? vec4 (x5 (e.r), x5 (e.g), x5 (e.b), 255.0) : vec4 (x4 (e.r), x4 (e.g), x4 (e.b), x3 (e.a));
		else if (f == 7) c = vec4 (e.a);
		else if (f == 9) c = vec4 (e.g);
		else if (f == 10) c = vec4 (e.b);
		else if (f == 11) c = vec4 (vec3 (e.g), e.r);
		else if (f == 12) c = vec4 (vec3 (e.b), e.g);
		else c = e;
	}
	o = c / 255.0;
}
)";

// the picture into the window (its rows top first: upside down for GL)
static const char *PRESENT_FS = R"(
uniform sampler2D src;
uniform vec2 scaleUV;
in vec2 vUV;
out vec4 o;
void main () { o = vec4 (texture (src, vec2 (vUV.x, 1.0 - vUV.y) * scaleUV).rgb, 1.0); }
)";

// ---- the renderer ----------------------------------------------------------------------------------------------
enum { EFB_W = 640, EFB_H = 528, XF_WORDS = 736, XF_BYTES = XF_WORDS * 4 };
enum { CAPV = 0x20000, CAPI = 0x60000 };

// a ring of a buffer written ahead of the GPU: mapped for good, its quarters fenced (a quarter is
// written again once the GPU is done with it), else mapped unsynchronized per write (orphaned when full)
struct Ring { GLuint buf; GLenum target; u32 size, pos; u8 *map; GLsync fence[4]; u32 quarter, left; };

class GxGl : public GxGpu
{
public:
	char err[512], name[128];
	bool init (int scale);
	void shutdown ();
	void draw (Machine &m, const GxState &s, const GxVertex *v, int nv, const u32 *idx, int ni, int prim) override;
	void copy (Machine &m, const GxCopy &c) override;
	bool present (int x, int y, int w, int h, int W, int H);
	int read (unsigned *px, int cap, int *w, int *h);
	void release ();
	void setScale (int s);
	~GxGl () override { shutdown (); }

private:
	int S = 1;
	bool spec = true;						// (the TEV's specialized programs; NEMU_GX_UBER=1: the ubershader)
	GLuint prog[2] = {}, copyProg = 0, presentProg = 0;
	GLint uCopyRect = -1, uCopySize = -1, uCopyMode = -1, uPresentScale = -1;
	GLuint vao = 0, quadVao = 0;
	Ring vbo {}, ibo {}, ubo {};
	GLint uboAlign = 256;
	GLuint efbColor = 0, efbDepth = 0, efbFbo = 0, xfbTex = 0, xfbFbo = 0, dummy = 0;
	bool bound = false;
	int picW = 0, picH = 0; bool hasPicture = false;
	GLuint texObj[Machine::MAX_TEX] = {}; int texLevels[Machine::MAX_TEX] = {};
	struct CopyTex { GLuint tex, fbo; int w, h; } copies[GX_COPIES] = {};
	// the batch being gathered
	bool open = false; u32 serial = 0, xfSerial = 0; int prim = 0;
	GxVertex *bv = 0; int bnv = 0; u32 *bi = 0; int bni = 0;
	GxState bs; u32 bxf[XF_WORDS];
	// the GL state set (to skip what is already so)
	struct { GLuint prog; int depthOn, depthFunc, depthMask, blendOn, blendSrc, blendDst, blendEq, logicOn, logicOp, cull, cr, cg, cb, ca; GLint vp[4]; float dr[2]; GLint sc[4]; GLuint tex[8], samp[8]; } cur;

	GLuint compile (const char *vs, const char *fs, const char *defs, bool wait = true);
	GLuint gxProgram (const char *fs, const char *defs);
	bool gxReady (GLuint p);
	GLuint program (const GxState &s);
	struct Prog { GLuint p; bool pending; };
	std::unordered_map<u64, Prog> progs;			// (the TEV configurations' programs)
	bool parallel = false;					// (the driver compiles them in the background)
public:
	int programs = 0;
private:
	void makeTargets ();
	void freeTargets ();
	void begin ();
	void flush ();
	void upload (Machine &m, int t);
	void bindTex (int unit, const GxState &s);
	u32 put (Ring &r, const void *d, u32 bytes, u32 align);
	void fenceRings ();
	GLuint sampler (u32 m0, u32 m1, int levels);
	std::unordered_map<u64, GLuint> samplers;			// (the maps' sampling modes)
	void resetCache () { memset (&cur, 0xFF, sizeof cur); }
};

GLuint GxGl::compile (const char *vs, const char *fs, const char *defs, bool wait)
{
	GLuint sh[2] = { glCreateShader (GL_VERTEX_SHADER), glCreateShader (GL_FRAGMENT_SHADER) };
	const char *srcs[2] = { vs, fs };
	bool gx = vs == VS;						// (the GX's programs: the uniform blocks)
	for (int i = 0; i < 2; i++)
	{
		const char *parts[4] = { "#version 330 core\n", i ? defs : "", gx ? BLOCKS : "", srcs[i] };
		glShaderSource (sh[i], 4, parts, 0);
		glCompileShader (sh[i]);
		if (!wait) continue;					// (its errors: the link's)
		GLint ok = 0; glGetShaderiv (sh[i], GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			int k = snprintf (err, sizeof err, "%s shader: ", i ? "fragment" : "vertex");
			glGetShaderInfoLog (sh[i], (GLsizei) (sizeof err - (size_t) k), 0, err + k);
			return 0;
		}
	}
	GLuint p = glCreateProgram ();
	glAttachShader (p, sh[0]); glAttachShader (p, sh[1]);
	if (gx) { glBindFragDataLocationIndexed (p, 0, 0, "oCol"); glBindFragDataLocationIndexed (p, 0, 1, "oSrc"); }
	glLinkProgram (p);
	glDeleteShader (sh[0]); glDeleteShader (sh[1]);
	if (!wait) return p;
	GLint ok = 0; glGetProgramiv (p, GL_LINK_STATUS, &ok);
	if (!ok) { int k = snprintf (err, sizeof err, "shader link: "); glGetProgramInfoLog (p, (GLsizei) (sizeof err - (size_t) k), 0, err + k); return 0; }
	return p;
}

// a program of the GX: the uniform blocks bound, the samplers on their units
GLuint GxGl::gxProgram (const char *fs, const char *defs)
{
	GLuint p = compile (VS, fs, defs);
	return p && gxReady (p) ? p : 0;
}

// a linked program set up (its blocks, its samplers); false: its link failed
bool GxGl::gxReady (GLuint p)
{
	GLint ok = 0; glGetProgramiv (p, GL_LINK_STATUS, &ok);
	if (!ok) { int k = snprintf (err, sizeof err, "shader link: "); glGetProgramInfoLog (p, (GLsizei) (sizeof err - (size_t) k), 0, err + k); return false; }
	glUniformBlockBinding (p, glGetUniformBlockIndex (p, "XF"), 0);
	glUniformBlockBinding (p, glGetUniformBlockIndex (p, "ST"), 1);
	glUseProgram (p);
	for (int i = 0; i < 8; i++) { char n[4] = { 's', (char) ('0' + i), 0, 0 }; glUniform1i (glGetUniformLocation (p, n), i); }
	cur.prog = p;
	return true;
}

// The program of a draw's TEV configuration: the ubershader's code with its stages unrolled and
// the state's choices as constants (the GPU's compiler folds the switches away), made once --
// much faster than the ubershader per pixel (an integrated GPU's). The ubershader if it fails.
GLuint GxGl::program (const GxState &s)
{
	s32 k[28 + 16 * 5] = {};
	int n = s.gen[0] < 1 ? 1 : s.gen[0] > 16 ? 16 : s.gen[0];
	k[0] = n; k[1] = s.gen[1]; k[2] = s.gen[2]; k[3] = s.alpha[0]; k[4] = s.alpha[1]; k[5] = s.fogI[0]; k[6] = s.fogI[1];
	k[7] = s.fogK[10] != 0; k[8] = s.gen[3]; k[9] = (s.zenv[3] & 0x100) != 0; k[10] = s.zenv[0]; k[11] = s.zenv[1];
	memcpy (k + 12, s.swap, sizeof s.swap);
	for (int st = 0; st < n; st++)
	{
		s32 *q = k + 28 + st * 5;
		q[0] = s.tevC[st]; q[1] = s.tevA[st]; q[2] = s.tref[st]; q[3] = s.ksel[st]; q[4] = s.ind[st];
	}
	u64 h = 1469598103934665603ull;
	for (size_t i = 0; i < sizeof k; i++) { h ^= ((const u8 *) k)[i]; h *= 1099511628211ull; }
	GLuint uber = prog[s.zenv[0] != 0 ? 1 : 0];
	auto it = progs.find (h);
	if (it != progs.end ())
	{
		Prog &e = it->second;
		if (!e.pending) return e.p;
		GLint done = 0;
		glGetProgramiv (e.p, GL_COMPLETION_STATUS, &done);
		if (!done) return uber;					// (compiling: the ubershader meanwhile)
		e.pending = false;
		if (gxReady (e.p)) programs++;
		else { glDeleteProgram (e.p); e.p = uber; }
		return e.p;
	}
	char defs[1024];
	int m = snprintf (defs, sizeof defs, "#define NSTAGES %d\n#define NIND %d\n#define ALOGIC %d\n#define AF0 %d\n#define AF1 %d\n"
		"#define FOGSEL %d\n#define FOGPROJ %d\n#define FOGRANGE %s\n#define EFBFMT %d\n#define DSTA %s\n#define ZOP %d\n#define ZFMT %d\n%s"
		"const ivec4 SWPT[4] = ivec4[4] (ivec4 (%d, %d, %d, %d), ivec4 (%d, %d, %d, %d), ivec4 (%d, %d, %d, %d), ivec4 (%d, %d, %d, %d));\n",
		k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7] ? "true" : "false", k[8], k[9] ? "true" : "false", k[10], k[11], k[10] ? "#define ZTEX 1\n" : "",
		k[12], k[13], k[14], k[15], k[16], k[17], k[18], k[19], k[20], k[21], k[22], k[23], k[24], k[25], k[26], k[27]);
	(void) m;
	std::string fs = std::string (FS_HEAD) + FS_INIT;
	for (int st = 0; st < n; st++)
	{
		const s32 *q = k + 28 + st * 5;
		char c[200];
		snprintf (c, sizeof c, "\t{ const int st = %d; const int CENV = %d; const int AENV = %d; const int TREF = %d; const int KSEL = %d; const int INDC = %d;\n",
			st, q[0], q[1], q[2], q[3], q[4]);
		fs += c; fs += FS_STAGE; fs += "\t}\n";
	}
	fs += FS_END;
	if (parallel)							// (compiled in the background: the ubershader until then)
	{
		GLuint p = compile (VS, fs.c_str (), defs, false);
		progs[h] = Prog { p ? p : uber, p != 0 };
		return uber;
	}
	GLuint p = gxProgram (fs.c_str (), defs);
	if (!p) p = uber;
	else programs++;
	progs[h] = Prog { p, false };
	return p;
}

bool GxGl::init (int scale)
{
	err[0] = 0;
	spec = !(getenv ("NEMU_GX_UBER") && atoi (getenv ("NEMU_GX_UBER")));
	const char *ver = (const char *) glGetString (GL_VERSION), *ren = (const char *) glGetString (GL_RENDERER);
	snprintf (name, sizeof name, "%s (OpenGL %s)", ren ? ren : "?", ver ? ver : "?");
	int major = ver ? ver[0] - '0' : 0, minor = ver && ver[1] == '.' ? ver[2] - '0' : 0;
	if (major < 3 || (major == 3 && minor < 3)) { snprintf (err, sizeof err, "OpenGL 3.3 is needed (%s)", ver ? ver : "?"); return false; }
#define L(name) name = (T_##name) glproc (#name); if (!name) { snprintf (err, sizeof err, "no %s", #name); return false; }
	L (glGenBuffers) L (glDeleteBuffers) L (glBindBuffer) L (glBufferData) L (glBufferSubData) L (glBindBufferRange)
	L (glGenVertexArrays) L (glDeleteVertexArrays) L (glBindVertexArray) L (glEnableVertexAttribArray) L (glVertexAttribPointer)
	L (glVertexAttribIPointer) L (glCreateShader) L (glDeleteShader) L (glShaderSource) L (glCompileShader) L (glGetShaderiv)
	L (glGetShaderInfoLog) L (glCreateProgram) L (glDeleteProgram) L (glAttachShader) L (glLinkProgram) L (glGetProgramiv)
	L (glGetProgramInfoLog) L (glUseProgram) L (glGetUniformLocation) L (glGetUniformBlockIndex) L (glUniformBlockBinding)
	L (glUniform1i) L (glUniform2f) L (glUniform4f) L (glUniform4i) L (glGenFramebuffers) L (glDeleteFramebuffers)
	L (glBindFramebuffer) L (glFramebufferTexture2D) L (glCheckFramebufferStatus) L (glBlitFramebuffer) L (glActiveTexture)
	L (glBlendFuncSeparate) L (glBlendEquation) L (glBindFragDataLocationIndexed) L (glDrawElementsBaseVertex)
	L (glMapBufferRange) L (glUnmapBuffer) L (glGenSamplers) L (glDeleteSamplers) L (glBindSampler) L (glSamplerParameteri) L (glSamplerParameterf)
	L (glFenceSync) L (glClientWaitSync) L (glDeleteSync)
#undef L
	glBufferStorage = (T_glBufferStorage) glproc ("glBufferStorage");	// (4.4: the persistent mappings)
	// the specialized programs compiled by the driver's threads, if it can
	glGetStringi = (T_glGetStringi) glproc ("glGetStringi");
	glMaxShaderCompilerThreadsKHR = (T_glMaxShaderCompilerThreadsKHR) glproc ("glMaxShaderCompilerThreadsKHR");
	if (!glMaxShaderCompilerThreadsKHR) glMaxShaderCompilerThreadsKHR = (T_glMaxShaderCompilerThreadsKHR) glproc ("glMaxShaderCompilerThreadsARB");
	if (glGetStringi && glMaxShaderCompilerThreadsKHR)
	{
		GLint ne = 0; glGetIntegerv (GL_NUM_EXTENSIONS, &ne);
		for (GLint i = 0; i < ne && !parallel; i++)
		{
			const char *x = (const char *) glGetStringi (GL_EXTENSIONS, (GLuint) i);
			if (x && (!strcmp (x, "GL_KHR_parallel_shader_compile") || !strcmp (x, "GL_ARB_parallel_shader_compile"))) parallel = true;
		}
		if (parallel) glMaxShaderCompilerThreadsKHR (0xFFFFFFFFu);
	}
	if (getenv ("NEMU_GX_SYNC") && atoi (getenv ("NEMU_GX_SYNC"))) parallel = false;
	if (parallel) strncat (name, ", shaders compiled in parallel", sizeof name - strlen (name) - 1);
	// the programs: the GX's (without / with a z texture), the copies', the picture's
	std::string uber = std::string (FS_UBER_DEFS) + FS_HEAD + FS_INIT + FS_UBER_LOOP + FS_STAGE + "\t}\n" + FS_END;
	for (int z = 0; z < 2; z++)
		if (!(prog[z] = gxProgram (uber.c_str (), z ? "#define ZTEX 1\n" : ""))) return false;
	copyProg = compile (QUAD_VS, COPY_FS, "");
	presentProg = compile (QUAD_VS, PRESENT_FS, "");
	if (!copyProg || !presentProg) return false;
	glUseProgram (copyProg);
	glUniform1i (glGetUniformLocation (copyProg, "src"), 0);
	uCopyRect = glGetUniformLocation (copyProg, "rect"); uCopySize = glGetUniformLocation (copyProg, "srcSize"); uCopyMode = glGetUniformLocation (copyProg, "mode");
	glUseProgram (presentProg);
	glUniform1i (glGetUniformLocation (presentProg, "src"), 0);
	uPresentScale = glGetUniformLocation (presentProg, "scaleUV");
	glUseProgram (0);
	// the buffers (rings: written ahead, orphaned when full) and the vertices' layout
	glGetIntegerv (GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uboAlign);
	if (uboAlign < 16) uboAlign = 256;
	Ring *rings[3] = { &vbo, &ibo, &ubo };
	const GLenum targets[3] = { GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, GL_UNIFORM_BUFFER };
	const u32 sizes[3] = { 32u << 20, 8u << 20, 16u << 20 };
	glGenVertexArrays (1, &vao); glGenVertexArrays (1, &quadVao);
	glBindVertexArray (vao);
	for (int i = 0; i < 3; i++)
	{
		Ring &r = *rings[i];
		r.target = targets[i]; r.size = sizes[i]; r.pos = 0; r.map = 0; r.quarter = 0; r.left = 0;
		for (int q = 0; q < 4; q++) r.fence[q] = 0;
		glGenBuffers (1, &r.buf);
		glBindBuffer (r.target, r.buf);
		if (glBufferStorage && !(getenv ("NEMU_GX_NOPERSIST") && atoi (getenv ("NEMU_GX_NOPERSIST"))))
		{
			const GLbitfield f = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
			glBufferStorage (r.target, r.size, 0, f);
			r.map = (u8 *) glMapBufferRange (r.target, 0, r.size, f);
		}
		if (!r.map) glBufferData (r.target, r.size, 0, GL_STREAM_DRAW);
	}
	glBindBuffer (GL_ARRAY_BUFFER, vbo.buf);
	const GLsizei st = (GLsizei) sizeof (GxVertex);
	glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, st, (void *) offsetof (GxVertex, pos));
	glVertexAttribPointer (1, 3, GL_FLOAT, GL_FALSE, st, (void *) offsetof (GxVertex, nrm));
	glVertexAttribPointer (2, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void *) offsetof (GxVertex, c0));
	glVertexAttribPointer (3, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void *) offsetof (GxVertex, c1));
	for (int k = 0; k < 4; k++) glVertexAttribPointer (4 + k, 4, GL_FLOAT, GL_FALSE, st, (void *) (offsetof (GxVertex, tc) + (size_t) k * 16));
	for (int k = 0; k < 3; k++) glVertexAttribIPointer (8 + k, 4, GL_UNSIGNED_BYTE, st, (void *) (offsetof (GxVertex, mtx) + (size_t) k * 4));
	for (int k = 0; k < 11; k++) glEnableVertexAttribArray (k);
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, ibo.buf);
	glBindVertexArray (0);
	bv = new GxVertex[CAPV]; bi = new u32[CAPI];
	// a texture for what is missing (a copy made before we were here)
	glGenTextures (1, &dummy);
	glBindTexture (GL_TEXTURE_2D, dummy);
	u32 black = 0;
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, &black);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	S = scale < 1 ? 1 : scale > 4 ? 4 : scale;
	makeTargets ();
	if (err[0]) return false;
	resetCache ();
	return true;
}

// the EFB (colour + depth) and the XFB's picture, at the scale
void GxGl::makeTargets ()
{
	int w = EFB_W * S, h = EFB_H * S;
	glGenTextures (1, &efbColor); glGenTextures (1, &efbDepth); glGenTextures (1, &xfbTex);
	glBindTexture (GL_TEXTURE_2D, efbColor);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture (GL_TEXTURE_2D, efbDepth);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture (GL_TEXTURE_2D, xfbTex);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenFramebuffers (1, &efbFbo); glGenFramebuffers (1, &xfbFbo);
	glBindFramebuffer (GL_FRAMEBUFFER, xfbFbo);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xfbTex, 0);
	glBindFramebuffer (GL_FRAMEBUFFER, efbFbo);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, efbColor, 0);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, efbDepth, 0);
	if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) snprintf (err, sizeof err, "the EFB's framebuffer is incomplete");
	glDisable (GL_SCISSOR_TEST);
	glColorMask (1, 1, 1, 1); glDepthMask (1);
	glClearColor (0, 0, 0, 1); glClearDepth (1.0);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	hasPicture = false;
	resetCache ();
	bound = false;
}

void GxGl::freeTargets ()
{
	GLuint t[3] = { efbColor, efbDepth, xfbTex }, f[2] = { efbFbo, xfbFbo };
	glDeleteTextures (3, t); glDeleteFramebuffers (2, f);
	for (int i = 0; i < GX_COPIES; i++)
		if (copies[i].tex) { glDeleteTextures (1, &copies[i].tex); glDeleteFramebuffers (1, &copies[i].fbo); copies[i] = CopyTex {}; }
	efbColor = efbDepth = xfbTex = efbFbo = xfbFbo = 0;
}

void GxGl::shutdown ()
{
	if (!bv) return;
	freeTargets ();
	for (int i = 0; i < Machine::MAX_TEX; i++) if (texObj[i]) glDeleteTextures (1, &texObj[i]);
	glDeleteTextures (1, &dummy);
	for (auto &e : samplers) glDeleteSamplers (1, &e.second);
	samplers.clear ();
	Ring *rings[3] = { &vbo, &ibo, &ubo };
	for (Ring *r : rings)
	{
		for (int q = 0; q < 4; q++) if (r->fence[q]) glDeleteSync (r->fence[q]);
		if (r->map) { glBindBuffer (r->target, r->buf); glUnmapBuffer (r->target); }
	}
	GLuint b[3] = { vbo.buf, ibo.buf, ubo.buf };
	glDeleteBuffers (3, b);
	GLuint a[2] = { vao, quadVao };
	glDeleteVertexArrays (2, a);
	for (auto &e : progs) if (e.second.p != prog[0] && e.second.p != prog[1]) glDeleteProgram (e.second.p);
	progs.clear ();
	for (int z = 0; z < 2; z++) if (prog[z]) glDeleteProgram (prog[z]);
	if (copyProg) glDeleteProgram (copyProg);
	if (presentProg) glDeleteProgram (presentProg);
	delete [] bv; delete [] bi; bv = 0; bi = 0;
}

void GxGl::setScale (int s)
{
	s = s < 1 ? 1 : s > 4 ? 4 : s;
	if (s == S) return;
	flush ();
	freeTargets ();
	S = s;
	makeTargets ();
}

// our state on (after the front end's own drawing)
void GxGl::begin ()
{
	if (bound) return;
	glBindFramebuffer (GL_FRAMEBUFFER, efbFbo);
	glBindVertexArray (vao);
	resetCache ();
	bound = true;
}

void GxGl::release ()
{
	flush ();
	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	glBindVertexArray (0);
	glUseProgram (0);
	for (int i = 7; i >= 0; i--) { glActiveTexture (GL_TEXTURE0 + i); glBindTexture (GL_TEXTURE_2D, 0); glBindSampler ((GLuint) i, 0); }
	glDisable (GL_SCISSOR_TEST); glDisable (GL_DEPTH_TEST); glDisable (GL_BLEND); glDisable (GL_CULL_FACE); glDisable (GL_COLOR_LOGIC_OP);
	glColorMask (1, 1, 1, 1); glDepthMask (1); glBlendEquation (GL_FUNC_ADD); glDepthRange (0.0, 1.0);
	bound = false;
}

u32 GxGl::put (Ring &r, const void *d, u32 bytes, u32 align)
{
	u32 p = (r.pos + align - 1) / align * align;
	if (r.map)
	{
		if (p + bytes > r.size) p = 0;
		// the quarters it enters: the one it leaves fenced after the next draw, these waited for
		for (u32 q = p * 4 / r.size, qe = (p + bytes - 1) * 4 / r.size; q <= qe; q++)
		{
			if (q == r.quarter) continue;
			r.left |= 1u << r.quarter;
			if (r.fence[q]) { glClientWaitSync (r.fence[q], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull); glDeleteSync (r.fence[q]); r.fence[q] = 0; }
			r.quarter = q;
		}
		memcpy (r.map + p, d, bytes);
		r.pos = p + bytes;
		return p;
	}
	glBindBuffer (r.target, r.buf);
	if (p + bytes > r.size) { glBufferData (r.target, r.size, 0, GL_STREAM_DRAW); p = 0; }	// (orphaned: the GPU keeps the old one)
	void *m = glMapBufferRange (r.target, p, bytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
	if (m) { memcpy (m, d, bytes); glUnmapBuffer (r.target); }
	else glBufferSubData (r.target, p, bytes, d);
	r.pos = p + bytes;
	return p;
}

// after a draw: the rings' quarters left behind fenced (what the GPU still reads in them)
void GxGl::fenceRings ()
{
	Ring *rings[3] = { &vbo, &ibo, &ubo };
	for (Ring *r : rings)
		for (u32 q = 0; r->left && q < 4; q++)
			if (r->left & (1u << q))
			{
				if (r->fence[q]) glDeleteSync (r->fence[q]);
				r->fence[q] = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
				r->left &= ~(1u << q);
			}
}

// a texture of the cache (and its mipmaps) into its GL texture
void GxGl::upload (Machine &m, int t)
{
	GTexture &T = m.tex[t];
	if (!texObj[t]) glGenTextures (1, &texObj[t]);
	glActiveTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, texObj[t]); cur.tex[0] = texObj[t];		// (its unit 0: the cache knows)
	glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
	const u32 *px = T.px;
	int levels = T.levels < 1 ? 1 : T.levels;
	for (int l = 0; l < levels; l++)
	{
		int lw = T.w >> l ? T.w >> l : 1, lh = T.h >> l ? T.h >> l : 1;
		glTexImage2D (GL_TEXTURE_2D, l, GL_RGBA8, lw, lh, 0, GL_BGRA, GL_UNSIGNED_BYTE, px);
		px += lw * lh;
	}
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
	texLevels[t] = levels;
	T.dirty = false;
}

// a sampling mode (TX_SETMODE0 / 1: the wrapping, the filters, the LOD) -> its sampler object
GLuint GxGl::sampler (u32 m0, u32 m1, int levels)
{
	u64 key = (u64) (m0 & 0x1FFFF) | (u64) (m1 & 0xFFFF) << 17 | (u64) (levels > 1) << 33;
	auto it = samplers.find (key);
	if (it != samplers.end ()) return it->second;
	GLuint sm = 0;
	glGenSamplers (1, &sm);
	static const GLint wrap[4] = { GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT };
	glSamplerParameteri (sm, GL_TEXTURE_WRAP_S, wrap[m0 & 3]);
	glSamplerParameteri (sm, GL_TEXTURE_WRAP_T, wrap[(m0 >> 2) & 3]);
	int minf = (int) (m0 >> 5) & 7;
	static const GLint mins[8] = { GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST, GL_LINEAR, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR };
	glSamplerParameteri (sm, GL_TEXTURE_MIN_FILTER, levels > 1 ? mins[minf] : (minf >= 4 ? GL_LINEAR : GL_NEAREST));
	glSamplerParameteri (sm, GL_TEXTURE_MAG_FILTER, (m0 & 16) ? GL_LINEAR : GL_NEAREST);
	if (levels > 1)
	{
		glSamplerParameterf (sm, GL_TEXTURE_LOD_BIAS, (float) (s8) ((m0 >> 9) & 0xFF) / 32.f);
		glSamplerParameterf (sm, GL_TEXTURE_MIN_LOD, (float) (m1 & 0xFF) / 16.f);
		glSamplerParameterf (sm, GL_TEXTURE_MAX_LOD, (float) ((m1 >> 8) & 0xFF) / 16.f);
	}
	samplers[key] = sm;
	return sm;
}

// a map's texture and its sampler
void GxGl::bindTex (int unit, const GxState &s)
{
	int t = s.tex[unit];
	if (t < 0) return;
	GLuint obj = dummy; int levels = 1;
	if (t >= GX_TEX_COPY) { if (copies[t - GX_TEX_COPY].tex) obj = copies[t - GX_TEX_COPY].tex; }
	else if (texObj[t]) { obj = texObj[t]; levels = texLevels[t]; }
	GLuint sm = sampler (s.texMode[unit][0], s.texMode[unit][1], levels);
	if (cur.tex[unit] != obj) { glActiveTexture (GL_TEXTURE0 + unit); glBindTexture (GL_TEXTURE_2D, obj); cur.tex[unit] = obj; }
	if (cur.samp[unit] != sm) { glBindSampler ((GLuint) unit, sm); cur.samp[unit] = sm; }
}

void GxGl::draw (Machine &m, const GxState &s, const GxVertex *v, int nv, const u32 *idx, int ni, int pr)
{
	begin ();
	// the textures it reads, uploaded (a texture decoded again: what the batch drew with it first)
	for (int k = 0; k < 8; k++)
	{
		int t = s.tex[k];
		if (t >= 0 && t < GX_TEX_COPY && (m.tex[t].dirty || !texObj[t])) { flush (); upload (m, t); }
	}
	if (open && (serial != s.serial || xfSerial != m.xfSerial || prim != pr || bnv + nv > CAPV || bni + ni > CAPI)) flush ();
	if (nv > CAPV || ni > CAPI) return;
	if (!open)
	{
		open = true; serial = s.serial; xfSerial = m.xfSerial; prim = pr; bnv = bni = 0;
		bs = s;
		// the XF memory: the matrices (0x000-0x0FF), the normal ones (0x400-0x45F), the post ones
		// (0x500-0x5FF), the lights (0x600-0x67F)
		memcpy (bxf, m.xfRegs, 256 * 4);
		memcpy (bxf + 256, m.xfRegs + 0x400, 96 * 4);
		memcpy (bxf + 352, m.xfRegs + 0x500, 256 * 4);
		memcpy (bxf + 608, m.xfRegs + 0x600, 128 * 4);
	}
	memcpy (bv + bnv, v, (size_t) nv * sizeof (GxVertex));
	for (int i = 0; i < ni; i++) bi[bni + i] = idx[i] + (u32) bnv;
	bnv += nv; bni += ni;
}

static const GLenum ZFUNC[8] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };

void GxGl::flush ()
{
	if (!open) return;
	open = false;
	if (!bni) return;
	GxState &s = bs;
	s.vp[2] = (float) S;
	u32 vOff = put (vbo, bv, (u32) bnv * (u32) sizeof (GxVertex), (u32) sizeof (GxVertex));
	u32 iOff = put (ibo, bi, (u32) bni * 4, 4);
	u32 xOff = put (ubo, bxf, XF_BYTES, (u32) uboAlign);
	u32 sOff = put (ubo, &s, GX_UBO_BYTES, (u32) uboAlign);
	glBindBufferRange (GL_UNIFORM_BUFFER, 0, ubo.buf, xOff, XF_BYTES);
	glBindBufferRange (GL_UNIFORM_BUFFER, 1, ubo.buf, sOff, GX_UBO_BYTES);
	GLuint p = spec ? program (s) : prog[s.zenv[0] != 0 ? 1 : 0];
	if (cur.prog != p) { glUseProgram (p); cur.prog = p; }
	// the viewport, the depth range, the scissor (the EFB's pixels x the scale)
	GLint vp[4] = { (GLint) (s.viewport[0] * S), (GLint) (s.viewport[1] * S), (GLint) (s.viewport[2] * S), (GLint) (s.viewport[3] * S) };
	if (memcmp (vp, cur.vp, sizeof vp)) { glViewport (vp[0], vp[1], vp[2] > 0 ? vp[2] : 1, vp[3] > 0 ? vp[3] : 1); memcpy (cur.vp, vp, sizeof vp); }
	if (s.viewport[4] != cur.dr[0] || s.viewport[5] != cur.dr[1]) { glDepthRange (s.viewport[4], s.viewport[5]); cur.dr[0] = s.viewport[4]; cur.dr[1] = s.viewport[5]; }
	GLint sc[4] = { s.scissor[0] * S, s.scissor[1] * S, (s.scissor[2] - s.scissor[0]) * S, (s.scissor[3] - s.scissor[1]) * S };
	if (sc[0] < 0) { sc[2] += sc[0]; sc[0] = 0; }
	if (sc[1] < 0) { sc[3] += sc[1]; sc[1] = 0; }
	if (sc[2] < 0) sc[2] = 0;
	if (sc[3] < 0) sc[3] = 0;
	if (memcmp (sc, cur.sc, sizeof sc)) { glEnable (GL_SCISSOR_TEST); glScissor (sc[0], sc[1], sc[2], sc[3]); memcpy (cur.sc, sc, sizeof sc); }
	// the depth
	int zon = s.zmode & 1, zf = (int) (s.zmode >> 1) & 7, zm = zon && (s.zmode & 0x10) ? 1 : 0;
	if (zon != cur.depthOn) { if (zon) glEnable (GL_DEPTH_TEST); else glDisable (GL_DEPTH_TEST); cur.depthOn = zon; }
	if (zon && zf != cur.depthFunc) { glDepthFunc (ZFUNC[zf]); cur.depthFunc = zf; }
	if (zm != cur.depthMask) { glDepthMask ((GLboolean) zm); cur.depthMask = zm; }
	// the colour / alpha writes, the blending (the source's alpha: the second output), the logic op
	u32 cm = s.cmode0;
	int fmt = (int) (s.peCtrl & 7);
	bool hasAlpha = fmt == 1;
	int cw = (cm >> 3) & 1, aw = hasAlpha && ((cm >> 4) & 1) ? 1 : 0;
	if (cw != cur.cr || aw != cur.ca) { glColorMask ((GLboolean) cw, (GLboolean) cw, (GLboolean) cw, (GLboolean) aw); cur.cr = cw; cur.ca = aw; }
	int bon = 0, bsrc = 0, bdst = 0, beq = GL_FUNC_ADD, lon = 0;
	if (cm & 0x800) { bon = 1; bsrc = GL_ONE; bdst = GL_ONE; beq = GL_FUNC_REVERSE_SUBTRACT; }
	else if (cm & 1)
	{
		static const GLenum SRC[8] = { GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
		static const GLenum DST[8] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
		bon = 1; bsrc = (int) SRC[(cm >> 8) & 7]; bdst = (int) DST[(cm >> 5) & 7];
		if (!hasAlpha)						// (no alpha in the EFB: it reads 1)
		{
			if (bsrc == GL_DST_ALPHA) bsrc = GL_ONE; else if (bsrc == GL_ONE_MINUS_DST_ALPHA) bsrc = GL_ZERO;
			if (bdst == GL_DST_ALPHA) bdst = GL_ONE; else if (bdst == GL_ONE_MINUS_DST_ALPHA) bdst = GL_ZERO;
		}
	}
	else if (cm & 2) lon = 1;
	if (bon != cur.blendOn) { if (bon) glEnable (GL_BLEND); else glDisable (GL_BLEND); cur.blendOn = bon; }
	if (bon && (bsrc != cur.blendSrc || bdst != cur.blendDst)) { glBlendFunc ((GLenum) bsrc, (GLenum) bdst); cur.blendSrc = bsrc; cur.blendDst = bdst; }
	if (bon && beq != cur.blendEq) { glBlendEquation ((GLenum) beq); cur.blendEq = beq; }
	if (lon != cur.logicOn) { if (lon) glEnable (GL_COLOR_LOGIC_OP); else glDisable (GL_COLOR_LOGIC_OP); cur.logicOn = lon; }
	if (lon && (int) ((cm >> 12) & 15) != cur.logicOp) { glLogicOp (GL_CLEAR + ((cm >> 12) & 15)); cur.logicOp = (int) ((cm >> 12) & 15); }
	// the culling (triangles only; GX's front: clockwise on the screen)
	int cull = prim == GX_TRIANGLES ? (int) s.cull : 0;
	if (cull != cur.cull)
	{
		if (!cull) glDisable (GL_CULL_FACE);
		else { glEnable (GL_CULL_FACE); glCullFace (cull == 1 ? GL_BACK : cull == 2 ? GL_FRONT : GL_FRONT_AND_BACK); }
		cur.cull = cull;
	}
	GLenum mode = GL_TRIANGLES;
	if (prim == GX_LINES) { mode = GL_LINES; glLineWidth ((float) (s.lpSize & 0xFF) / 6.f * (float) S); }
	else if (prim == GX_POINTS) { mode = GL_POINTS; glPointSize ((float) ((s.lpSize >> 8) & 0xFF) / 6.f * (float) S); }
	for (int k = 0; k < 8; k++) bindTex (k, s);
	glDrawElementsBaseVertex (mode, bni, GL_UNSIGNED_INT, (void *) (size_t) iOff, (GLint) (vOff / (u32) sizeof (GxVertex)));
	fenceRings ();
}

void GxGl::copy (Machine &, const GxCopy &c)
{
	begin ();
	flush ();
	int x = c.x * S, y = c.y * S, w = c.w * S, h = c.h * S;
	if (c.toXfb)
	{
		if (w > EFB_W * S) w = EFB_W * S;
		if (h > EFB_H * S) h = EFB_H * S;
		glBindFramebuffer (GL_READ_FRAMEBUFFER, efbFbo);
		glBindFramebuffer (GL_DRAW_FRAMEBUFFER, xfbFbo);
		glDisable (GL_SCISSOR_TEST); cur.sc[0] = -1;
		glBlitFramebuffer (x, y, x + w, y + h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer (GL_FRAMEBUFFER, efbFbo);
		picW = w; picH = h; hasPicture = true;
	}
	else if (c.slot >= 0 && c.slot < GX_COPIES)
	{
		CopyTex &t = copies[c.slot];
		int dw = (c.half ? c.w / 2 : c.w) * S, dh = (c.half ? c.h / 2 : c.h) * S;
		if (dw < 1) dw = 1;
		if (dh < 1) dh = 1;
		if (!t.tex || t.w != dw || t.h != dh)
		{
			if (!t.tex) { glGenTextures (1, &t.tex); glGenFramebuffers (1, &t.fbo); }
			glActiveTexture (GL_TEXTURE0); cur.tex[0] = t.tex;
			glBindTexture (GL_TEXTURE_2D, t.tex);
			glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, dw, dh, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			glBindFramebuffer (GL_FRAMEBUFFER, t.fbo);
			glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
			t.w = dw; t.h = dh;
		}
		glBindFramebuffer (GL_FRAMEBUFFER, t.fbo);
		glViewport (0, 0, dw, dh); cur.vp[0] = -1;
		glDisable (GL_SCISSOR_TEST); cur.sc[0] = -1;
		glDisable (GL_DEPTH_TEST); cur.depthOn = 0;
		glDisable (GL_BLEND); cur.blendOn = 0;
		glDisable (GL_CULL_FACE); cur.cull = 0;
		glDisable (GL_COLOR_LOGIC_OP); cur.logicOn = 0;
		glColorMask (1, 1, 1, 1); cur.cr = cur.ca = 1;
		glUseProgram (copyProg); cur.prog = copyProg;
		glBindSampler (0, 0); cur.samp[0] = 0;
		glActiveTexture (GL_TEXTURE0);
		GLuint src = c.depth ? efbDepth : efbColor;
		glBindTexture (GL_TEXTURE_2D, src); cur.tex[0] = src;
		GLint f = c.half && !c.depth ? GL_LINEAR : GL_NEAREST;
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
		glUniform4f (uCopyRect, (float) x, (float) y, (float) w, (float) h);
		glUniform2f (uCopySize, (float) (EFB_W * S), (float) (EFB_H * S));
		glUniform4i (uCopyMode, (int) c.fmt, c.depth ? 1 : 0, c.intensity ? 1 : 0, c.efbFmt == 1 ? 1 : 0);
		glBindVertexArray (quadVao);
		glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
		glBindVertexArray (vao);
		glBindFramebuffer (GL_FRAMEBUFFER, efbFbo);
	}
	// the EFB cleared after (its masks: the colour, the alpha if it has one, the depth)
	if (c.clear)
	{
		bool ca = c.alphaMask && c.efbFmt == 1;
		GLbitfield mask = (c.colorMask || ca ? GL_COLOR_BUFFER_BIT : 0) | (c.zMask ? GL_DEPTH_BUFFER_BIT : 0);
		if (mask)
		{
			glEnable (GL_SCISSOR_TEST); glScissor (x, y, w, h); cur.sc[0] = -1;
			glColorMask ((GLboolean) c.colorMask, (GLboolean) c.colorMask, (GLboolean) c.colorMask, (GLboolean) ca); cur.cr = -1;
			glDepthMask ((GLboolean) c.zMask); cur.depthMask = -1;
			u32 cc = c.clearColor;
			glClearColor (((cc >> 16) & 255) / 255.f, ((cc >> 8) & 255) / 255.f, (cc & 255) / 255.f, (cc >> 24) / 255.f);
			glClearDepth ((double) c.clearZ / 16777216.0);
			glClear (mask);
		}
	}
}

bool GxGl::present (int x, int y, int w, int h, int W, int H)
{
	flush ();
	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	glDisable (GL_SCISSOR_TEST); glDisable (GL_DEPTH_TEST); glDisable (GL_BLEND); glDisable (GL_CULL_FACE); glDisable (GL_COLOR_LOGIC_OP);
	glColorMask (1, 1, 1, 1);
	glViewport (0, 0, W, H);
	glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT);
	bool ok = hasPicture;
	if (ok)
	{
		glViewport (x, H - y - h, w, h);
		glUseProgram (presentProg);
		glBindSampler (0, 0);
		glActiveTexture (GL_TEXTURE0);
		glBindTexture (GL_TEXTURE_2D, xfbTex);
		glUniform2f (uPresentScale, (float) picW / (float) (EFB_W * S), (float) picH / (float) (EFB_H * S));
		glBindVertexArray (quadVao);
		glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
	}
	glBindVertexArray (0);
	glUseProgram (0);
	bound = false;
	return ok;
}

// ---- for nemucore.cpp ------------------------------------------------------------------------------------------
GxGpu *gxgl_create (int scale, char *err, int cap)
{
	GxGl *g = new GxGl ();
	if (!g->init (scale))
	{
		if (err && cap > 0) { strncpy (err, g->err, (size_t) cap - 1); err[cap - 1] = 0; }
		delete g;
		return 0;
	}
	return g;
}
void gxgl_destroy (GxGpu *g) { delete (GxGl *) g; }
bool gxgl_present (GxGpu *g, int x, int y, int w, int h, int W, int H) { return ((GxGl *) g)->present (x, y, w, h, W, H); }
void gxgl_release (GxGpu *g) { ((GxGl *) g)->release (); }
void gxgl_set_scale (GxGpu *g, int scale) { ((GxGl *) g)->setScale (scale); }
// the last picture (0xAARRGGBB, rows top first) -> its size, 0 if none
int gxgl_read (GxGpu *g, unsigned *px, int cap, int *w, int *h) { return ((GxGl *) g)->read (px, cap, w, h); }
const char *gxgl_name (GxGpu *g) { return ((GxGl *) g)->name; }

int GxGl::read (unsigned *px, int cap, int *w, int *h)
{
	if (!hasPicture || picW * picH > cap) return 0;
	flush ();
	glBindFramebuffer (GL_FRAMEBUFFER, xfbFbo);
	glPixelStorei (GL_PACK_ALIGNMENT, 4);
	glReadPixels (0, 0, picW, picH, GL_BGRA, GL_UNSIGNED_BYTE, px);
	glBindFramebuffer (GL_FRAMEBUFFER, bound ? efbFbo : 0);
	*w = picW; *h = picH;
	return picW * picH;
}

#else
gc::GxGpu *gxgl_create (int, char *err, int cap) { if (err && cap > 0) { strncpy (err, "no OpenGL here", (size_t) cap - 1); err[cap - 1] = 0; } return 0; }
void gxgl_destroy (gc::GxGpu *) {}
bool gxgl_present (gc::GxGpu *, int, int, int, int, int, int) { return false; }
void gxgl_release (gc::GxGpu *) {}
void gxgl_set_scale (gc::GxGpu *, int) {}
int gxgl_read (gc::GxGpu *, unsigned *, int, int *, int *) { return 0; }
const char *gxgl_name (gc::GxGpu *) { return "none"; }
#endif
