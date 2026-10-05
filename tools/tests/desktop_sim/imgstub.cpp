//
// imgstub.cpp -- the image codecs left out of the simulator (stb's allocator would shadow the
// host's): img_load finds no image, so an ImageBox stays empty.
//
#include "imagekit/img/imgload.hpp"

bool img_load (const char *, ImgFrames *) { return false; }
bool img_is_image_name (const char *) { return false; }
