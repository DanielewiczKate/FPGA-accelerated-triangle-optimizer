// The single translation unit that emits stb_image_write's function bodies.
// stb_image_write.h is declarations-only unless STB_IMAGE_WRITE_IMPLEMENTATION
// is defined, and it must be defined in exactly one TU or the definitions
// collide at link. Kept separate from stb_image_impl.cpp so each header's
// implementation is its own object file.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
