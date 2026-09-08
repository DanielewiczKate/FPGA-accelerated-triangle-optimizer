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

## Benchmark: `RasterizeTriangle` vs `RasterizeTriangleV2`

`RasterizeTriangleV2` computes the three edge functions with running sums
(one add per pixel per edge) instead of a fresh cross product per pixel. Same
integer blend, same corner-sampled coverage rule, so it must be byte-identical
to `RasterizeTriangle` on every input; `RasterizeTriangle` stays the golden
model.

**Equivalence.** `ctest` is 18 cases, including `RasterizeTriangleV2: V2
matches V1` (rasterize the same triangle set with both, assert identical
pixels). Swapping V2 into the optimizer's scoring path leaves the seed-`1` /
8000-iteration final SSE at `54798669` unchanged — the determinism gate is a
free end-to-end check that V2 is bit-exact.

**Wall time**, `tests/mona_lisa_256.png`, seed `1`, 8000 proposals, Release,
one machine, one session, commit `f9b9f8e`, median of 5:

| scoring-path rasterizer | wall time | speedup |
| --- | --- | --- |
| `RasterizeTriangle` (V1) | 641 ms | 1.00x |
| `RasterizeTriangleV2` | 476 ms | 1.35x |

This is A/B on one commit with only the rasterize call swapped, so the ~165 ms
is V2's effect and nothing else. It is whole-optimizer wall time, not an
isolated rasterize microbenchmark — V2's share of the win tracks how many
pixels the proposals cover, which is large early (fresh triangles span much of
the canvas) and shrinks as triangles refine. Not comparable to the
`golden-model` / `delta-ssh-model` table above: that varies the SSE scan, this
varies the rasterizer. The `golden` / `delta` numbers there were taken before
this swap.

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

## `RasterizerMaster` — edge-function front end

`FPGA/RasterizerMaster.sv` is the first stage of the compute datapath. It turns
the decoded `triangle` + `max_coord` from `AXILiteWorker` into the per-pixel
quantities a downstream `RenderWorker` needs, walking the bounding box in raster
order and emitting one set of edge-function values per pixel. It is the RTL port
of `RasterizeTriangleV2`'s incremental edge-function scheme (`src/rasterizer.cpp`).

**Inputs**: `triangle`, `max_coord` (bbox max, `(0,0)` min assumed), the
`pixel_valid` / `pixel_last` stream strobes, and the streamed `t_col` / `b_col`
pixel pair (per lane). **Outputs**, per lane: `s_d0` / `s_d1` / `s_d2` — the
three edge functions, `s33_t` = signed 33-bit — and `idx`, the linear pixel
index, plus `t_col_out` / `b_col_out` / `tri_col_out` forwarded straight
through. `render_ready` is the backpressure strobe back toward the stream sink.

### Two-phase operation

- **Precompute, once per triangle.** From the three vertices, register the
  per-edge step constants — `A0..A2` (vertex-pair `Δy`) and `B0..B2`
  (vertex-pair `−Δx`) — and the edge-function value at the bbox origin,
  `d0_row..d2_row`. Those origin values are the *only* multiplies in the block
  (six, and since this runs once per image they can be folded onto a single
  time-shared multiplier — the `TODO` in the source).
- **Per-pixel, incremental.** Each consumed pixel adds `A*` to the running edge
  value (`s_d*`) and bumps `idx`. End of row is detected a cycle ahead by
  `x + 1 >= max_coord.x`; on that boundary `x` wraps to 0 and the row
  accumulators step by `B*` instead. One add per edge per pixel, no multiplies.
  `y` is not tracked here — the stream master owns `max_coord.y` and end of
  packet.

`advance = pixel_valid && render_ready` gates every update, so the walk only
moves on cycles where a pixel is actually consumed.

The coverage test (`d0`, `d1`, `d2` all the same sign), the alpha blend, and the
squared-error accumulate live in the block comment as the spec for the
downstream `RenderWorker`; none of that is in this module.

### Multi-lane

`NUM_LANES` is a parameter for a future mode that processes several bbox rows in
parallel by giving each lane its own `d*` / `idx` offset. It is `1` everywhere
today; the per-lane loops are pass-throughs and lane routing is not implemented.

### Verification

cocotb, `FPGA/test_render_master.py`, `make render_master` (add `SIM=verilator`
as elsewhere).

Golden reference is `tests/data/render_dump.txt`: a list of `(idx, d0, d1, d2)`
tuples dumped by the C++ `RasterizeTriangleV2` when built with `-DTRIOPT_DUMP=ON`
(`dump.sh`, via the `Dump Triangles` ctest case). A `monitor` coroutine samples
`idx` / `s_d0` / `s_d1` / `s_d2` on every `pixel_valid` cycle and the captured
beats are compared against the dump, so the RTL edge-function walk is
differentially tested against the same V2 rasterizer that is the C++ golden
model.

| test | checks |
| --- | --- |
| `single_row_test` | one 5×5 triangle, first bbox row only; assert the first 4 beats match the dump |
| `multi_row_test` | same triangle, full 5×5 bbox; assert every captured beat matches the dump, in order |

## `RasterizerWorker` — coverage test + blend + squared-error accumulate

`FPGA/RasterizerWorker.sv` is the compute stage downstream of `RasterizerMaster`.
It consumes the per-pixel edge functions and colour triple and folds each
covered pixel into a running `delta_SSE`. This is the block the sections above
call `RenderWorker`.

**Inputs**: `pixel_valid` / `pixel_last` strobes, the three edge functions
`s_d0` / `s_d1` / `s_d2` (`s33_t`) and `idx` from `RasterizerMaster`, and the
`t_col` / `b_col` / `tri_col` colour triple for that pixel. **Output**:
`sse_acc[63:0]` — the accumulated signed squared-error delta.

**Per pixel** (combinational except the accumulator):

- `in_tri` — the coverage test: `s_d0` / `s_d1` / `s_d2` all `>= 0` or all `<= 0`.
- `c_col` — the candidate colour after the integer alpha blend of `tri_col` over
  `t_col`, `t + a*(tri - t)/255`. The `/255` is the `(x*32897) >> 23` magic
  multiply from `RasterizeTriangleV2`, sign-corrected in the `sdiv255` function.
- `diff_* = b_col - c_col` and `sum_* = 2*t_col - b_col - c_col` per channel;
  `sse_acc += Σ diff*sum` on every `pixel_valid`. This is the multiply-reduced
  `Δ(squared error) = (c - b)(c + b - 2t)` form — three multiplies per pixel
  instead of six.

### Not done yet

- The module still declares `module RasterizerMaster`, so `make render_worker`
  cannot elaborate it (`COCOTB_TOPLEVEL = RasterizerWorker`) until it is renamed.
- `in_tri` is computed but unused — the accumulate fires on every `pixel_valid`,
  covered or not. Either gate the accumulate on `in_tri` or gate `pixel_valid`
  upstream in the master.
- `idx` and `pixel_last` are ports but are not read.
- `FPGA/test_render_worker.py` is a stub (`TB` class only, no `@cocotb.test()`).
  No differential check against the C++ `compute_delta_SSE` exists yet.

### Verification

cocotb, `FPGA/test_render_worker.py`, `make render_worker` (add `SIM=verilator`
as elsewhere). Wired into the `Makefile` but not runnable until the items above
are resolved.

## Status

- `AXILiteWorker` (bus / control interface) and `RasterizerMaster` (edge-
  function front end) exist, each with a cocotb testbench.
- `RasterizerWorker` — the coverage test + alpha blend + streaming squared-error
  accumulator that consumes `RasterizerMaster`'s output — exists in RTL but is
  a work in progress (see its section: wrong module name, coverage gating and
  testbench still missing). The top level that wires the interface, the master,
  the worker, and `PixelIndexer` (`FPGA/PixelIndexer.sv`, a standalone raster-
  order coordinate generator) into one datapath is not implemented.
- No measured hardware-vs-CPU comparison exists. Any throughput claim is
  pending a cycle model or synthesis numbers.
