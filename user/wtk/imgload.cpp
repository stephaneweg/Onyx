//
// wtk/imgload.cpp -- the image codecs (img/imgload.hpp: stb_image, simplewebp, PCX),
// compiled once into libwtk.a with FP/SIMD (see ../Makefile). Pulled into an app only
// if it calls img_load / img_free / img_is_image_name (or uses a wtk::ImageBox).
//
#define IMGLOAD_IMPLEMENTATION
#include "img/imgload.hpp"
