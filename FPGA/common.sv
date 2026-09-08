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
    // sigend 9 bit int. Usefull for char subtractions
    typedef logic signed [16:0] s17_t;
    // signed 33 bit int. This is used for edge calcualation in
    // the render system
    typedef logic signed [32:0] s33_t;
endpackage
