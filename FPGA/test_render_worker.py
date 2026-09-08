import itertools
import logging
import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer, ReadOnly
from common import Color, Vertex, Triangle, monitor, mix64
import common
from cocotbext.axi import AxiLiteBus, AxiLiteMaster, AxiStreamSource, AxiStreamBus

from dataclasses import dataclass


class TB(object):
    def __init__(self, dut):
        self.dut = dut

        self.log = logging.getLogger("cocotb.tb")
        self.log.setLevel(logging.DEBUG)

        cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())

    async def cycle_reset(self):
        # s_axi_aresetn is active-low: 1 = released, 0 = asserted
        self.dut.rst.value = 1
        await RisingEdge(self.dut.clk)
        await RisingEdge(self.dut.clk)
        self.dut.rst.value = 0
        await RisingEdge(self.dut.clk)
        await RisingEdge(self.dut.clk)
        self.dut.rst.value = 1
        await RisingEdge(self.dut.clk)
        await RisingEdge(self.dut.clk)


@cocotb.test()
async def per_pixel_sse(dut):
    tb = TB(dut)

    await RisingEdge(dut.clk)
    await tb.cycle_reset()
    await RisingEdge(dut.clk)

    t_col = Color(25, 2, 12, 255)
    b_col = Color(75, 251, 52, 255)
    c_col = Color(52, 31, 2, 255)

    dut.t_col.value = t_col.to_word()
    dut.c_col.value = c_col.to_word()
    dut.b_col.value = b_col.to_word()

    # allow to settle
    await RisingEdge(dut.clk)

    assert Color.delta_SSE(t_col, b_col, c_col) == dut.px_sse.value;
