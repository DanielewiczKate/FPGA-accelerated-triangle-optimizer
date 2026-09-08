module Rasterizer
  import common::*;
  import RasterizerDispatch::*;
  import RasterizerWorker::*;
  #(
    parameter integer NUM_LANES = 1
  ) (
    input logic clk,
    input logic rst, // active low

    // From AXILite worker
    input logic pixel_valid,
    input logic pixel_last,

    input triangle_t triangle, // candidate triangle
    input vertex_t max_coord, // bbox max, assuming 0,0 min

    input color_t t_col [NUM_LANES],
    input color_t b_col [NUM_LANES],

    output var logic render_ready,

    output var signed [63:0] t_sse_acc
  );

  color_t t_col_out [NUM_LANES];
  color_t b_col_out [NUM_LANES];
  color_t tri_col_out [NUM_LANES];

  s33_t s_d0 [NUM_LANES];
  s33_t s_d1 [NUM_LANES];
  s33_t s_d2 [NUM_LANES];
  logic [31:0] idx [NUM_LANES];
  logic signed [63:0] sse_acc [NUM_LANES];

  RasterizerDispatch #(
    .NUM_LANES(NUM_LANES)
  ) rasterizer_dispatch (
    .clk          (clk),
    .rst          (rst),

    // From AXILite worker
    .pixel_valid  (pixel_valid),
    .pixel_last   (pixel_last),

    .triangle     (triangle),
    .max_coord    (max_coord),

    .t_col        (t_col),
    .b_col        (b_col),

    .render_ready (render_ready),

    .t_col_out    (t_col_out),
    .b_col_out    (b_col_out),
    .tri_col_out  (tri_col_out),

    .s_d0         (s_d0),
    .s_d1         (s_d1),
    .s_d2         (s_d2),
    .idx          (idx)
  );

  //RasterizerWorker
  genvar i;
  generate
    for (i = 0; i < NUM_LANES; i = i + 1) begin : gen_worker
      RasterizerWorker rasterizer_worker (
        .clk         (clk),
        .rst         (rst),

        .pixel_valid (pixel_valid),

        .t_col       (t_col_out[i]),
        .b_col       (b_col_out[i]),
        .tri_col     (tri_col_out[i]),

        .s_d0        (s_d0[i]),
        .s_d1        (s_d1[i]),
        .s_d2        (s_d2[i]),
        .idx         (idx[i]),

        .sse_acc     (sse_acc[i])
      );
    end
  endgenerate

  // Sum per-lane SSE accumulators into a single combined result
  always_comb
  begin
    t_sse_acc = '0;
    for (int j = 0; j < NUM_LANES; j = j + 1)
      t_sse_acc = t_sse_acc + sse_acc[j];
  end
endmodule
