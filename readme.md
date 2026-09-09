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
- **Datapath ports** — `start` / `rst_render` (1-cycle pulses), `busy`/`done`
  inputs, `delta_sse[63:0]` input, and the decoded `triangle` / `max_coord`
  structs out. `pixel_valid` / `pixel_last` strobes accompany `t_col` / `b_col`;
  `render_ready` is the datapath's backpressure into the stream sink.

  As wired in `FPGAAccelerator.sv` today, the datapath is kicked by
  `rst_render` (it resets and re-primes the dispatch); `start` is decoded but
  the compute datapath has no `start` port and does not consume it, and
  `busy` / `done` are left undriven, so `STATUS.done` never asserts. The
  "poll `done`, then read `SSE_LO`/`SSE_HI`" flow is the intended contract,
  not the current behaviour — see Known gaps.

### Register map

Byte offset, 32-bit words. `RW` = written by the CPU and read by the datapath;
`RO` = driven by the datapath, read-only over the bus; `W1P` = write-1 pulses,
reads back 0.

| offset | name | dir | contents |
| --- | --- | --- | --- |
| `0x00` | `CTRL` | W1P | bit 0 -> `start` pulse, bit 1 -> `rst_render` pulse (both self-clearing) |
| `0x04` | `STATUS` | RO | bit 0 = `busy`, bit 1 = `done` (sticky, cleared by `start`) |
| `0x08` | `MAX_COORD` | RW | bounding-box max, `vertex_t` layout |
| `0x0C` | `TRI_V0` | RW | vertex 0, `vertex_t` layout |
| `0x10` | `TRI_V1` | RW | vertex 1 |
| `0x14` | `TRI_V2` | RW | vertex 2 |
| `0x18` | `TRI_COL` | RW | candidate colour, `color_t` layout |
| `0x1C` | `SSE_LO` | RO | `delta_sse[31:0]` |
| `0x20` | `SSE_HI` | RO | `delta_sse[63:32]` |

`SSE_LO` / `SSE_HI` are only coherent once `STATUS.done` is set; software
reads them after polling `done`. (Intended contract — `done` is not yet
driven; see Known gaps.)

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

## `RasterizerDispatch` — edge-function front end

`FPGA/RasterizerDispatch.sv` is the first stage of the compute datapath. It turns
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

- **Precompute, once per triangle.** The per-edge step constants — `A0..A2`
  (vertex-pair `Δy`) and `B0..B2` (vertex-pair `−Δx`) — are plain combinational
  `assign`s off the vertices (they were registered in an earlier revision; that
  created a one-cycle stale-read hazard on the `d*_row` init and was removed).
  The edge-function value at the bbox origin, `d0_row..d2_row`, is registered
  one cycle after reset. Those origin values are the *only* multiplies in the
  block (six, and since this runs once per image they can be folded onto a
  single time-shared multiplier — the `TODO` in the source).
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

cocotb, `FPGA/test_render_dispatch.py`, `make render_dispatch` (add `SIM=verilator`
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

`FPGA/RasterizerWorker.sv` is the compute stage downstream of `RasterizerDispatch`.
It consumes the per-pixel edge functions and colour triple and folds each
covered pixel into a running `delta_SSE`. This is the block the sections above
call `RenderWorker`.

**Inputs**: the `pixel_valid` strobe, the three edge functions
`s_d0` / `s_d1` / `s_d2` (`s33_t`) and `idx` from `RasterizerDispatch`, and the
`t_col` / `b_col` / `tri_col` colour triple for that pixel. **Output**:
`sse_acc[63:0]` — the accumulated signed squared-error delta.

**Per pixel** (combinational except the accumulator):

- `in_tri` — the coverage test: `s_d0` / `s_d1` / `s_d2` all `>= 0` or all `<= 0`.
- `c_col` — the candidate colour after the integer alpha blend of `tri_col` over
  `b_col` (the previous-best pixel), `(b*(255-a) + tri*a)/255`. The `/255` is the
  `(x*32897) >> 23` magic multiply from `RasterizeTriangleV2`, done unsigned in
  the `udiv255` function — the numerator is always in `[0, 65025]`, so no sign
  handling is needed. (An earlier revision blended over `t_col`; the two agree
  only when `t_col == b_col` or `a == 255`, which every all-zero testbench
  happened to satisfy. The `CPP_bindings_random_colors` differential test
  against the C++ is what caught it.)
- `diff_* = b_col - c_col` and `sum_* = 2*t_col - b_col - c_col` per channel;
  `sse_acc += Σ diff*sum` on every `pixel_valid && in_tri`. Roles: `t_col` is the
  target/reference pixel, `b_col` the previous best, `c_col` the candidate. The
  product expands to `(t_col - c_col)² - (t_col - b_col)²` — the multiply-reduced
  Δ(squared error), three multiplies per pixel instead of six.

### Verification

cocotb, `FPGA/test_render_worker.py`, `make render_worker` (add `SIM=verilator`
as elsewhere). The reference is the Python `Color` model in `FPGA/common.py`
(`Color.rasterize` for the blend, `Color.delta_SSE` for the per-pixel error).

| test | checks |
| --- | --- |
| `per_pixel_sse` | 50 random `t_col` / `b_col` / `tri_col` triples (seed `0xC0FFEE`); one clock after driving each, assert the combinational `px_sse` wire equals `Color.delta_SSE(t_col, b_col, Color.rasterize(b_col, tri_col))` |
| `accumulator` | streams the same 50 triples, one per cycle, with `pixel_valid` held high; a `monitor` coroutine samples `sse_acc` on every `pixel_valid` edge, and the last sample must equal the running sum of the per-pixel deltas |

The `in_tri` coverage gate is not exercised here (both tests leave `s_d0` /
`s_d1` / `s_d2` at 0, so `in_tri` is always true); it is covered at the
integration level by the `CPP_bindings_random_triangle` test, which drives real
edge functions through `RasterizerDispatch`. The `sse_acc` reset value and the
`idx` port are still unchecked at unit level.

## `Rasterizer` / `FPGAAccelerator` — datapath wrapper and top level

`FPGA/Rasterizer.sv` instantiates `RasterizerDispatch` and, per lane, a
`RasterizerWorker`, and sums the per-lane `sse_acc` into one `t_sse_acc[63:0]`.
`FPGA/FPGAAccelerator.sv` is the synthesis top: it wires `AXILiteWorker` to
`Rasterizer`, connecting the decoded `triangle` / `max_coord` and the pixel
stream in, and `t_sse_acc` back to the `SSE_LO` / `SSE_HI` registers as
`delta_sse`. The datapath reset is `s_axi_aresetn & ~rst_render`, so a
`CTRL` bit-1 write both clears the accumulator and re-primes the dispatch for a
new triangle.

### Verification

cocotb, `FPGA/test_fpga_accelerator.py`, `make fpga_accel`.

This is the first testbench that runs the full interface → dispatch → worker
path, and the first to differentially test against the **C++ golden model**
rather than the Python `Color` mirror. `FPGA/triopt_ref.py` is a `ctypes`
bridge to `bindings/triopt_c.cpp` (an `extern "C"` shim over `triopt_core`);
build it once with `cmake --build build --target triopt_c`, then the tests call
`RasterizeTriangleV2` and `compute_delta_SSE` directly and compare the RTL
`t_sse_acc` to the value the optimizer's real scoring path would produce.

| test | checks |
| --- | --- |
| `count_sse_pos` / `count_sse_neg` / `large_sse` | fixed 5×5 corner triangle, all-zero pixel stream; assert `t_sse_acc` equals the closed-form `±N(N+1)/2` (scaled) coverage count |
| `CPP_bindings` | same corner triangle, drive it through the C++ bridge and the RTL, assert equal `delta_SSE` |
| `CPP_bindings_random_colors` | corner triangle, **random** target / prev-best images and triangle colour (seeded); 10 trials, each diffed against `compute_delta_SSE` |
| `CPP_bindings_random_triangle` | as above but a random triangle shape; `max_coord` derived from `tri.bounds()`, pixels streamed in the dispatch's `max_coord.x`-wide raster order |

## Status

RTL exists for the whole scoring datapath — `AXILiteWorker`,
`RasterizerDispatch`, `RasterizerWorker`, the `Rasterizer` wrapper, and the
`FPGAAccelerator` top — each with a cocotb testbench, and the top-level path is
differentially tested against the C++ golden model. This is a semi-final RTL
snapshot: the datapath computes the right number, but the control/completion
handshake around it is not finished and there is no synthesis data.

### Known gaps / loose ends

**Completion signalling (biggest).** There is no valid qualifier on the result.
`delta_sse` / `t_sse_acc` is combinational and always live; `busy` / `done` are
undriven in `FPGAAccelerator.sv`, so `STATUS.done` never asserts. Every
testbench works around this by clocking a fixed `N*N + 3` cycles after the last
pixel and reading `t_sse_acc` directly — a magic number that holds only for
`N = 5` on a stall-free stream. Needs an `sse_valid` / `done` strobe
propagated dispatch → worker → register file, matched to the datapath's
pipeline depth.

**`start` vs `rst_render`.** The datapath is kicked by `rst_render` (which
resets it). `start` (CTRL bit 0) is decoded but unused, and `STATUS` is built
around a `start`/`busy`/`done` model that isn't wired. Pick one contract.

**Dispatch priming.** `rst_render` is a 1-cycle pulse; `render_ready` asserts
one cycle after reset. With `A*/B*` now combinational the precompute is
nominally one cycle, but the alignment of `render_ready` with `s_d*` validity
is not proven — add an explicit prime counter or a directed test.

**Worker accumulate gate.** `RasterizerWorker` accumulates on
`pixel_valid && in_tri`, not `&& render_ready`, while the dispatch's `advance`
does gate on `render_ready`. During the prime cycle they can disagree and
double-count the first pixel; current test sequencing hides it.

**Dispatch does not stop at the bbox.** `y` is not tracked and `pixel_last` /
`tlast` is not consumed. If the stream master feeds more than
`max_coord.x * max_coord.y` beats, the walk runs past the bottom row and the
worker may accumulate phantom pixels. Stream length correctness is entirely on
the master.

**Stream raster order.** The driver must emit pixels in the dispatch's
`max_coord.x`-wide raster order, not the image's width — the dispatch assigns
coordinates from its own counter. `_pack_pixel` / the stream loop in the
testbench encode this; a real DMA path would need the same.

**Degenerate triangles.** `CPP_bindings_random_triangle` can generate
zero-area / collinear triangles; RTL-vs-C++ agreement there is unverified.

**Multi-lane.** `NUM_LANES` is a parameter only; the per-lane loops are
pass-throughs and lane routing is unimplemented.

**`PixelIndexer`.** `FPGA/PixelIndexer.sv` (raster-order coordinate generator)
is standalone with its own testbench; it is not instantiated in the datapath.

**No synthesis / timing / resource / power numbers, and no measured
hardware-vs-CPU comparison.** Structural note only: at one lane the datapath
consumes one pixel per cycle, so a worst-case full-frame 256×256 bounding box
is 65536 cycles (~655 µs at 100 MHz). That is arithmetic from the architecture,
not a benchmark; typical bounding boxes are far smaller, and the CPU path
already only scans the bbox.
