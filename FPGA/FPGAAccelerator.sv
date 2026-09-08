module FPGAAccelerator
  import common::*;
  #(
  parameter integer C_AXI_DATA_WIDTH=32, // must be 32 (decode assumes 32-bit words)
  parameter integer C_AXI_ADDR_WIDTH=6,   // 4 index bits after ADDRLSB -> up to 16 regs
  parameter integer C_AXI_STREAM_WIDTH=64,
  parameter integer NUM_LANES = 1
  ) (

    // AXI interface passthrough

    input  wire          s_axi_aclk, // Clock
    input  wire          s_axi_aresetn, // Active low reset

    // Write Addr
    input  wire          s_axi_awvalid, // Is the control valid?
    output  wire          s_axi_awready, // Worker is ready for control
    input  wire  [C_AXI_ADDR_WIDTH-1:0]    s_axi_awaddr, // Write addr
    input  wire  [2:0]        s_axi_awprot, // Protection level

    // Write Data
    input  wire          s_axi_wvalid, // Is the data valid?
    output  wire          s_axi_wready, // Worker is ready for data
    input  wire  [C_AXI_DATA_WIDTH-1:0]    s_axi_wdata, // Write data bus
    input  wire  [C_AXI_DATA_WIDTH/8-1:0]  s_axi_wstrb, // Which lanes hold
    // valid data, one bit per byte

    // Write confirmation
    output  wire          s_axi_bvalid, // Write responce valid
    input  wire          s_axi_bready, // Master ready to accept write response
    output  wire  [1:0]        s_axi_bresp, // Write Responce

    // Read Addr
    input  wire          s_axi_arvalid, // Control Data valid
    output  wire          s_axi_arready, // Worker ready
    input  wire  [C_AXI_ADDR_WIDTH-1:0]    s_axi_araddr, // Read addr
    input  wire  [2:0]        s_axi_arprot, // Protection level

    // Read Data
    output  wire          s_axi_rvalid, // Data valid
    input  wire          s_axi_rready, // Master ready to recive
    output  wire  [C_AXI_DATA_WIDTH-1:0]    s_axi_rdata, // Data bus
    output  wire  [1:0]        s_axi_rresp, // Read responce

    input logic s_axi_tvalid,
    output wire s_axi_tready,
    input  wire  [C_AXI_STREAM_WIDTH-1:0] s_axi_tdata, // 64 bit stream databus
    input wire s_axi_tlast
  );

  wire start;
  wire rst_render;
  wire busy;
  wire done;
  wire logic [63:0] delta_sse;

  wire triangle_t triangle;
  wire vertex_t max_coord;

  wire pixel_valid;
  wire pixel_last;
  wire render_ready;

  wire color_t t_col [NUM_LANES];
  wire color_t b_col [NUM_LANES];


  AXILiteWorker #(
      .C_AXI_ADDR_WIDTH   (C_AXI_ADDR_WIDTH),
      .C_AXI_DATA_WIDTH   (C_AXI_DATA_WIDTH),
      .C_AXI_STREAM_WIDTH (C_AXI_STREAM_WIDTH)
  ) axi_lite_worker (
      .s_axi_aclk     (s_axi_aclk),
      .s_axi_aresetn  (s_axi_aresetn),

      // Write Addr
      .s_axi_awvalid  (s_axi_awvalid),
      .s_axi_awready  (s_axi_awready),
      .s_axi_awaddr   (s_axi_awaddr),
      .s_axi_awprot   (s_axi_awprot),

      // Write Data
      .s_axi_wvalid   (s_axi_wvalid),
      .s_axi_wready   (s_axi_wready),
      .s_axi_wdata    (s_axi_wdata),
      .s_axi_wstrb    (s_axi_wstrb),

      // Write confirmation
      .s_axi_bvalid   (s_axi_bvalid),
      .s_axi_bready   (s_axi_bready),
      .s_axi_bresp    (s_axi_bresp),

      // Read Addr
      .s_axi_arvalid  (s_axi_arvalid),
      .s_axi_arready  (s_axi_arready),
      .s_axi_araddr   (s_axi_araddr),
      .s_axi_arprot   (s_axi_arprot),

      // Read Data
      .s_axi_rvalid   (s_axi_rvalid),
      .s_axi_rready   (s_axi_rready),
      .s_axi_rdata    (s_axi_rdata),
      .s_axi_rresp    (s_axi_rresp),

      // Datapath control
      .start          (start),
      .rst_render     (rst_render),
      .busy           (busy),
      .done           (done),
      .delta_sse      (delta_sse),

      .triangle       (triangle),
      .max_coord      (max_coord),

      // AXI Stream
      .s_axi_tvalid   (s_axi_tvalid),
      .s_axi_tready   (s_axi_tready),
      .s_axi_tdata    (s_axi_tdata),
      .s_axi_tlast    (s_axi_tlast),

      .pixel_valid    (pixel_valid),
      .pixel_last     (pixel_last),
      .render_ready   (render_ready),

      .t_col          (t_col[0]),
      .b_col          (b_col[0])
    );
  Rasterizer #(
    .NUM_LANES (NUM_LANES)
    ) rasterizer (
      .clk            (s_axi_aclk),
      .rst            (s_axi_aresetn & ~rst_render),

      .pixel_valid    (pixel_valid),
      .pixel_last     (pixel_last),

      .triangle       (triangle),
      .max_coord      (max_coord),

      .t_col          (t_col),
      .b_col          (b_col),

      .render_ready   (render_ready),

      .t_sse_acc      (delta_sse)
    );
endmodule
