#!/usr/bin/env python
"""
Based on works by: Dan Gisselquist
See https://github.com/ZipCPU/wb2axip/blob/master/rtl/easyaxil.v
"""

import itertools
import logging
import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer, ReadOnly

from cocotbext.axi import AxiLiteBus, AxiLiteMaster, AxiStreamSource, AxiStreamBus

from dataclasses import dataclass

RW_REGS = {
    "MAXC":    0x08,
    "TRI_V0":  0x0C,
    "TRI_V1":  0x10,
    "TRI_V2":  0x14,
    "TRI_COL": 0x18,
}
RO_REGS = {           # mirrors AXILiteWorker.sv register map; read-only, writes ignored
    "STATUS": 0x04,   # [0] = dut.busy, [1] = done_flag (sticky latch of dut.done)
    "SSE_LO": 0x1C,   # dut.delta_sse[31:0]
    "SSE_HI": 0x20,   # dut.delta_sse[63:32]
}



@dataclass
class Color:                       # color_t: r=[31:24] g=[23:16] b=[15:8] a=[7:0]
    r: int; g: int; b: int; a: int

    def to_word(self) -> int:
        return (self.r << 24) | (self.g << 16) | (self.b << 8) | self.a

    @classmethod
    def from_word(cls, w: int):
        return cls((w >> 24) & 0xFF, (w >> 16) & 0xFF, (w >> 8) & 0xFF, w & 0xFF)


@dataclass
class Vertex:                      # vertex_t: x=[31:16] y=[15:0]
    x: int; y: int

    def to_word(self) -> int:
        return ((self.x & 0xFFFF) << 16) | (self.y & 0xFFFF)

    @classmethod
    def from_word(cls, w: int):
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
    def from_int(cls, v: int):
        return cls(
            Color.from_word((v >> 96) & 0xFFFFFFFF),
            [Vertex.from_word((v >> s) & 0xFFFFFFFF) for s in (0, 32, 64)],
        )


class TB(object):
    def __init__(self, dut):
        self.dut = dut

        self.log = logging.getLogger("cocotb.tb")
        self.log.setLevel(logging.DEBUG)

        cocotb.start_soon(Clock(dut.s_axi_aclk, 10, units="ns").start())

        self.axil_master = AxiLiteMaster(
            AxiLiteBus.from_prefix(dut, "s_axi"),
            dut.s_axi_aclk, dut.s_axi_aresetn,
            reset_active_level=False,
        )

        self.axil_stream = AxiStreamSource(
            AxiStreamBus.from_prefix(dut, "s_axi"),
            dut.s_axi_aclk, dut.s_axi_aresetn,
            reset_active_level=False,
        )

        dut.render_ready.value = 0 # this must not be floating or the stream fails

    def set_idle_generator(self, generator=None):
        if generator:
            self.axil_master.write_if.aw_channel.set_pause_generator(generator())
            self.axil_master.write_if.w_channel.set_pause_generator(generator())
            self.axil_master.read_if.ar_channel.set_pause_generator(generator())

    def set_backpressure_generator(self, generator=None):
        if generator:
            self.axil_master.write_if.b_channel.set_pause_generator(generator())
            self.axil_master.read_if.r_channel.set_pause_generator(generator())

    async def cycle_reset(self):
        # s_axi_aresetn is active-low: 1 = released, 0 = asserted
        self.dut.s_axi_aresetn.value = 1
        await RisingEdge(self.dut.s_axi_aclk)
        await RisingEdge(self.dut.s_axi_aclk)
        self.dut.s_axi_aresetn.value = 0
        await RisingEdge(self.dut.s_axi_aclk)
        await RisingEdge(self.dut.s_axi_aclk)
        self.dut.s_axi_aresetn.value = 1
        await RisingEdge(self.dut.s_axi_aclk)
        await RisingEdge(self.dut.s_axi_aclk)

def mix64(x: int) -> int:
    m = (1 << 64) - 1
    x = (x + 0x9E3779B97F4A7C15) & m
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & m
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & m
    return x ^ (x >> 31)

async def monitor(dut, clk, signals, out, gate=None):
    """Append a tuple of `signals` values to `out` each rising edge.
    If `gate` is given, only append on cycles where that signal is 1."""
    while True:
        await RisingEdge(clk)
        await ReadOnly()                      # let this edge's NBA updates settle
        if gate is None or getattr(dut, gate).value == 1:
            out.append(tuple(int(getattr(dut, s).value) for s in signals))

def cycle_pause():
    return itertools.cycle([1, 1, 1, 0])


def with_stalls(fn):
    """Run the test 4x: no stalls, idle only, backpressure only, both."""
    return cocotb.test()(
        cocotb.parametrize(
            ("idle_inserter", [None, cycle_pause]),
            ("backpressure_inserter", [None, cycle_pause]),
        )(fn)
    )


@with_stalls
async def write_read_all_regs(dut, idle_inserter, backpressure_inserter):

    tb = TB(dut)
    await tb.cycle_reset()
    tb.set_idle_generator(idle_inserter)
    tb.set_backpressure_generator(backpressure_inserter)

    def pattern(i, addr):
        return 0xA5000000 | (i << 16) | addr

    for i, addr in enumerate(RW_REGS.values()):
        await tb.axil_master.write(addr, pattern(i, addr).to_bytes(4, "little"))

    for i, (name, addr) in enumerate(RW_REGS.items()):
        got = int.from_bytes((await tb.axil_master.read(addr, 4)).data, "little")
        want = pattern(i, addr)
        assert got == want, f"{name}@{addr:#04x}: got {got:#010x} want {want:#010x}"

@with_stalls
async def reset_regs(dut, idle_inserter, backpressure_inserter):

    tb = TB(dut)
    await tb.cycle_reset()
    tb.set_idle_generator(idle_inserter)
    tb.set_backpressure_generator(backpressure_inserter)

    def pattern(i, addr):
        return 0xA5000000 | (i << 16) | addr

    for i, addr in enumerate(RW_REGS.values()):
        await tb.axil_master.write(addr, pattern(i, addr).to_bytes(4, "little"))

    await tb.cycle_reset()

    for i, (name, addr) in enumerate(RW_REGS.items()):
        got = int.from_bytes((await tb.axil_master.read(addr, 4)).data, "little")
        want = 0
        assert got == want, f"{name}@{addr:#04x}: got {got:#010x} want {want:#010x}"
@cocotb.test(skip=True)
async def partial_writes(dut):
    """WSTRB sub-word writes: driver issues full-word writes only, not exercised."""

@with_stalls
async def write_delta_sse_and_read(dut, idle_inserter, backpressure_inserter):
    tb = TB(dut)
    await tb.cycle_reset()
    tb.set_idle_generator(idle_inserter)
    tb.set_backpressure_generator(backpressure_inserter)
    test_value = 0xDEADBEEF_CAFEBABE
    dut.delta_sse.value = test_value

    SSE_LO = int.from_bytes((await tb.axil_master.read(RO_REGS["SSE_LO"], 4)).data, "little")
    SSE_HI = int.from_bytes((await tb.axil_master.read(RO_REGS["SSE_HI"], 4)).data, "little")

    got = SSE_HI << 32 | SSE_LO
    assert got == test_value, f"delta_sse {test_value:#018x} -> read {got:#018x}"


@with_stalls
async def passthough_packing(dut, idle_inserter, backpressure_inserter):
    tb = TB(dut)
    await tb.cycle_reset()
    tb.set_idle_generator(idle_inserter)
    tb.set_backpressure_generator(backpressure_inserter)

    expected_maxc = Vertex(123, 42)

    expected_tri = Triangle(
            Color(14, 124, 23, 43),
            [Vertex(32, 21), Vertex(12, 23), Vertex(12, 32)])

    await tb.axil_master.write(RW_REGS["TRI_V0"],
                               expected_tri.verts[0].to_word().to_bytes(4, "little"))
    await tb.axil_master.write(RW_REGS["TRI_V1"],
                               expected_tri.verts[1].to_word().to_bytes(4, "little"))
    await tb.axil_master.write(RW_REGS["TRI_V2"],
                               expected_tri.verts[2].to_word().to_bytes(4, "little"))
    await tb.axil_master.write(RW_REGS["TRI_COL"],
                               expected_tri.color.to_word().to_bytes(4, "little"))
    await tb.axil_master.write(RW_REGS["MAXC"],
                               expected_maxc.to_word().to_bytes(4, "little"))
    got = Triangle.from_int(int(dut.triangle.value))
    assert got == expected_tri

@with_stalls
async def ctrl_start_pulse(dut, idle_inserter, backpressure_inserter):
    tb = TB(dut)
    await tb.cycle_reset()
    tb.set_idle_generator(idle_inserter)
    tb.set_backpressure_generator(backpressure_inserter)

    highs = []
    async def mon():
        while True:
            await RisingEdge(dut.s_axi_aclk)
            highs.append(int(dut.start.value))

    t = cocotb.start_soon(mon())
    await tb.axil_master.write(0x00, (1).to_bytes(4, "little"))
    await RisingEdge(dut.s_axi_aclk)
    t.cancel()

    assert sum(highs) == 1, f"start high for {sum(highs)} cycles, want 1"
    assert int.from_bytes((await tb.axil_master.read(0x00, 4)).data, "little") == 0

@cocotb.test()
async def pixel_stream_data(dut):
    tb = TB(dut)
    await tb.cycle_reset()

    beats = []
    mon = cocotb.start_soon(
        monitor(dut, dut.s_axi_aclk,
        ["t_col", "b_col"],
        beats, gate="pixel_valid")
    )

    dut.render_ready.value = 1
    for i in range(10):
        await tb.axil_stream.send(mix64(i).to_bytes(8, "little"))

    await tb.axil_stream.wait()            # block until all frames transmitted
    await RisingEdge(dut.s_axi_aclk)       # pixel_valid for the last beat registers
    await RisingEdge(dut.s_axi_aclk)       # monitor samples it

    mon.cancel()
    for i in range(10):
        assert (beats[i][0] << 32 | beats[i][1]) == mix64(i)


@cocotb.test()
async def pixel_stream_data_backpressure(dut):
    tb = TB(dut)
    await tb.cycle_reset()

    beats = []
    mon = cocotb.start_soon(
        monitor(dut, dut.s_axi_aclk,
        ["t_col", "b_col"],
        beats, gate="pixel_valid")
    )

    dut.render_ready.value = 1
    for i in range(0, 10):
        await tb.axil_stream.send(mix64(i).to_bytes(8, "little"))

    await Timer(2, "ns")
    dut.render_ready.value = 0
    await Timer(2, "ns")
    dut.render_ready.value = 1

    await tb.axil_stream.wait()            # block until all frames transmitted
    await RisingEdge(dut.s_axi_aclk)       # pixel_valid for the last beat registers
    await RisingEdge(dut.s_axi_aclk)       # monitor samples it

    mon.cancel()
    for i in range(10):
        assert (beats[i][0] << 32 | beats[i][1]) == mix64(i)



