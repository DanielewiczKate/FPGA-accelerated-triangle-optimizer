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
