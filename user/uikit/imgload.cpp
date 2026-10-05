//
// uikit/imgload.cpp -- the image codecs (img/imgload.hpp: stb_image, simplewebp, PCX),
// compiled once into libuikit.a with FP/SIMD (see ../Makefile). Pulled into an app only
// if it calls img_load / img_free / img_is_image_name (or uses a uikit::ImageBox).
//
#define IMGLOAD_IMPLEMENTATION
#include "img/imgload.hpp"
