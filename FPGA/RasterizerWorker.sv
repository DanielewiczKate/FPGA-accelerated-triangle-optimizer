module RasterizerWorker
  import common::*;
  (
    input logic clk, // active low
    input logic rst,

    // From AXILite worker
    input logic pixel_valid,
    input logic pixel_last,

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

  color_t c_col;
  assign c_col.r = t_col.r + sdiv255($signed({1'b0, tri_col.a}) * (tri_col.r - t_col.r));
  assign c_col.g = t_col.g + sdiv255($signed({1'b0, tri_col.a}) * (tri_col.g - t_col.g));
  assign c_col.b = t_col.b + sdiv255($signed({1'b0, tri_col.a}) * (tri_col.b - t_col.b));

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

  // Exact trunc(x/255) for x in [-65025, 65025]
  function automatic signed [16:0] sdiv255;
      input signed [16:0] x;
      reg             sign;
      reg  [15:0]     mag;
      reg  [31:0]     mag_mult;
      reg  [8:0]      udiv;
      begin
          sign     = x[16];
          mag      = sign ? (~x[15:0] + 16'd1) : x[15:0];
          mag_mult = mag * 32'd32897;
          udiv     = mag_mult[31:23];
          sdiv255  = sign ? -$signed({8'd0, udiv}) : $signed({8'd0, udiv});
      end
  endfunction

  // From RasterizeMaster
  // The only pixels we have to consiter are the pixels which satisfy:
  //
  // ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0))
  //
  // Then the following can be computer per channel to get the resutant sse
  //
  //  c_col.r = t_col + alpha*(tri_col - t_col) / 255
  //
  // Note: this computaion must be done with signed
  //
  //  int d_r = b_col.r - c_col.r;   // = c_r - b_r
  //  int s_r = 2 * t_col.r - b_col.r - c_col.r; // = c_r + b_r
  //  acc += d_r * s_r
  //
  // The q / 255 can be combinatorically as well:
  // udiv_255(q) = (q * 32897) >> 23 // magic number :>
  // q / 255 = ~udiv_255(abs(q)) + 1
  //
  // q * 32897 has a 24 bit result.
  //
  //  This results in 7 multiplies per pixel. These are multiply reduced forms,
  //  see src/rasterizer.cpp and src/sse.cpp for the full algorythem

endmodule
