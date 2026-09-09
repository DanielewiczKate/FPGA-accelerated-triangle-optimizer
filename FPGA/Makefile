# cocotb + simulator harness for the FPGA RTL.
#
#   make                      # run the default DUT's tests (axil, Icarus)
#   make axil                 # AXILiteWorker tests
#   make pixel_indexer        # PixelIndexer tests
#   make render_dispatch      # RasterizerDispatch tests
#   make render_worker        # RasterizerWorker tests
#   make fpga_accel           # FPGAAccelerator full-datapath tests
#   make both                 # every DUT, in sequence
#   make DUT=pixel_indexer SIM=verilator
#   make lint                 # verilator --lint-only over all RTL
#   make clean-all
#
# Requires: cocotb + cocotbext-axi (pip), and iverilog or verilator.

SIM           ?= icarus
TOPLEVEL_LANG ?= verilog
DUT           ?= axil
# WAVES=1 -> full-hierarchy waveform dump (see the 'waves' target)
WAVES         ?= 0

# ---- per-DUT selection -----------------------------------------------------
COMMON_RTL := $(PWD)/common.sv

ifeq ($(DUT),axil)
  COCOTB_TOPLEVEL     = AXILiteWorker
  COCOTB_TEST_MODULES = test_axil
  DUT_RTL             = $(PWD)/AXILiteWorker.sv
else ifeq ($(DUT),pixel_indexer)
  COCOTB_TOPLEVEL     = PixelIndexer
  COCOTB_TEST_MODULES = test_pixel_indexer
  DUT_RTL             = $(PWD)/PixelIndexer.sv
else ifeq ($(DUT),render_dispatch)
  COCOTB_TOPLEVEL     = RasterizerDispatch
  COCOTB_TEST_MODULES = test_render_dispatch
  DUT_RTL             = $(PWD)/RasterizerDispatch.sv
else ifeq ($(DUT),render_worker)
  COCOTB_TOPLEVEL     = RasterizerWorker
  COCOTB_TEST_MODULES = test_render_worker
  DUT_RTL             = $(PWD)/RasterizerWorker.sv
else ifeq ($(DUT),fpga_accel)
  COCOTB_TOPLEVEL     = FPGAAccelerator
  COCOTB_TEST_MODULES = test_fpga_accelerator
  DUT_RTL             = $(PWD)/AXILiteWorker.sv \
                       $(PWD)/RasterizerDispatch.sv \
                       $(PWD)/RasterizerWorker.sv \
                       $(PWD)/Rasterizer.sv \
                       $(PWD)/FPGAAccelerator.sv
else
  $(error unknown DUT '$(DUT)' -- use 'axil', 'pixel_indexer', 'render_dispatch', 'render_worker' or 'fpga_accel')
endif

VERILOG_SOURCES     += $(COMMON_RTL) $(DUT_RTL)
SIM_BUILD            = sim_build/$(DUT)
COCOTB_RESULTS_FILE  = results.$(DUT).xml

ifeq ($(SIM),icarus)
    COMPILE_ARGS += -g2012
endif
ifeq ($(SIM),verilator)
    EXTRA_ARGS += -Wall -Wno-fatal
endif

# ---- waveform dump (opt-in: WAVES=1) -------------------------------------
# Off by default -- a full-datapath trace is large and slows the run.
#   Icarus:    cocotb's Makefile.icarus already honours WAVES -- on WAVES=1
#              it generates a $dumpvars(0, $(COCOTB_TOPLEVEL)) module and
#              writes $(SIM_BUILD)/$(COCOTB_TOPLEVEL).fst; on WAVES=0 it
#              passes -none to actively disable dumping. Nothing to do here.
#   Verilator: needs --trace at build (and it's harmless on the run cmd,
#              where EXTRA_ARGS is also forwarded). Writes $(SIM_BUILD)/dump.vcd.
ifeq ($(WAVES),1)
  ifeq ($(SIM),verilator)
    EXTRA_ARGS += --trace --trace-structs
  endif
endif

include $(shell cocotb-config --makefiles)/Makefile.sim

# ---- convenience targets -------------------------------------------------
.PHONY: axil pixel_indexer render_dispatch render_worker fpga_accel both lint waves clean-all

axil:
	$(MAKE) sim DUT=axil

pixel_indexer:
	$(MAKE) sim DUT=pixel_indexer

render_dispatch:
	$(MAKE) sim DUT=render_dispatch

render_worker:
	$(MAKE) sim DUT=render_worker

fpga_accel:
	$(MAKE) sim DUT=fpga_accel

both:
	$(MAKE) sim DUT=axil
	$(MAKE) sim DUT=pixel_indexer
	$(MAKE) sim DUT=render_dispatch
	$(MAKE) sim DUT=render_worker
	$(MAKE) sim DUT=fpga_accel

ALL_RTL := $(PWD)/common.sv $(PWD)/AXILiteWorker.sv $(PWD)/PixelIndexer.sv \
           $(PWD)/RasterizerDispatch.sv $(PWD)/RasterizerWorker.sv \
           $(PWD)/Rasterizer.sv $(PWD)/FPGAAccelerator.sv
# -Wno-MULTIDRIVEN: tolerate `initial x = 0;` beside an always_ff reset (a
#   synthesizable Xilinx power-on idiom) if it's ever reintroduced.
lint:
	verilator --lint-only -Wall -Wno-MULTIDRIVEN $(ALL_RTL)

# Open the most recent dump for the selected DUT: Icarus writes
# <toplevel>.fst, Verilator writes dump.vcd. Run a DUT with WAVES=1 first,
# e.g.  make fpga_accel WAVES=1  &&  make waves DUT=fpga_accel
waves:
	@f=$$(ls -t $(SIM_BUILD)/$(COCOTB_TOPLEVEL).fst $(SIM_BUILD)/dump.vcd 2>/dev/null | head -1); \
	if [ -z "$$f" ]; then echo "no dump in $(SIM_BUILD) -- run e.g. 'make $(DUT) WAVES=1' first"; exit 1; fi; \
	echo "opening $$f"; gtkwave $$f >/dev/null 2>&1 &

clean-all: clean
	rm -rf __pycache__ sim_build results.*.xml dump.vcd obj_dir
