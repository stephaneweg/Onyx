//
// v3d.h -- the V3D GPU (sys/v3d.cpp): kapi v52 gpu_info / gpu_draw.
//
#ifndef _kern_v3d_h
#define _kern_v3d_h

struct kapi_gpu_vertex;

extern "C" {
int kapi_gpu_info (char *pBuf, unsigned nCap);
int kapi_gpu_draw (const struct kapi_gpu_vertex *pV, unsigned n, unsigned nClear, unsigned *pDst, int w, int h, int nStride);
}

#endif
