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
async def single_row_test(dut):
    tb = TB(dut)

    N = 5
    tri = Triangle(
            Color(255, 255, 255, 255),
            [Vertex(0, 0), Vertex(N-1, 0), Vertex(N-1, N-1)])
    max_coord = Vertex(N, N)

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

    dut.pixel_valid.value = 0

    await RisingEdge(dut.clk)
    await tb.cycle_reset()
    await RisingEdge(dut.clk)

    dut.pixel_valid.value = 1
    for _ in range(4):
        await RisingEdge(dut.clk)
    expected = common.load_dump("render_dump.txt")
    beats = [
        (idx, common.as_signed(s_d0, 33), common.as_signed(s_d1, 33), common.as_signed(s_d2, 33))
        for idx, s_d0, s_d1, s_d2 in beats
    ]

    for i in range(4):
        assert expected[i][0] == beats[i][0]
        assert expected[i][1] == beats[i][1]
        assert expected[i][2] == beats[i][2]
        assert expected[i][3] == beats[i][3]



@cocotb.test()
async def multi_row_test(dut):
    tb = TB(dut)

    N = 5
    tri = Triangle(
            Color(255, 255, 255, 255),
            [Vertex(0, 0), Vertex(N-1, 0), Vertex(N-1, N-1)])
    max_coord = Vertex(N, N)

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

    dut.pixel_valid.value = 0

    await RisingEdge(dut.clk)
    await tb.cycle_reset()
    await RisingEdge(dut.clk)

    dut.pixel_valid.value = 1
    for _ in range(N*N):
        await RisingEdge(dut.clk)
    expected = common.load_dump("render_dump.txt")
    beats = [
        (idx, common.as_signed(s_d0, 33), common.as_signed(s_d1, 33), common.as_signed(s_d2, 33))
        for idx, s_d0, s_d1, s_d2 in beats
    ]

    assert beats == expected
