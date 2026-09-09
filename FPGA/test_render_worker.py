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

    NUM_TRIALS = 50

    rng = random.Random(0xC0FFEE)

    for trial in range(NUM_TRIALS):
        t_col = Color(
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
        )
        b_col = Color(
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
        )
        tri_col = Color(
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
        )

        dut.t_col.value = t_col.to_word()
        dut.b_col.value = b_col.to_word()
        dut.tri_col.value = tri_col.to_word()

        c_col = Color.rasterize(b_col, tri_col)

        # allow to settle
        await RisingEdge(dut.clk)

        expected = Color.delta_SSE(t_col, b_col, c_col)
        assert expected == dut.px_sse.value, \
            f"Trial {trial}: expected {expected}, got {dut.px_sse.value} " \
            f"(t_col={t_col}, b_col={b_col}, tri_col={tri_col})"


@cocotb.test()
async def accumulator(dut):
    tb = TB(dut)

    await RisingEdge(dut.clk)
    await tb.cycle_reset()
    await RisingEdge(dut.clk)

    beats = []
    mon = cocotb.start_soon(
        monitor(dut, dut.clk,
        [
            "sse_acc"
            ],
        beats, gate="pixel_valid")
    )

    NUM_TRIALS = 50

    rng = random.Random(0xC0FFEE)

    dut.pixel_valid.value = 1;
    dut.in_tri.value = 1;

    delta_sse_acc = []
    running_total = 0

    for trial in range(NUM_TRIALS):
        t_col = Color(
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
        )
        b_col = Color(
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
        )
        tri_col = Color(
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
            rng.randint(0, 255),
        )

        dut.t_col.value = t_col.to_word()
        dut.b_col.value = b_col.to_word()
        dut.tri_col.value = tri_col.to_word()

        c_col = Color.rasterize(b_col, tri_col)

        running_total += Color.delta_SSE(t_col, b_col, c_col)
        delta_sse_acc.append(running_total)

        # allow to settle
        await RisingEdge(dut.clk)

    await RisingEdge(dut.clk)

    beats = [
        (common.as_signed(sse_acc, 64))
        for (sse_acc, ) in beats
    ]

    assert delta_sse_acc[NUM_TRIALS-1] == beats[NUM_TRIALS-1]
