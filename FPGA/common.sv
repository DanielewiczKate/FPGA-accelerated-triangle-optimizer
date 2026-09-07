package common;
  localparam integer LANES = 1;
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
