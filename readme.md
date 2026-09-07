# System Goal
The triangle optimizer is a hill climber intended to allow for approximation of .bmp files using iterative addition of semi-transparent geometric primitives.

# C++ implementation 
As a baseline a pure C++ implementation is created, benchmarked and optimized.

This system picks a random triangle, optimizes it to reduce the MSE between the target image and the candidate image.


# CLI definition
The tool will be called using a CLI and controlled with a config.yaml file which will allow for different parameters and tests to be completed.

```
./triopt <target.png> <output.png> [iterations] [seed] [patience]
```

# Testing and Validation

## Standard test

Configure and build (Release is the default; the benchmark numbers assume it):

```
cmake -S . -B build
cmake --build build
```

Run the unit suite (doctest, one test binary, discovered per `TEST_CASE` by ctest):

```
ctest --test-dir build --output-on-failure
```

Covers `compute_SSE` and `compute_delta_SSE` (including the delta/full-rescan
identity), the rasterizer (pixel coverage, alpha blending, degenerate
triangles), and PNG load/save round-tripping. All cases must pass.

## Standard optimizer run

```
./build/triopt tests/mona_lisa_256.png out.png 100000 1
```

`triopt <target.png> <output.png> [iterations] [seed] [patience]`. Fixed seed
`1` and the committed `tests/mona_lisa_256.png` fixture make this reproducible:
same seed + same iteration budget must produce an identical SSE trajectory (the
per-commit `best_sse` values printed to stderr), regardless of which scoring
path is compiled in. A changed trajectory means a scoring change is not
bit-exact.

`test.sh` automates this: it runs the optimizer several times at seed `1` /
8000 iterations, asserts every run hits the same hardcoded final SSE
(determinism gate), and reports wall-time stats.

## Benchmark: `golden-model` vs `delta-ssh-model`

Two tags mark the scoring implementations:

| tag | scoring | notes |
| --- | --- | --- |
| `golden-model` | full `compute_SSE` rescan every proposal, O(W*H) | reference |
| `delta-ssh-model` | `best_sse + compute_delta_SSE(...)` over the triangle's bounding box, O(bbox) | |

**Equivalence.** At seed `1` both reach final SSE `54798669` at 8000
iterations, and the 100000-iteration output PNG is byte-identical. The delta
path changes cost, not results (integer squared-error deltas, no rounding).
`ctest` is 15 cases on `golden-model`, 16 on `delta-ssh-model` (the extra one
asserts `compute_SSE(target, cand) == compute_SSE(target, prev) +
compute_delta_SSE(target, prev, cand, bbox)`).

**Wall time**, `tests/mona_lisa_256.png`, seed `1`, Release, one machine:

| proposals | `golden-model` | `delta-ssh-model` | speedup |
| --- | --- | --- | --- |
| 8000 | 679 ms (median of 5) | 645 ms | 1.05x |
| 100000 | 4800 ms (median of 3) | 2897 ms | 1.66x |

The speedup grows with proposal count because bounding boxes shrink as
triangles refine, so the delta scan cost falls while the full rescan stays
flat. The remaining per-proposal O(W*H) cost is the `ImageData trial = best`
copy; removing it is the next step.

# FPGA accelerator

The hardware target is the per-proposal scoring inner loop: given a candidate
triangle and its bounding box, stream the target and previous-best pixels over
that box and return the scalar `delta_SSE`. The search itself (RNG, proposal
generation, accept/reject, canvas commits) stays in software; only the pixel
math is offloaded.

## Partition

| Software (host / PS) | Hardware (`FPGA/`) |
| --- | --- |
| hill-climb loop, proposal generation, accept/reject | per-pixel edge test, alpha blend, squared-error accumulate |
| holds the canonical canvas and target | streamed pixels + one scalar result |
| writes the candidate triangle + bbox, reads the result | `AXILiteWorker` register file + pixel-stream sink |

## `AXILiteWorker` — control + data interface

`FPGA/AXILiteWorker.sv` is the boundary block between the bus and the compute
datapath. It has two independent AXI interfaces plus a plain handshake to the
datapath:

- **AXI4-Lite worker** (32-bit) — the control plane. A CPU writes the candidate
  triangle and bounding box, kicks a render, and reads the 64-bit result back.
- **AXI4-Stream worker** (64-bit) — the data plane. Each beat carries one
  `{t_col, b_col}` pixel pair (`t_col` = `tdata[63:32]`, `b_col` = `[31:0]`);
  `tlast` marks the final pixel of the bounding box. `TSTRB`/`TKEEP` are not
  implemented — every beat is a full pixel pair.
- **Datapath ports** — `start` (1-cycle pulse), `busy`/`done` inputs,
  `delta_sse[63:0]` input, and the decoded `triangle` / `max_coord` structs
  out. `pixel_valid` / `pixel_last` strobes accompany `t_col` / `b_col`;
  `render_ready` is the datapath's backpressure into the stream sink, and the
  datapath owns end-of-packet by deasserting it while it finalises a result.

### Register map

Byte offset, 32-bit words. `RW` = written by the CPU and read by the datapath;
`RO` = driven by the datapath, read-only over the bus; `W1P` = write-1 pulses,
reads back 0.

| offset | name | dir | contents |
| --- | --- | --- | --- |
| `0x00` | `CTRL` | W1P | bit 0 -> `start` pulse |
| `0x04` | `STATUS` | RO | bit 0 = `busy`, bit 1 = `done` (sticky, cleared by `start`) |
| `0x08` | `MAX_COORD` | RW | bounding-box max, `vertex_t` layout |
| `0x0C` | `TRI_V0` | RW | vertex 0, `vertex_t` layout |
| `0x10` | `TRI_V1` | RW | vertex 1 |
| `0x14` | `TRI_V2` | RW | vertex 2 |
| `0x18` | `TRI_COL` | RW | candidate colour, `color_t` layout |
| `0x1C` | `SSE_LO` | RO | `delta_sse[31:0]` |
| `0x20` | `SSE_HI` | RO | `delta_sse[63:32]` |

`SSE_LO` / `SSE_HI` are only coherent once `STATUS.done` is set; software
reads them after polling `done`.

### Packed types (`FPGA/common.sv`)

```
color_t    { logic [7:0] r, g, b, a }              // r = bits [31:24]
vertex_t   { logic [15:0] x, y }                   // x = bits [31:16]
triangle_t { color_t color; vertex_t [2:0] verts } // color is the MSBs
```

The register word layout for a vertex or colour matches the corresponding
struct field order, so the driver and testbench pack bytes the same way the
RTL unpacks them.

## Verification

cocotb testbench, `FPGA/test_axil.py`, driven by `FPGA/Makefile`:

```
cd FPGA
python -m venv .venv && . .venv/bin/activate     # once
pip install cocotb cocotbext-axi

make                 # run the suite under Icarus (default)
make SIM=verilator   # ... under Verilator
make lint            # verilator --lint-only
```

`cocotbext-axi` provides the bus drivers that play the initiating side (its
`AxiLiteMaster` / `AxiStreamSource` classes stand in for the CPU and DMA).
Register and datapath-facing tests run through `@with_stalls`, which repeats
each one four ways: no stalls, idle inserter, backpressure inserter, and both,
so the AXI4-Lite handshake is exercised under channel pauses on every side.

| test | checks |
| --- | --- |
| `write_read_all_regs` | write a distinct value to every RW register, read them all back (write-all-then-read-all catches cross-register clobber) |
| `reset_regs` | write the RW registers, pulse reset, confirm all read 0 |
| `write_delta_sse_and_read` | drive the 64-bit `delta_sse` input, read `SSE_LO`/`SSE_HI`, reassemble and compare |
| `passthough_packing` | write the vertex / colour / bbox registers, check the `triangle` and `max_coord` output structs carry the expected packed value |
| `ctrl_start_pulse` | write `CTRL` bit 0, assert `start` is high exactly one cycle and `CTRL` reads back 0 |
| `pixel_stream_data` | feed deterministic 64-bit beats on the stream port, capture `{t_col, b_col}` on `pixel_valid`, compare each to what was sent |
| `pixel_stream_data_backpressure` | same, toggling `render_ready` to confirm no beats are dropped under stream backpressure |
| `partial_writes` | skipped — `WSTRB` sub-word writes are out of scope (the driver issues full 32-bit words) |

## Status

The interface block and its testbench exist; the compute datapath (edge-
function rasteriser + streaming `delta_SSE` accumulator) is not yet
implemented, and there is no measured hardware-vs-CPU comparison. Any
throughput claim here is pending a cycle model or real synthesis numbers.
