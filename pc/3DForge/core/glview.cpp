//
// pc/3DForge/core/glview.cpp -- the view's frame drawn by the graphics card (frender.h's FORGE_HOST_SHOW): the very
// batches frender.h builds for the Pi's GPU (kapi_gpu_vertex3 / kapi_gpu_batch: a matrix or none, the depth test, the
// culling, the blending) drawn by OpenGL into a multisampled framebuffer the size of the view (8, else 4 samples: the
// smoothness the processor's path gets from drawing twice as large), resolved, read back into the host's pixels.
// An OpenGL context of its own on a window never shown, made at the first frame on the thread that draws (the
// window's). No OpenGL 3 framebuffers (an old driver, a remote desktop): false, and the processor draws as before.
// FORGE_SOFT=1: the processor always.
//
// MIT licence (Onyx).
//
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <stdio.h>
#include "forgeshim.h"

#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_MAX_SAMPLES 0x8D57
#define GL_BGRA 0x80E1
#define GL_MULTISAMPLE 0x809D

typedef void (APIENTRY *PGenFB) (GLsizei, GLuint *);
typedef void (APIENTRY *PBindFB) (GLenum, GLuint);
typedef void (APIENTRY *PRbStorageMS) (GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY *PFbRb) (GLenum, GLenum, GLenum, GLuint);
typedef GLenum (APIENTRY *PCheckFB) (GLenum);
typedef void (APIENTRY *PBlitFB) (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void (APIENTRY *PDelFB) (GLsizei, const GLuint *);

static PGenFB glGenFramebuffers_, glGenRenderbuffers_; static PBindFB glBindFramebuffer_, glBindRenderbuffer_;
static PRbStorageMS glRenderbufferStorageMultisample_; static PFbRb glFramebufferRenderbuffer_; static PCheckFB glCheckFramebufferStatus_;
static PBlitFB glBlitFramebuffer_; static PDelFB glDeleteFramebuffers_, glDeleteRenderbuffers_;

static int g_state;				// 0 not tried, 1 ready, -1 none
static HWND g_wnd; static HDC g_dc; static HGLRC g_rc;
static GLuint g_ms, g_msColour, g_msDepth, g_rs, g_rsColour; static int g_w, g_h, g_samples;

static bool gl_init ()
{
	const char *soft = getenv ("FORGE_SOFT");
	if (soft && *soft == '1') return false;
	WNDCLASSA wc = {}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA (0); wc.lpszClassName = "3DForgeGL"; wc.style = CS_OWNDC;
	RegisterClassA (&wc);
	g_wnd = CreateWindowA ("3DForgeGL", "", WS_POPUP, 0, 0, 16, 16, 0, 0, wc.hInstance, 0);
	if (!g_wnd) return false;
	g_dc = GetDC (g_wnd);
	PIXELFORMATDESCRIPTOR pfd = {}; pfd.nSize = sizeof pfd; pfd.nVersion = 1; pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
	int pf = ChoosePixelFormat (g_dc, &pfd);
	if (!pf || !SetPixelFormat (g_dc, pf, &pfd)) return false;
	g_rc = wglCreateContext (g_dc);
	if (!g_rc || !wglMakeCurrent (g_dc, g_rc)) return false;
	const char *ren = (const char *) glGetString (GL_RENDERER);
	if (!ren || strstr (ren, "GDI Generic")) return false;		// (Windows' own software OpenGL 1.1: slower than ours)
	#define GET(v, n) v = (decltype (v)) wglGetProcAddress (n); if (!v) return false
	GET (glGenFramebuffers_, "glGenFramebuffers"); GET (glBindFramebuffer_, "glBindFramebuffer"); GET (glGenRenderbuffers_, "glGenRenderbuffers");
	GET (glBindRenderbuffer_, "glBindRenderbuffer"); GET (glRenderbufferStorageMultisample_, "glRenderbufferStorageMultisample");
	GET (glFramebufferRenderbuffer_, "glFramebufferRenderbuffer"); GET (glCheckFramebufferStatus_, "glCheckFramebufferStatus");
	GET (glBlitFramebuffer_, "glBlitFramebuffer"); GET (glDeleteFramebuffers_, "glDeleteFramebuffers"); GET (glDeleteRenderbuffers_, "glDeleteRenderbuffers");
	#undef GET
	GLint most = 0; glGetIntegerv (GL_MAX_SAMPLES, &most);
	g_samples = most >= 8 ? 8 : most >= 4 ? 4 : most;
	if (getenv ("FORGE_SAMPLES")) g_samples = atoi (getenv ("FORGE_SAMPLES"));
	if (getenv ("FORGE_PROFILE")) fprintf (stderr, "%s, %d samples\n", ren, g_samples);
	return true;
}
// The framebuffers for a view of w x h: multisampled (drawn), single (resolved, read).
static bool gl_size (int w, int h)
{
	if (w == g_w && h == g_h && g_ms) return true;
	if (g_ms) { glDeleteFramebuffers_ (1, &g_ms); glDeleteFramebuffers_ (1, &g_rs); GLuint rb[3] = { g_msColour, g_msDepth, g_rsColour }; glDeleteRenderbuffers_ (3, rb); g_ms = 0; }
	glGenFramebuffers_ (1, &g_ms); glGenFramebuffers_ (1, &g_rs);
	glGenRenderbuffers_ (1, &g_msColour); glGenRenderbuffers_ (1, &g_msDepth); glGenRenderbuffers_ (1, &g_rsColour);
	glBindRenderbuffer_ (GL_RENDERBUFFER, g_msColour); glRenderbufferStorageMultisample_ (GL_RENDERBUFFER, g_samples, GL_RGBA8, w, h);
	glBindRenderbuffer_ (GL_RENDERBUFFER, g_msDepth); glRenderbufferStorageMultisample_ (GL_RENDERBUFFER, g_samples, GL_DEPTH_COMPONENT24, w, h);
	glBindRenderbuffer_ (GL_RENDERBUFFER, g_rsColour); glRenderbufferStorageMultisample_ (GL_RENDERBUFFER, 0, GL_RGBA8, w, h);
	glBindFramebuffer_ (GL_FRAMEBUFFER, g_ms);
	glFramebufferRenderbuffer_ (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_msColour);
	glFramebufferRenderbuffer_ (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_msDepth);
	if (glCheckFramebufferStatus_ (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
	glBindFramebuffer_ (GL_FRAMEBUFFER, g_rs);
	glFramebufferRenderbuffer_ (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_rsColour);
	if (glCheckFramebufferStatus_ (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
	g_w = w; g_h = h; return true;
}

extern "C" __declspec (dllexport) int fc_gl () { return g_state; }		// (the status bar: 1 the graphics card draws)

bool forge_gl_show (const kapi_gpu_vertex3 *v, unsigned nv, const kapi_gpu_batch *b, unsigned nb, int w, int h, unsigned clear, unsigned *dst, int stride)
{
	if (g_state == 0) g_state = gl_init () ? 1 : -1;
	if (g_state < 0 || w < 1 || h < 1) return false;
	if (wglGetCurrentContext () != g_rc) wglMakeCurrent (g_dc, g_rc);
	if (!gl_size (w, h)) { g_state = -1; return false; }
	glBindFramebuffer_ (GL_FRAMEBUFFER, g_ms);
	glViewport (0, 0, w, h);
	glEnable (GL_MULTISAMPLE);
	glClearColor (((clear >> 16) & 255) / 255.0f, ((clear >> 8) & 255) / 255.0f, (clear & 255) / 255.0f, 1);
	glClearDepth (1); glDepthMask (GL_TRUE);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable (GL_LIGHTING); glDisable (GL_TEXTURE_2D); glShadeModel (GL_SMOOTH);
	// (the picture is read from the top row down: y turned over, so the faces' turn is too)
	glMatrixMode (GL_PROJECTION); glLoadIdentity (); glScalef (1, -1, 1);
	glFrontFace (GL_CW); glCullFace (GL_BACK);
	glEnableClientState (GL_VERTEX_ARRAY); glEnableClientState (GL_COLOR_ARRAY);
	glVertexPointer (4, GL_FLOAT, sizeof (kapi_gpu_vertex3), &v[0].x);
	glColorPointer (4, GL_UNSIGNED_BYTE, sizeof (kapi_gpu_vertex3), &v[0].r);
	static const GLenum ZF[8] = { GL_LESS, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
	glMatrixMode (GL_MODELVIEW);
	for (unsigned i = 0; i < nb; i++)
	{
		const kapi_gpu_batch &bt = b[i];
		if (!bt.count || bt.first + bt.count > nv) continue;
		glEnable (GL_DEPTH_TEST); glDepthFunc (ZF[bt.flags & 7]);
		glDepthMask (bt.flags & KAPI_GPU_B_NOZWRITE ? GL_FALSE : GL_TRUE);
		if (bt.flags & KAPI_GPU_B_CULL_BACK) glEnable (GL_CULL_FACE); else glDisable (GL_CULL_FACE);
		if (((bt.flags >> 8) & 15) == KAPI_GPU_BLEND_ALPHA) { glEnable (GL_BLEND); glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); } else glDisable (GL_BLEND);
		if (bt.flags & KAPI_GPU_B_NOMATRIX) glLoadIdentity ();
		else { const float *m = bt.matrix; GLfloat t[16] = { m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13], m[2], m[6], m[10], m[14], m[3], m[7], m[11], m[15] }; glLoadMatrixf (t); }
		glDrawArrays (GL_TRIANGLES, (GLint) bt.first, (GLsizei) (bt.count / 3 * 3));
	}
	glDisableClientState (GL_VERTEX_ARRAY); glDisableClientState (GL_COLOR_ARRAY);
	// resolved, then read into the host's pixels (0x00RRGGBB: the bytes B G R A)
	glBindFramebuffer_ (GL_READ_FRAMEBUFFER, g_ms); glBindFramebuffer_ (GL_DRAW_FRAMEBUFFER, g_rs);
	glBlitFramebuffer_ (0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer_ (GL_READ_FRAMEBUFFER, g_rs);
	glPixelStorei (GL_PACK_ALIGNMENT, 4); glPixelStorei (GL_PACK_ROW_LENGTH, stride);
	glReadPixels (0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, dst);
	glPixelStorei (GL_PACK_ROW_LENGTH, 0);
	if (glGetError () != GL_NO_ERROR) { g_state = -1; return false; }
	return true;
}
