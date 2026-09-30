//
// v3d.h -- the V3D GPU (sys/v3d.cpp): kapi v52 gpu_info / gpu_draw, v53 gpu_texture /
// gpu_render, v70 gpu_texture_rect.
//
#ifndef _kern_v3d_h
#define _kern_v3d_h

struct kapi_gpu_vertex;
struct kapi_gpu_vertex3;
struct kapi_gpu_batch;
struct kapi_gpu_frame;
class CAddressSpace;

extern "C" {
int kapi_gpu_info (char *pBuf, unsigned nCap);
int kapi_gpu_draw (const struct kapi_gpu_vertex *pV, unsigned n, unsigned nClear, unsigned *pDst, int w, int h, int nStride);
int kapi_gpu_texture (int nHandle, const unsigned *pPixels, int w, int h, int nStride);
int kapi_gpu_render (const struct kapi_gpu_frame *pF, const struct kapi_gpu_vertex3 *pV, unsigned nV,
		     const struct kapi_gpu_batch *pB, unsigned nB);
int kapi_gpu_texture_rect (int nHandle, int x, int y, int w, int h, const unsigned *pPixels, int nStride);	// (v70)
}

// The textures of a program that ends (mm/addrspace.cpp).
void V3DReleaseAS (CAddressSpace *pAS);

#endif
