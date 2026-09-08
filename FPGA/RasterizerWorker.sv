module RasterizerWorker
  import common::*;
  (
    input logic clk,
    input logic rst, // active low

    // From AXILite worker
    input logic pixel_valid,

    input color_t t_col,
    input color_t b_col,
    input color_t tri_col,


    input var s33_t s_d0,
    input var s33_t s_d1,
    input var s33_t s_d2,
    input var logic [31:0] idx,

    output logic signed [63:0] sse_acc

    );

  wire in_tri = (s_d0 >= 0 && s_d1 >= 0 && s_d2 >= 0) ||
                (s_d0 <= 0 && s_d1 <= 0 && s_d2 <= 0);

  // c_col.x = (t_col.x*(255-a) + tri_col.x*a) / 255, matching Color.rasterize.
  // Numerator is always in [0, 65025], so an unsigned floor divide matches
  // Python's `// 255` exactly (no sign handling, no trunc-vs-floor mismatch).
  color_t c_col;
  wire [7:0] blend_a  = tri_col.a;
  wire [7:0] blend_ia = 8'd255 - blend_a;
  assign c_col.r = udiv255(t_col.r * blend_ia + tri_col.r * blend_a);
  assign c_col.g = udiv255(t_col.g * blend_ia + tri_col.g * blend_a);
  assign c_col.b = udiv255(t_col.b * blend_ia + tri_col.b * blend_a);

  wire signed [8:0]  diff_r = b_col.r - c_col.r;
  wire signed [8:0]  diff_g = b_col.g - c_col.g;
  wire signed [8:0]  diff_b = b_col.b - c_col.b;

  wire signed [9:0]  sum_r  = (t_col.r <<< 1) - b_col.r - c_col.r;
  wire signed [9:0]  sum_g  = (t_col.g <<< 1) - b_col.g - c_col.g;
  wire signed [9:0]  sum_b  = (t_col.b <<< 1) - b_col.b - c_col.b;

  wire signed [63:0]  px_sse =
        diff_r * sum_r +
        diff_g * sum_g +
        diff_b * sum_b;

  always_ff @(posedge clk)
    if (!rst)
      sse_acc <= 0;
    else if (pixel_valid && in_tri)
      sse_acc <= sse_acc + px_sse;

  // Exact floor(x/255) for x in [0, 65025] via the magic-number reciprocal.
  function automatic [7:0] udiv255;
      input [15:0] x;
      reg  [31:0] mag_mult;
      begin
          mag_mult = x * 32'd32897;   // magic number :>
          udiv255  = mag_mult[31:23];
      end
  endfunction

  // From RasterizerDispatch
  // The only pixels we have to consider are the pixels which satisfy:
  //
  // ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0))
  //
  // Then the following can be computed per channel to get the resultant sse
  //
  //  c_col.r = (t_col.r*(255-alpha) + tri_col.r*alpha) / 255
  //
  // Note: the numerator is always >= 0, so plain unsigned floor division
  // matches the reference model's `// 255`.
  //
  //  int d_r = b_col.r - c_col.r;   // = c_r - b_r
  //  int s_r = 2 * t_col.r - b_col.r - c_col.r; // = c_r + b_r
  //  acc += d_r * s_r
  //
  // The q / 255 can be combinatorically as well:
  // udiv_255(q) = (q * 32897) >> 23 // magic number :>
  //
  // q * 32897 fits in 32 bits for q <= 65025.
  //
  //  This results in 7 multiplies per pixel. These are multiply reduced forms,
  //  see src/rasterizer.cpp and src/sse.cpp for the full algorithm

endmodule
