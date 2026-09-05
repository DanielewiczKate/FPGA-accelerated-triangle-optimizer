// The single translation unit that emits stb_image's function bodies.
// stb_image.h is declarations-only unless STB_IMAGE_IMPLEMENTATION is defined,
// and it must be defined in exactly one TU or the definitions collide at link.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
