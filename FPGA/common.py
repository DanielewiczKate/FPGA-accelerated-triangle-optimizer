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
