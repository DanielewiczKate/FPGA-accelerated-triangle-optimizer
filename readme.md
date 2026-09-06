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
