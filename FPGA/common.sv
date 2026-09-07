package common;
    typedef struct packed {
        logic [7:0] r, g, b, a;
    } color_t;
    typedef struct packed {
        logic [15:0] x, y;
    } vertex_t;
    typedef struct packed {
        color_t color;
        vertex_t [2:0] verts;
    } triangle_t;
endpackage



/*
module AXILiteWorker (
    // input logic
    input logic clk,
    input logic rst, // active high reset
    input logic [7:0] addr,
    input logic [31:0] din,

    // Backpressure signal
    output logic rdy,

    // Output to the next modules

    // The system expects that the min coord is 0,0, meaning the bounding box
    // is from 0,0 to coord_max the driver is responcible for entering the
    // correct datastream

    // Preamble data
    output vertex_t coord_max, // bounding box dim
    output vertex_t coord_px, // current pixel
    output triangle_t c_tri, // candidate triangle

    // Streamed data
    output color_t t_col, // target color at this pixel
    output color_t b_col, // prev_best color at this pixel
    output logic stream_ready, // indicates that all preamble has been loaded

    input logic render_active, // render is active and is expecting datastream
    input logic [63:0] delta_sse // only valid once all coords have been
                                 // streamed to renderer
);

    timeunit 1ns;
    timeprecision 1ns;

    always_ff @(posedge clk) begin

    end // end always @(poseedge clk)
endmodule
*/
