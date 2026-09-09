"""ctypes bridge to the C++ reference implementation in src/.

Loads ``libtriopt_c`` (the ``extern "C"`` shim in ``bindings/triopt_c.cpp``)
and exposes the golden rasterizer and SSE functions, so the RTL testbenches can
differentially test against the real C++ instead of a Python re-implementation.

Build the shared lib first::

    cmake -S . -B build
    cmake --build build --target triopt_c

Set ``TRIOPT_C_LIB`` to point at the .so explicitly if it lands somewhere odd.
"""

import ctypes
import os
from ctypes import POINTER, c_uint8, c_uint16, c_uint64
from pathlib import Path

from common import ImageData, PixelBounds, Triangle, as_signed

_REPO_ROOT = Path(__file__).resolve().parent.parent
_LIB_NAMES = ("libtriopt_c.so", "libtriopt_c.dylib", "triopt_c.dll")
_SEARCH_DIRS = (
    _REPO_ROOT / "build",
    _REPO_ROOT / "build" / "lib",
    _REPO_ROOT / "build" / "Debug",
    _REPO_ROOT / "build" / "Release",
    _REPO_ROOT,
)


def _find_lib() -> Path:
    override = os.environ.get("TRIOPT_C_LIB")
    if override:
        p = Path(override)
        if not p.exists():
            raise FileNotFoundError(f"TRIOPT_C_LIB={override} does not exist")
        return p
    for d in _SEARCH_DIRS:
        for name in _LIB_NAMES:
            if (d / name).exists():
                return d / name
    raise FileNotFoundError(
        "libtriopt_c not found. Build it with:\n"
        "    cmake -S . -B build && cmake --build build --target triopt_c\n"
        f"searched: {[str(d) for d in _SEARCH_DIRS]}"
    )


_lib = ctypes.CDLL(str(_find_lib()))

_U8 = POINTER(c_uint8)
_U16 = POINTER(c_uint16)
_RASTER_ARGS = [_U8, c_uint16, c_uint16, _U16, _U16,
                c_uint8, c_uint8, c_uint8, c_uint8]

_lib.triopt_rasterize_v1.restype = None
_lib.triopt_rasterize_v1.argtypes = _RASTER_ARGS
_lib.triopt_rasterize_v2.restype = None
_lib.triopt_rasterize_v2.argtypes = _RASTER_ARGS

_lib.triopt_sse.restype = c_uint64
_lib.triopt_sse.argtypes = [_U8, _U8, c_uint16, c_uint16]

_lib.triopt_delta_sse.restype = c_uint64
_lib.triopt_delta_sse.argtypes = [_U8, _U8, _U8, c_uint16, c_uint16,
                                  c_uint16, c_uint16, c_uint16, c_uint16]


def _as_u8(image: ImageData):
    """ctypes view over the image's bytearray -- no copy, writes land in place."""
    return (c_uint8 * len(image.buf)).from_buffer(image.buf)


def _verts(triangle: Triangle):
    vx = (c_uint16 * 3)(*(v.x & 0xFFFF for v in triangle.verts))
    vy = (c_uint16 * 3)(*(v.y & 0xFFFF for v in triangle.verts))
    return vx, vy


def _check_same_size(*images: ImageData) -> None:
    sizes = {(im.x_size, im.y_size) for im in images}
    if len(sizes) != 1:
        raise ValueError(f"image size mismatch: {sizes}")


def _rasterize(fn, image: ImageData, triangle: Triangle) -> ImageData:
    vx, vy = _verts(triangle)
    c = triangle.color
    fn(_as_u8(image), image.x_size, image.y_size, vx, vy,
       c.r & 0xFF, c.g & 0xFF, c.b & 0xFF, c.a & 0xFF)
    return image


def rasterize_v1(image: ImageData, triangle: Triangle) -> ImageData:
    """RasterizeTriangle (golden). Mutates and returns ``image``."""
    return _rasterize(_lib.triopt_rasterize_v1, image, triangle)


def rasterize_v2(image: ImageData, triangle: Triangle) -> ImageData:
    """RasterizeTriangleV2 (incremental edge functions). Mutates and returns ``image``."""
    return _rasterize(_lib.triopt_rasterize_v2, image, triangle)


def compute_sse(target: ImageData, candidate: ImageData) -> int:
    """compute_SSE(target, candidate)."""
    _check_same_size(target, candidate)
    return int(_lib.triopt_sse(_as_u8(target), _as_u8(candidate),
                               target.x_size, target.y_size))


def delta_sse(target: ImageData, prev_best: ImageData, candidate: ImageData,
              bounds: PixelBounds) -> int:
    """compute_delta_SSE(target, prev_best, candidate, bounds), as a signed int."""
    _check_same_size(target, prev_best, candidate)
    raw = _lib.triopt_delta_sse(
        _as_u8(target), _as_u8(prev_best), _as_u8(candidate),
        target.x_size, target.y_size,
        bounds.x_min, bounds.x_max, bounds.y_min, bounds.y_max)
    return as_signed(int(raw), 64)
