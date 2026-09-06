// ============================================================================
//  AI-GENERATED FILE  --  hill-climbing optimizer driver
//
//  Written by Claude (Anthropic's AI assistant), at the repo owner's request,
//  as SCAFFOLDING ONLY. It is marked AI-authored on purpose so it is never
//  mistaken for the hand-written work this project exists to demonstrate.
//
//  The code that IS the point of this project -- the rasterizer
//  (src/rasterizer.*), the SSE metric (src/SSE.*), PNG I/O (src/image.*), and
//  the future delta-SSE / FPGA datapath -- is the repo owner's own. This file
//  only wires those pieces into a search loop.
//
//  Algorithm: greedy hill climbing by triangle accretion. Each "round"
//  locally optimizes ONE triangle, then commits it to the canvas.
//    - Start from a blank canvas.
//    - Each step, propose a triangle and score it by rasterizing onto a COPY
//      of the current best image and measuring full SSE against the target:
//        * no triangle held yet   -> ERROR-WEIGHTED proposal: sample the three
//          vertices from a distribution proportional to current per-pixel
//          squared error (so new triangles are born where the canvas is most
//          wrong), and seed the colour from the target pixel at the first
//          vertex.
//        * a triangle is held     -> mostly a gaussian NUDGE of the held
//          triangle (vertices + colour), occasionally an error-weighted
//          proposal for escape. This local refinement is what makes edges
//          line up with real image features instead of landing at random.
//    - The per-pixel error CDF that placement samples from is rebuilt once per
//      commit (the canvas only changes then), not every step -- same idea as
//      the reference C project rebuilding its error SAT once per locked layer.
//    - Hold the best-scoring proposal of the round as "pending"; a strictly
//      better proposal replaces it and resets the stale counter. Once
//      `patience` proposals in a row fail to beat it, commit it to the canvas
//      and start a new round.
//    - Degenerate/sliver triangles (area below a small fraction of the canvas)
//      are rejected at proposal time -- placements are re-rolled, nudges that
//      collapse a triangle are reverted.
//    - `iterations` is the total proposal budget, not the committed-triangle
//      count -- expect very roughly iterations/patience triangles out.
//    - The committed canvas never gets worse, so the loop climbs monotonically.
//    Still no population/crossover or retry cap -- those live in the reference
//    C project this is modelled on.
//
//  Deliberately naive, for baseline measurement:
//    * full O(W*H) image copy  every step   -- incremental canvas replaces this
//    * full O(W*H) SSE rescan  every step   -- delta-SSE / FPGA path replaces this
//  Those two costs are exactly what the efficiency and pipelining work is
//  meant to remove, so they are left in as the reference to measure against.
// ============================================================================

#include "common.hpp"
#include "image.hpp"
#include "rasterizer.hpp"
#include "SSE.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Options {
    std::string target_path;
    std::string output_path;
    uint64_t iterations = 2000;
    uint32_t seed = 0xC0FFEEu;
    uint64_t patience = 350;
    uint64_t report_every = 1000;
};

bool parse_args(int argc, char** argv, Options& opt) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <target.png> <output.png> [iterations] [seed] [patience]\n"
                     "  (AI-generated hill-climbing driver; see file header)\n",
                     argc > 0 ? argv[0] : "triopt");
        return false;
    }
    opt.target_path = argv[1];
    opt.output_path = argv[2];
    if (argc >= 4) opt.iterations = std::strtoull(argv[3], nullptr, 10);
    if (argc >= 5) opt.seed = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
    if (argc >= 6) opt.patience = std::strtoull(argv[5], nullptr, 10);
    return true;
}

// --- tuning knobs ----------------------------------------------------------
// Item 5: keep proposed alpha high enough that a triangle actually moves the
// score. Very transparent triangles waste proposals. (Init range; nudges are
// clamped to the same band.)
constexpr int kAlphaLo = 90;
constexpr int kAlphaHi = 255;

// Item 3: reject triangles whose area is below this fraction of the canvas.
// Matches the reference C project's 0.0005-of-unit-square threshold.
constexpr double kMinAreaFrac = 0.0005;

// Item 1: local-refinement knobs.
constexpr double kExploreProb  = 0.20;  // chance to roll fresh-random instead of nudging
constexpr double kMutRatePos   = 0.50;  // per-vertex chance to nudge
constexpr double kMutRateCol   = 0.90;  // chance to nudge the colour
constexpr double kSigmaPosFrac = 0.04;  // vertex-nudge sigma, as a fraction of max(w,h)
constexpr double kSigmaCol     = 12.0;  // colour-channel nudge sigma, in [0,255] units
// -------------------------------------------------------------------------

pcrd_t clamp_coord(double v, pcrd_t size) {
    if (v <= 0.0) return 0;
    const double hi = static_cast<double>(size - 1);
    return static_cast<pcrd_t>(v >= hi ? hi : v);
}

col_t clamp_u8(double v) {
    if (v <= 0.0) return 0;
    return static_cast<col_t>(v >= 255.0 ? 255.0 : v);
}

col_t clamp_alpha(double v) {
    if (v <= kAlphaLo) return static_cast<col_t>(kAlphaLo);
    return static_cast<col_t>(v >= kAlphaHi ? kAlphaHi : v);
}

// Twice the signed area is the edge-function cross product; halve the abs.
double tri_area_px(const Triangle& t) {
    const double ax = t.verts_x[0], ay = t.verts_y[0];
    const double bx = t.verts_x[1], by = t.verts_y[1];
    const double cx = t.verts_x[2], cy = t.verts_y[2];
    return 0.5 * std::fabs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay));
}

// Item 2: running prefix sum of per-pixel squared RGB error (candidate vs
// target), one entry per pixel. cdf.back() is the whole-image SSE. Sampling
// an index weighted by error is then "uniform draw in [0, total) + binary
// search". Rebuilt only when the canvas changes (i.e. on commit).
std::vector<double> build_error_cdf(const ImageData& target, const ImageData& canvas) {
    const std::size_t n = target.size();
    std::vector<double> cdf(n);
    const Color* t = target.data();
    const Color* c = canvas.data();
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const int dr = static_cast<int>(t[i].r) - static_cast<int>(c[i].r);
        const int dg = static_cast<int>(t[i].g) - static_cast<int>(c[i].g);
        const int db = static_cast<int>(t[i].b) - static_cast<int>(c[i].b);
        acc += static_cast<double>(dr * dr + dg * dg + db * db);
        cdf[i] = acc;
    }
    return cdf;
}

// Draw a flat pixel index with probability proportional to its error weight.
std::size_t sample_pixel(const std::vector<double>& cdf, std::mt19937& rng) {
    std::uniform_real_distribution<double> u(0.0, cdf.back());
    const auto it = std::upper_bound(cdf.begin(), cdf.end(), u(rng));
    std::size_t idx = static_cast<std::size_t>(it - cdf.begin());
    if (idx >= cdf.size()) idx = cdf.size() - 1;  // guard the [0,total) edge
    return idx;
}

// Uniformly-random triangle: vertices in [0,w-1] x [0,h-1] (the rasterizer's
// in-bounds precondition), random RGB, alpha in [kAlphaLo,kAlphaHi]. Re-rolls
// the vertices until the triangle clears the sliver threshold (item 3); the
// attempt cap keeps this terminating on tiny canvases where it may not.
Triangle random_triangle(std::mt19937& rng, pcrd_t w, pcrd_t h) {
    std::uniform_int_distribution<int> dist_x(0, w - 1);
    std::uniform_int_distribution<int> dist_y(0, h - 1);
    std::uniform_int_distribution<int> dist_c(0, 255);
    std::uniform_int_distribution<int> dist_a(kAlphaLo, kAlphaHi);

    const double min_area = kMinAreaFrac * static_cast<double>(w) * h;
    Triangle t;
    for (int attempt = 0; attempt < 64; ++attempt) {
        for (int i = 0; i < 3; ++i) {
            t.verts_x[i] = static_cast<pcrd_t>(dist_x(rng));
            t.verts_y[i] = static_cast<pcrd_t>(dist_y(rng));
        }
        if (tri_area_px(t) >= min_area) break;
    }
    t.color.r = static_cast<col_t>(dist_c(rng));
    t.color.g = static_cast<col_t>(dist_c(rng));
    t.color.b = static_cast<col_t>(dist_c(rng));
    t.color.a = static_cast<col_t>(dist_a(rng));
    return t;
}

// Item 2: error-weighted proposal. Sample each vertex from the per-pixel error
// distribution (via `cdf`), jitter it by the same gaussian the nudge uses so
// the three points are not stuck on exact pixel centres, and seed the colour
// from the target at the first sampled pixel. Falls back to uniform-random if
// the canvas already matches the target (zero total error).
Triangle error_weighted_triangle(std::mt19937& rng, const ImageData& target,
                                 const std::vector<double>& cdf, pcrd_t w, pcrd_t h) {
    if (cdf.empty() || cdf.back() <= 0.0) return random_triangle(rng, w, h);

    std::normal_distribution<double> jit(0.0, kSigmaPosFrac *
                                                 static_cast<double>(std::max(w, h)));
    const double min_area = kMinAreaFrac * static_cast<double>(w) * h;

    Triangle t;
    std::size_t seed_px = 0;
    for (int attempt = 0; attempt < 64; ++attempt) {
        for (int i = 0; i < 3; ++i) {
            const std::size_t px = sample_pixel(cdf, rng);
            if (i == 0) seed_px = px;
            t.verts_x[i] = clamp_coord(static_cast<double>(px % w) + jit(rng), w);
            t.verts_y[i] = clamp_coord(static_cast<double>(px / w) + jit(rng), h);
        }
        if (tri_area_px(t) >= min_area) break;
    }

    const Color* tp = target.data();
    t.color.r = tp[seed_px].r;
    t.color.g = tp[seed_px].g;
    t.color.b = tp[seed_px].b;
    t.color.a = static_cast<col_t>(
        std::uniform_int_distribution<int>(kAlphaLo, kAlphaHi)(rng));
    return t;
}

// Item 1: local refinement. Copy `src`, nudge ~half its vertices by a gaussian
// in pixel space and (usually) the colour by a gaussian in [0,255], staying
// in-bounds. If the nudge collapses the triangle to a sliver (item 3), return
// `src` unchanged rather than emit a degenerate proposal.
Triangle mutate_triangle(const Triangle& src, std::mt19937& rng, pcrd_t w, pcrd_t h) {
    const double sigma_pos =
        kSigmaPosFrac * static_cast<double>(std::max(w, h));
    std::normal_distribution<double> npos(0.0, sigma_pos);
    std::normal_distribution<double> ncol(0.0, kSigmaCol);
    std::uniform_real_distribution<double> u01(0.0, 1.0);

    Triangle m = src;
    for (int i = 0; i < 3; ++i) {
        if (u01(rng) < kMutRatePos) {
            m.verts_x[i] = clamp_coord(m.verts_x[i] + npos(rng), w);
            m.verts_y[i] = clamp_coord(m.verts_y[i] + npos(rng), h);
        }
    }
    if (u01(rng) < kMutRateCol) {
        m.color.r = clamp_u8(m.color.r + ncol(rng));
        m.color.g = clamp_u8(m.color.g + ncol(rng));
        m.color.b = clamp_u8(m.color.b + ncol(rng));
        m.color.a = clamp_alpha(m.color.a + ncol(rng));
    }

    const double min_area = kMinAreaFrac * static_cast<double>(w) * h;
    return tri_area_px(m) < min_area ? src : m;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) return 2;

    std::optional<ImageData> target_opt = LoadImageDataFromPNG(opt.target_path);
    if (!target_opt) {
        std::fprintf(stderr, "failed to load target PNG: %s\n",
                     opt.target_path.c_str());
        return 1;
    }
    const ImageData target = std::move(*target_opt);
    const pcrd_t w = target.x_size();
    const pcrd_t h = target.y_size();
    if (w == 0 || h == 0) {
        std::fprintf(stderr, "target image has a zero dimension\n");
        return 1;
    }

    // Current best starts blank. Any fill works; opaque black keeps the blend
    // math trivial and gives a deterministic starting SSE.
    ImageData best(w, h, Color{0, 0, 0, 255});
    uint64_t best_sse = compute_SSE(target, best);
    const uint64_t start_sse = best_sse;

    // Per-pixel error CDF for error-weighted placement (item 2). Rebuilt after
    // every commit, since that is the only time `best` changes.
    std::vector<double> error_cdf = build_error_cdf(target, best);

    std::mt19937 rng(opt.seed);

    std::optional<ImageData> pending;  // best proposal so far this round;
    Triangle pending_tri;             //   its triangle (the nudge seed);
    uint64_t pending_sse = 0;          //   committed once it survives
    uint64_t stale = 0;               //   `patience` proposals unbeaten
    uint64_t committed = 0;
    std::uniform_real_distribution<double> u01(0.0, 1.0);

    for (uint64_t step = 0; step < opt.iterations; ++step) {
        // Item 1: nudge the held triangle most of the time; otherwise (and
        // always before the first hold) roll a fresh random one.
        const Triangle cand =
            (pending && u01(rng) >= kExploreProb)
                ? mutate_triangle(pending_tri, rng, w, h)
                : error_weighted_triangle(rng, target, error_cdf, w, h);

        ImageData trial = best;                          // O(W*H) copy
        RasterizeTriangle(trial, cand);                  // writes only the bbox
        const uint64_t trial_sse = compute_SSE(target, trial);  // O(W*H) rescan

        // Beat the pending triangle if there is one, otherwise the canvas.
        const uint64_t bar = pending ? pending_sse : best_sse;
        if (trial_sse < bar) {
            pending = std::move(trial);
            pending_tri = cand;
            pending_sse = trial_sse;
            stale = 0;
        } else {
            ++stale;
        }

        if (pending && stale >= opt.patience) {
            best = std::move(*pending);
            best_sse = pending_sse;
            pending.reset();
            stale = 0;
            ++committed;
            error_cdf = build_error_cdf(target, best);  // canvas changed
        }

        if (opt.report_every != 0 && (step + 1) % opt.report_every == 0) {
            std::fprintf(stderr,
                         "step %llu/%llu  committed=%llu  best_sse=%llu  "
                         "pending_sse=%lld  stale=%llu\n",
                         static_cast<unsigned long long>(step + 1),
                         static_cast<unsigned long long>(opt.iterations),
                         static_cast<unsigned long long>(committed),
                         static_cast<unsigned long long>(best_sse),
                         pending ? static_cast<long long>(pending_sse) : -1LL,
                         static_cast<unsigned long long>(stale));
        }
    }

    // A triangle still pending at the end of the budget was a real improvement,
    // it just never sat unbeaten for the full patience window. Commit it.
    if (pending) {
        best = std::move(*pending);
        best_sse = pending_sse;
        ++committed;
    }

    if (!SaveImageDataToPNG(opt.output_path, best)) {
        std::fprintf(stderr, "failed to write output PNG: %s\n",
                     opt.output_path.c_str());
        return 1;
    }

    std::fprintf(stderr,
                 "done. sse %llu -> %llu (%.1f%%)  committed %llu triangles over "
                 "%llu proposals  patience %llu  seed %u\n",
                 static_cast<unsigned long long>(start_sse),
                 static_cast<unsigned long long>(best_sse),
                 start_sse ? 100.0 * static_cast<double>(best_sse) /
                                 static_cast<double>(start_sse)
                           : 0.0,
                 static_cast<unsigned long long>(committed),
                 static_cast<unsigned long long>(opt.iterations),
                 static_cast<unsigned long long>(opt.patience),
                 opt.seed);
    return 0;
}
