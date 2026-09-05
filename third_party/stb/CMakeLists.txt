# stb_image is header-only: the header carries the definitions, gated behind
# STB_IMAGE_IMPLEMENTATION. stb_image_impl.cpp is the one TU that defines it,
# compiled once here so no source in src/ has to care.
add_library(stb STATIC stb_image_impl.cpp)

# SYSTEM keeps stb's own warnings out of our -Wall -Wextra output.
target_include_directories(stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
