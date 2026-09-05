# stb_image / stb_image_write are header-only: each header carries its
# definitions gated behind a *_IMPLEMENTATION macro. stb_image_impl.cpp and
# stb_image_write_impl.cpp are the one-TU-each that emit them, compiled here so
# no source in src/ has to define the macro itself.
add_library(stb STATIC
    stb_image_impl.cpp
    stb_image_write_impl.cpp
)

# SYSTEM keeps stb's own warnings out of our -Wall -Wextra output.
target_include_directories(stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
