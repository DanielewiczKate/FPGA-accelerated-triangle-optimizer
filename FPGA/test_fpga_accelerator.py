
import itertools
import logging
import random

import cocotb
from cocotb.clock import Clock
from cocotb.handle import Force
from cocotb.triggers import RisingEdge, Timer, ReadOnly
from common import Color, Vertex, Triangle, monitor, mix64
import common
from cocotbext.axi import AxiLiteBus, AxiLiteMaster, AxiStreamSource, AxiStreamBus

from dataclasses import dataclass

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
async def count_sse(dut):
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
