module PixelIndexer
  import common::*;
  (

    input logic clk,
    input logic rst, // reset, active low

    // note coord y_max is not enforced. This is to be managed by the stream
    // master
    input vertex_t max_coord,

    // Strobe indicating that the pixel currently stored in coord has been
    // used, and the next cycle will process the next coordinate
    input logic pixel_done,
    input logic pixel_valid,


    // current coord of interest.
    // This ranges between [0, max_coord - 1]
    output var vertex_t coord
  );

  wire advance = pixel_valid && pixel_done;
  vertex_t next_coord;

  always_ff @(posedge clk)
  begin
    if (!rst) coord <= '0;
    else if (advance) coord <= next_coord;
  end

  always_comb
  begin
   if (coord.x + 1 >= max_coord.x)
    begin
      next_coord.x = '0;
      next_coord.y = (coord.y + 1 >= max_coord.y) ? '0 : coord.y + 1;
    end else
    begin
      next_coord.x = coord.x + 1;
      next_coord.y = coord.y;
    end
  end
endmodule
