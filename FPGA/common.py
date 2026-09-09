"""Python mirrors of the packed structs in common.sv.

Bit layouts must match the SystemVerilog `typedef struct packed` field order so
the testbench packs words the same way the RTL unpacks them:

    color_t    { logic [7:0] r, g, b, a }              -> r = bits [31:24]
    vertex_t   { logic [15:0] x, y }                   -> x = bits [31:16]
    triangle_t { color_t color; vertex_t [2:0] verts } -> color is the MSBs
"""

import re

from cocotb.triggers import RisingEdge, Timer, ReadOnly
from dataclasses import dataclass
import ast
from pathlib import Path

DUMP_PATH = Path(__file__).resolve().parent.parent / "tests" / "data"
_TOKEN = re.compile(r'([^.\[\]]+)|\[(\d+)\]')

def load_dump(file):
    return ast.literal_eval((DUMP_PATH / file).read_text())

# we need this due to the strange bit sizes
def as_signed(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v



def resolve(dut, path):
    """Resolve a signal spec to a handle: 'idx[0]', 'sub.bus[2].valid', 'mem[1][3]'."""
    obj = dut
    for name, index in _TOKEN.findall(path):
        obj = getattr(obj, name) if name else obj[int(index)]
    return obj

async def monitor(dut, clk, signals, out, gate=None):
    """Append a tuple of `signals` values to `out` each rising edge.
    If `gate` is given, only append on cycles where that signal is 1.
    Signal specs may index arrays: "idx[0]", "sub.bus[2].valid"."""
    handles = [resolve(dut, s) for s in signals]
    gate_h = resolve(dut, gate) if gate is not None else None
    while True:
        await RisingEdge(clk)
        await ReadOnly()                      # let this edge's NBA updates settle
        if gate_h is None or gate_h.value == 1:
            out.append(tuple(int(h.value) for h in handles))

def mix64(x: int) -> int:
    m = (1 << 64) - 1
    x = (x + 0x9E3779B97F4A7C15) & m
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & m
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & m
    return x ^ (x >> 31)

@dataclass
class Color:                       # color_t: r=[31:24] g=[23:16] b=[15:8] a=[7:0]
    r: int
    g: int
    b: int
    a: int

    def to_word(self) -> int:
        return (self.r << 24) | (self.g << 16) | (self.b << 8) | self.a

    @classmethod
    def from_word(cls, w: int) -> "Color":
        return cls((w >> 24) & 0xFF, (w >> 16) & 0xFF, (w >> 8) & 0xFF, w & 0xFF)

    def to_rgba(self) -> bytes:
        """Byte order of `struct Color` in src/common.hpp: r, g, b, a."""
        return bytes((self.r & 0xFF, self.g & 0xFF, self.b & 0xFF, self.a & 0xFF))

    @classmethod
    def from_rgba(cls, b) -> "Color":
        return cls(b[0], b[1], b[2], b[3])

    @staticmethod
    def delta_SSE(target, best, candidate) -> int:
        b_r = target.r - best.r
        b_g = target.g - best.g
        b_b = target.b - best.b
        acc = -(b_r * b_r + b_g * b_g + b_b * b_b)

        c_r = target.r - candidate.r
        c_g = target.g - candidate.g
        c_b = target.b - candidate.b
        acc += c_r * c_r + c_g * c_g + c_b * c_b

        return acc
    @staticmethod
    def _cdiv(num: int, den: int) -> int:
        q, r = divmod(num, den)
        if r and (num < 0) != (den < 0):
            q += 1
        return q

    @staticmethod
    def rasterize(target, col) -> "Color":
        alpha = col.a
        new_r = Color._cdiv(target.r * (255 - alpha) + col.r * alpha, 255)
        new_g = Color._cdiv(target.g * (255 - alpha) + col.g * alpha, 255)
        new_b = Color._cdiv(target.b * (255 - alpha) + col.b * alpha, 255)
        new_a = target.a

        return Color(new_r, new_g, new_b, new_a)


@dataclass
class Vertex:                      # vertex_t: x=[31:16] y=[15:0]
    x: int
    y: int

    def to_word(self) -> int:
        return ((self.x & 0xFFFF) << 16) | (self.y & 0xFFFF)

    @classmethod
    def from_word(cls, w: int) -> "Vertex":
        return cls((w >> 16) & 0xFFFF, w & 0xFFFF)


@dataclass
class Triangle:                    # triangle_t: {color, verts[2:0]}, color is MSB
    color: Color
    verts: list                    # [v0, v1, v2]

    def to_int(self) -> int:
        return (self.color.to_word() << 96
                | self.verts[2].to_word() << 64
                | self.verts[1].to_word() << 32
                | self.verts[0].to_word())

    @classmethod
    def from_int(cls, v: int) -> "Triangle":
        return cls(
            Color.from_word((v >> 96) & 0xFFFFFFFF),
            [Vertex.from_word((v >> s) & 0xFFFFFFFF) for s in (0, 32, 64)],
        )

    def bounds(self) -> "PixelBounds":
        """Mirror of Triangle::bounds() in src/common.hpp."""
        xs = [v.x for v in self.verts]
        ys = [v.y for v in self.verts]
        return PixelBounds(min(xs), max(xs), min(ys), max(ys))


@dataclass
class PixelBounds:                 # mirror of PixelBounds in src/common.hpp
    x_min: int = 0
    x_max: int = 0
    y_min: int = 0
    y_max: int = 0


class ImageData:
    """Mirror of ImageData in src/common.hpp.

    Row-major RGBA8. Pixel [x, y] lives at byte (x + y * x_size) * 4, channel
    order r, g, b, a -- byte-for-byte the same layout as the C++ `Color*
    data()`, so `buf` can be handed straight to the ctypes bridge and mutated
    in place. Size is fixed at construction, matching the C++ invariant.
    """

    BYTES_PER_PIXEL = 4

    def __init__(self, x_size: int, y_size: int, fill: "Color | None" = None):
        self.x_size = int(x_size)
        self.y_size = int(y_size)
        px = (fill or Color(0, 0, 0, 255)).to_rgba()
        self.buf = bytearray(px * (self.x_size * self.y_size))

    @property
    def size(self) -> int:
        return self.x_size * self.y_size

    def _offset(self, x: int, y: int) -> int:
        if not (0 <= x < self.x_size and 0 <= y < self.y_size):
            raise IndexError(f"pixel ({x}, {y}) outside {self.x_size}x{self.y_size}")
        return (x + y * self.x_size) * self.BYTES_PER_PIXEL

    def __getitem__(self, xy) -> "Color":
        o = self._offset(*xy)
        return Color.from_rgba(self.buf[o:o + self.BYTES_PER_PIXEL])

    def __setitem__(self, xy, col: "Color") -> None:
        o = self._offset(*xy)
        self.buf[o:o + self.BYTES_PER_PIXEL] = col.to_rgba()

    def copy(self) -> "ImageData":
        img = ImageData(self.x_size, self.y_size)
        img.buf[:] = self.buf
        return img

    def to_bytes(self) -> bytes:
        return bytes(self.buf)

    @classmethod
    def from_bytes(cls, x_size: int, y_size: int, data) -> "ImageData":
        img = cls(x_size, y_size)
        if len(data) != len(img.buf):
            raise ValueError(f"{len(data)} bytes, expected {len(img.buf)}")
        img.buf[:] = data
        return img

    def __eq__(self, other) -> bool:
        return (isinstance(other, ImageData)
                and (self.x_size, self.y_size) == (other.x_size, other.y_size)
                and self.buf == other.buf)

    def __repr__(self) -> str:
        return f"ImageData({self.x_size}, {self.y_size})"
