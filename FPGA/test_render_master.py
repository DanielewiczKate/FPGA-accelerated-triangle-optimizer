import itertools
import logging
import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer, ReadOnly
from common import Color, Vertex, Triangle, monitor, mix64
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
async def reset(dut):
    tb = TB(dut)

    tri = Triangle(
            Color(14, 124, 23, 43),
            [Vertex(1, 21), Vertex(12, 23), Vertex(12, 11)])
    max_coord = Vertex(32,32)

    dut.triangle.value = tri.to_int();
    dut.max_coord.value = max_coord.to_word();


    beats = []
    mon = cocotb.start_soon(
        monitor(dut, dut.clk,
        [
            "idx[0]",
            "s_d0[0]",
            "s_d1[0]",
            "s_d2[0]"
            ],
        beats, gate="pixel_valid")
    )

    await RisingEdge(dut.clk)
    await tb.cycle_reset()
    await RisingEdge(dut.clk)

    dut.pixel_valid.value = 1
    for _ in range(10):
        await RisingEdge(dut.clk)

