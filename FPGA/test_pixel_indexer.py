import itertools
import logging
import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer, ReadOnly
from common import Color, Vertex, Triangle
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
async def counting(dut):
    tb = TB(dut)
    dut.max_coord.value = Vertex(5, 5).to_word()
    dut.pixel_done.value = 1 # renderer has zero backpressure
    dut.pixel_valid.value = 0
    await tb.cycle_reset()

    dut.pixel_valid.value = 1
    await ReadOnly()

    for y in range(5):
        for x in range(5):
            assert dut.coord.value == Vertex(x,y).to_word()
            await RisingEdge(dut.clk)    # one advance
            await ReadOnly()             # let coord's NBA update propagate

@cocotb.test()
async def counting_with_backpressure(dut):
    tb = TB(dut)
    dut.max_coord.value = Vertex(5, 5).to_word()
    dut.pixel_done.value = 1 # renderer has zero backpressure
    dut.pixel_valid.value = 0
    await tb.cycle_reset()

    dut.pixel_valid.value = 1
    await ReadOnly()

    for y in range(5):
        for x in range(5):
            assert dut.coord.value == Vertex(x,y).to_word()
            await RisingEdge(dut.clk)    # one advance
            await ReadOnly()             # let coord's NBA update propagate
            if x == 2:
                await Timer(2, "ns")
                dut.pixel_valid.value = 0
                await Timer(2, "ns")
                dut.pixel_valid.value = 1
                await ReadOnly()
        if y == 2:
            await Timer(2, "ns")
            dut.pixel_valid.value = 0
            await Timer(2, "ns")
            dut.pixel_valid.value = 1
            await ReadOnly()
