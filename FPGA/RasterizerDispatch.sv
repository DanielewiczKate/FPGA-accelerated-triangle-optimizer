module RasterizerDispatch
  import common::*;
  #(
    parameter integer NUM_LANES = 1
  )
  (
    input logic clk,
    input logic rst, // active low

    // From AXILite worker
    input logic pixel_valid,
    input logic pixel_last,

    input triangle_t triangle, // candidate triangle
    input vertex_t max_coord, // bbox max, assuming 0,0 min

    input color_t t_col [NUM_LANES],
    input color_t b_col [NUM_LANES],

    // Asserted once all the precompute logic has been handled
    output var logic render_ready,

    // Per RenderWorker IO, since I am not yet doing lane routing, this is
    // just forwarded
    output color_t t_col_out [NUM_LANES],
    output color_t b_col_out [NUM_LANES],
    output color_t tri_col_out [NUM_LANES],

    output var s33_t s_d0 [NUM_LANES],
    output var s33_t s_d1 [NUM_LANES],
    output var s33_t s_d2 [NUM_LANES],
    output var logic [31:0] idx [NUM_LANES]
  );
  vertex_t v0, v1, v2;

  always_comb begin
    for (int i = 0; i < NUM_LANES; i++) begin
      t_col_out[i] = t_col[i];
      b_col_out[i] = b_col[i];
      tri_col_out[i] = triangle.color;
    end
  end


  assign v0 = triangle.verts[0];
  assign v1 = triangle.verts[1];
  assign v2 = triangle.verts[2];

  s33_t A0;
  s33_t A1;
  s33_t A2;

  s33_t B0;
  s33_t B1;
  s33_t B2;

  s33_t d0_row;
  s33_t d1_row;
  s33_t d2_row;

  always_ff @(posedge clk)
  if (!rst)
  begin
    A0 <= v1.y - v0.y;
    A1 <= v2.y - v1.y;
    A2 <= v0.y - v2.y;

    B0 <= v0.x - v1.x;
    B1 <= v1.x - v2.x;
    B2 <= v2.x - v0.x;

    // TODO change this to a multiplexer, since this takes 6 multipliers. This
    // is done once per image, so no need to do it in a single frame
    d0_row <= -(v0.x * A0 + v0.y * B0);
    d1_row <= -(v1.x * A1 + v1.y * B1);
    d2_row <= -(v2.x * A2 + v2.y * B2);
  end

  // This section detects when the next cycle has a coord.x >= max_coord.x,
  // indicating that the next d vars must be reset

  // does not include lane offsets, but all lanes are relative to them
  // so we do  d*_row_next += NUMLANES * B*
  s33_t d0_row_next;
  s33_t d1_row_next;
  s33_t d2_row_next;


  s33_t s_d0_next [NUM_LANES];
  s33_t s_d1_next [NUM_LANES];
  s33_t s_d2_next [NUM_LANES];
  logic [31:0] idx_next [NUM_LANES];

  // Need to know the location in the row to do wrapping
  logic [16:0] x_next;
  logic [16:0] x;

  always_comb
  begin
    if(x + 1 >= max_coord.x) // next e
    begin
      x_next = 0;
      d0_row_next = d0_row + B0;
      d1_row_next = d1_row + B1;
      d2_row_next = d2_row + B2;
      for (int i = 0; i < NUM_LANES; i++) begin
        idx_next[i] = idx[i] + 1;
        s_d0_next[i] = d0_row + B0;
        s_d1_next[i] = d1_row + B1;
        s_d2_next[i] = d2_row + B2;
      end
    end
    else begin // Standard line.
      x_next = x + 1;
      d0_row_next = d0_row;
      d1_row_next = d1_row;
      d2_row_next = d2_row;
      for (int i = 0; i < NUM_LANES; i++) begin
        idx_next[i] = idx[i] + 1;
        s_d0_next[i] = s_d0[i] + A0;
        s_d1_next[i] = s_d1[i] + A1;
        s_d2_next[i] = s_d2[i] + A2;
      end
    end
  end

  wire advance = pixel_valid && render_ready;

  always_ff @(posedge clk)
  if(!rst)
  begin
    render_ready <= 0;
    x <= 0;
    for (int i = 0; i < NUM_LANES; i++) begin
      // TODO: find a better way to initialize this
      s_d0[i] <= -(v0.x * A0 + v0.y * B0);
      s_d1[i] <= -(v1.x * A1 + v1.y * B1);
      s_d2[i] <= -(v2.x * A2 + v2.y * B2);
      idx[i] <= 0;
    end
  end
  else begin
    render_ready <= 1; // Allows for one cycle to prime the input
    if(advance)
    begin
      x <= x_next;
      d0_row <= d0_row_next;
      d1_row <= d1_row_next;
      d2_row <= d2_row_next;
      for (int i = 0; i < NUM_LANES; i++) begin
        s_d0[i] <= s_d0_next[i];
        s_d1[i] <= s_d1_next[i];
        s_d2[i] <= s_d2_next[i];
        idx[i] <= idx_next[i];
      end
    end
  end






  // This rasterizer dispatch is based on RasterizeTriangleV2
  //
  // RasterizerDispatch can control many different rasterizers, RasterizerDispatch
  // is what organizes the various rasterizers. This includes giving each
  // their color data, and their edge function values. This was chosen to allow
  // for future multi lane functionality. At the moment, I am not implementing
  // the multilane functionality. So, this is an unnecessary abstraction.
  //
  // rst is active low
  //
  // This system first starts when the start input wire is hit; this triggers
  // the precomputation stages of the renderer. The C++ which accomplishes this is
  // pasted below
  //
  // Per-edge step deltas (constant across the whole triangle).
  //  const int64_t A0 = vy[1] - vy[0], B0 = vx[0] - vx[1];
  //  const int64_t A1 = vy[2] - vy[1], B1 = vx[1] - vx[2];
  //  const int64_t A2 = vy[0] - vy[2], B2 = vx[2] - vx[0];
  //
  //  Edge function values at (x_min, y_min) — the only multiplies needed.
  //  int64_t d0_row = - vx[0] * A0 + vy[0] * (vx[1] - vx[0]);
  //  int64_t d1_row = - vx[1] * A1 + vy[1] * (vx[2] - vx[1]);
  //  int64_t d2_row = - vx[2] * A2 + vy[2] * (vx[0] - vx[2]);
  //
  //
  // Then we serve d0, d1, d2, and i to the Rasterizer worker for each input
  // pixel. These evolve as described below:
  //
  //  for (pcrd_t y = 0; y <= max_coord.y; y++) {
  //      int64_t d0 = d0_row, d1 = d1_row, d2 = d2_row;
  //      size_t row_i = (size_t)y * x_size;
  //
  //      for (pcrd_t x = 0; x <= max_coord.x; x++) {
  //              i++
  //
  //              // Rasterizer worker does this
  //
  //          }
  //          d0 += A0; d1 += A1; d2 += A2;
  //      }
  //      d0_row += B0; d1_row += B1; d2_row += B2;
  //  }
  //
  // This is a convenient way to do this, since it is trivial to add extra lanes
  // by adding extra rows, by adding an offset to d0, d1, d2, and i.
  //
  // The only pixels we have to consider are the pixels which satisfy:
  //
  // ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0))
  //
  // Then the following can be computed per channel to get the resultant sse
  //
  //  c_col.r = t_col + alpha*(tri_col - t_col) / 255
  //
  // Note: this computation must be done with signed
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
  //  see src/rasterizer.cpp and src/sse.cpp for the full algorithm

endmodule
