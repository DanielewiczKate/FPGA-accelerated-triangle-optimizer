
import itertools
import logging
import random

import cocotb
from cocotb.clock import Clock
from cocotb.handle import Force
from cocotb.triggers import RisingEdge, Timer, ReadOnly
from common import Color, Vertex, Triangle, monitor, ImageData
import common
from cocotbext.axi import AxiLiteBus, AxiLiteMaster, AxiStreamSource, AxiStreamBus
import triopt_ref as ref
from dataclasses import dataclass
import random
# --- Register map (mirrors AXILiteWorker.sv) --------------------------------
# RW registers
A_CTRL    = 0x00
A_MAXC    = 0x08
A_TRI_V0  = 0x0C
A_TRI_V1  = 0x10
A_TRI_V2  = 0x14
A_TRI_COL = 0x18

# RO registers (writes ignored)
A_STATUS  = 0x04   # [0] = dut.busy, [1] = done_flag (sticky latch of dut.done)
A_SSE_LO  = 0x1C   # dut.delta_sse[31:0]
A_SSE_HI  = 0x20   # dut.delta_sse[63:32]

class TB(object):
    def __init__(self, dut):
        self.dut = dut

        # Drive reset asserted before the AXI components' _run loops start
        # sampling at t=0. Otherwise s_axi_aresetn is X, cocotbext-axi reads
        # that as "reset de-asserted", and the source samples s_axi_tready
        # (= render_ready, an undriven flop) as X -> exception.
        dut.s_axi_aresetn.setimmediatevalue(0)

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

    async def cycle_reset(self):
        # s_axi_aresetn is active-low: 1 = released, 0 = asserted.
        # Assert first, clock the datapath into its reset state, then release.
        self.dut.s_axi_aresetn.value = 0
        await RisingEdge(self.dut.s_axi_aclk)
        await RisingEdge(self.dut.s_axi_aclk)
        self.dut.s_axi_aresetn.value = 1
        await RisingEdge(self.dut.s_axi_aclk)
        await RisingEdge(self.dut.s_axi_aclk)

    # Setup the accerators AXI lite ports.
    # This does not handle the stream
    async def init_accelerator(self, max_coord, triangle):
        await self.axil_master.write(A_TRI_V0, triangle.verts[0].to_word().to_bytes(4, "little"))
        await self.axil_master.write(A_TRI_V1, triangle.verts[1].to_word().to_bytes(4, "little"))
        await self.axil_master.write(A_TRI_V2, triangle.verts[2].to_word().to_bytes(4, "little"))
        await self.axil_master.write(A_TRI_COL, triangle.color.to_word().to_bytes(4, "little"))
        await self.axil_master.write(A_MAXC, max_coord.to_word().to_bytes(4, "little"))

        for _ in range(20):
            await RisingEdge(self.dut.s_axi_aclk)

@cocotb.test()
async def count_sse_pos(dut):
    N = 5
    tri = Triangle(
            Color(1, 0, 0, 255),
            [Vertex(0, 0), Vertex(N-1, 0), Vertex(N-1, N-1)])
    max_coord = Vertex(N, N)

    tb = TB(dut)
    await tb.cycle_reset()
    await tb.init_accelerator(max_coord, tri)
    await RisingEdge(tb.dut.s_axi_aclk)

    await tb.axil_master.write(A_CTRL, 0x2.to_bytes(4, "little"))

    # Open up streams, stream in dummy data. By setting both pixels to 0 and
    # the tri col to (1,0,0,255) we can assert correctness by checking
    # delta_sse = N*(N+1)/2
    for i in range(N*N):
        await tb.axil_stream.send(0x0.to_bytes(8, "little"))

    # Why does this take N*N + 3?
    for i in range(N*N + 3):
        await RisingEdge(tb.dut.s_axi_aclk)

    expected = N*(N+1)/2
    assert expected == common.as_signed(int(dut.rasterizer.t_sse_acc.value), 64)

@cocotb.test()
async def count_sse_neg(dut):
    N = 5
    tri = Triangle(
            Color(255, 255, 255, 255),
            [Vertex(0, 0), Vertex(N-1, 0), Vertex(N-1, N-1)])
    max_coord = Vertex(N, N)

    tb = TB(dut)
    await tb.cycle_reset()
    await tb.init_accelerator(max_coord, tri)
    await RisingEdge(tb.dut.s_axi_aclk)

    await tb.axil_master.write(A_CTRL, 0x2.to_bytes(4, "little"))

    # Open up streams, stream in dummy data. By setting both pixels to 0 and
    # the tri col to (1,0,0,255) we can assert correctness by checking
    # delta_sse = N*(N+1)/2
    for i in range(N*N):
        await tb.axil_stream.send(0xFFFFFFFF_FEFFFFFF.to_bytes(8, "little"))

    # Why does this take N*N + 3?
    for i in range(N*N + 3):
        await RisingEdge(tb.dut.s_axi_aclk)

    expected = -N*(N+1)/2
    assert expected == common.as_signed(int(dut.rasterizer.t_sse_acc.value), 64)
@cocotb.test()
async def large_sse(dut):
    N = 5 # this works for large N as well, but testing takes a long time
    tri = Triangle(
            Color(255, 255, 255, 255),
            [Vertex(0, 0), Vertex(N-1, 0), Vertex(N-1, N-1)])
    max_coord = Vertex(N, N)

    tb = TB(dut)
    await tb.cycle_reset()
    await tb.init_accelerator(max_coord, tri)
    await RisingEdge(tb.dut.s_axi_aclk)

    await tb.axil_master.write(A_CTRL, 0x2.to_bytes(4, "little"))

    # Open up streams, stream in dummy data. By setting both pixels to 0 and
    # the tri col to (1,0,0,255) we can assert correctness by checking
    # delta_sse = N*(N+1)/2
    for i in range(N*N):
        await tb.axil_stream.send(0x0.to_bytes(8, "little"))

    # Why does this take N*N + 3?
    for i in range(N*N + 3):
        await RisingEdge(tb.dut.s_axi_aclk)

    expected = N*(N+1)/2 * 255 * 255 * 3
    assert expected == common.as_signed(int(dut.rasterizer.t_sse_acc.value), 64)

@cocotb.test()
async def CPP_bindings(dut):
    # Same test as above to confirm that the CPP bindings work correctly
    N = 5

    img = ImageData(N, N)
    target = img.copy()
    prev_best = img.copy()

    tri = Triangle(Color(1, 0, 0, 255), [Vertex(0, 0), Vertex(N-1, 0), Vertex(N-1, N-1)])
    ref.rasterize_v2(img, tri)                          # mutates img in place

    d = ref.delta_sse(target, prev_best, img, tri.bounds())

    max_coord = Vertex(N, N)

    tb = TB(dut)
    await tb.cycle_reset()
    await tb.init_accelerator(max_coord, tri)
    await RisingEdge(tb.dut.s_axi_aclk)

    await tb.axil_master.write(A_CTRL, 0x2.to_bytes(4, "little"))

    # Open up streams, stream in dummy data. By setting both pixels to 0 and
    # the tri col to (1,0,0,255) we can assert correctness by checking
    # delta_sse = N*(N+1)/2
    for i in range(N*N):
        await tb.axil_stream.send(0x0.to_bytes(8, "little"))

    # Why does this take N*N + 3?
    for i in range(N*N + 3):
        await RisingEdge(tb.dut.s_axi_aclk)

    assert d == common.as_signed(int(dut.rasterizer.t_sse_acc.value), 64)




@cocotb.test()
async def CPP_bindings_random_colors(dut):
    # Same corner triangle shape as the reference CPP_bindings() test
    # (Vertex(0,0), Vertex(N-1,0), Vertex(N-1,N-1)), but with random image
    # data instead of an all-zero canvas.
    SEED = 0xC0FFEE   # fixed seed -> deterministic across runs
    N = 5              # image is N x N
    T = 10              # number of random trials

    rng = random.Random(SEED)
    tb = TB(dut)

    for trial in range(T):
        await tb.cycle_reset()

        target = ImageData(N, N)
        _fill_random(target, N, rng)

        prev_best = ImageData(N, N)
        _fill_random(prev_best, N, rng)

        tri = Triangle(
            _rand_color(rng),
            [Vertex(0, 0), Vertex(N - 1, 0), Vertex(N - 1, N - 1)],
        )

        img = prev_best.copy()
        ref.rasterize_v2(img, tri)   # mutates img in place -> candidate

        d = ref.delta_sse(target, prev_best, img, tri.bounds())

        max_coord = _max_coord_from_bounds(tri)
        await tb.init_accelerator(max_coord, tri)
        await RisingEdge(tb.dut.s_axi_aclk)

        await tb.axil_master.write(A_CTRL, 0x2.to_bytes(4, "little"))

        # DUT's coordinate counter is max_coord.x wide, not N wide -- beat i
        n_beats = max_coord.x * max_coord.y
        for i in range(n_beats):
            x, y = i % max_coord.x, i // max_coord.x
            await tb.axil_stream.send(_pack_pixel(target, prev_best, x, y))

        for i in range(n_beats + 3):
            await RisingEdge(tb.dut.s_axi_aclk)

        actual = common.as_signed(int(dut.rasterizer.t_sse_acc.value), 64)
        assert d == actual, (
            f"trial {trial}: expected {d}, got {actual} "
            f"(tri verts={[(v.x, v.y) for v in tri.verts]}, "
            f"tri color={tri.color})"
        )


@cocotb.test()
async def CPP_bindings_random_triangle(dut):
    SEED = 0xC0FFEE
    N = 5
    T = 10

    rng = random.Random(SEED)
    tb = TB(dut)

    for trial in range(T):
        await tb.cycle_reset()

        target = ImageData(N, N)
        _fill_random(target, N, rng)

        prev_best = ImageData(N, N)
        _fill_random(prev_best, N, rng)

        tri = _rand_triangle(rng, N)

        img = prev_best.copy()
        ref.rasterize_v2(img, tri)

        d = ref.delta_sse(target, prev_best, img, tri.bounds())

        max_coord = _max_coord_from_bounds(tri)
        await tb.init_accelerator(max_coord, tri)
        await RisingEdge(tb.dut.s_axi_aclk)

        await tb.axil_master.write(A_CTRL, 0x2.to_bytes(4, "little"))

        n_beats = max_coord.x * max_coord.y
        for i in range(n_beats):
            x, y = i % max_coord.x, i // max_coord.x
            await tb.axil_stream.send(_pack_pixel(target, prev_best, x, y))

        for i in range(n_beats + 3):
            await RisingEdge(tb.dut.s_axi_aclk)

        actual = common.as_signed(int(dut.rasterizer.t_sse_acc.value), 64)
        assert d == actual, (
            f"trial {trial}: expected {d}, got {actual} "
            f"(tri verts={[(v.x, v.y) for v in tri.verts]}, "
            f"tri color={tri.color})"
        )




def _rand_color(rng: random.Random) -> "Color":
    return Color(
        rng.randrange(256), rng.randrange(256),
        rng.randrange(256), rng.randrange(256),
    )


def _rand_triangle(rng: random.Random, N: int) -> "Triangle":
    v0 = Vertex(0, 0)
    while True:
        v1 = Vertex(rng.randrange(N), rng.randrange(N))
        v2 = Vertex(rng.randrange(N), rng.randrange(N))
        area2 = v1.x * v2.y - v1.y * v2.x
        if area2 != 0:
            return Triangle(_rand_color(rng), [v0, v1, v2])


def _max_coord_from_bounds(tri: "Triangle") -> "Vertex":
    b = tri.bounds()
    try:
        x_max, y_max = b.x_max, b.y_max
    except AttributeError:
        _, x_max, _, y_max = b
    return Vertex(x_max + 1, y_max + 1)


def _fill_random(img: "ImageData", N: int, rng: random.Random) -> None:
    for y in range(N):
        for x in range(N):
            img[x, y] = _rand_color(rng)


def _pack_pixel(target: "ImageData", prev_best: "ImageData", x: int, y: int) -> bytes:
      return (
        prev_best[x, y].to_word().to_bytes(4, "little")
        + target[x, y].to_word().to_bytes(4, "little")
    )

