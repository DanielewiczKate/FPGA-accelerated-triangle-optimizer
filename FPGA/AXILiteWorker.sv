module AXILiteWorker
  import common::*;
  #(
  parameter integer C_AXI_DATA_WIDTH=32, // must be 32 (decode assumes 32-bit words)
  parameter integer C_AXI_ADDR_WIDTH=6,   // 4 index bits after ADDRLSB -> up to 16 regs
  parameter integer C_AXI_STREAM_WIDTH=64
  )(
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

    // To / from the render datapath
    // delta_sse is only defined when the done bit of the status register is
    // high, otherwise it is undefined behaviour
    output  wire          start,     // 1-cycle pulse when CTRL bit0 is written 1
    input   wire          busy,      // datapath mid-render (reads back in STATUS)
    input   wire          done,      // datapath finished  (reads back in STATUS)
    input   wire  [63:0]  delta_sse,  // result; readable at SSE_LO / SSE_HI.

    output triangle_t triangle, // candidate triangle
    output vertex_t max_coord, // bbox max, assuming 0,0 min

    ////////////////////////////////////////////////////////////////////////////
    // AXI STREAM
    ////////////////////////////////////////////////////////////////////////////

    // Note the null and valid systems are not implmemented to reduce
    // computaion requirements
    input logic s_axi_tvalid,
    output wire s_axi_tready,
    input  wire  [C_AXI_STREAM_WIDTH-1:0] s_axi_tdata, // 64 bit stream databus
    input wire s_axi_tlast,

    output logic pixel_valid,
    output logic pixel_last,
    input logic render_ready,

    output var color_t t_col,
    output var color_t b_col
  );



  localparam integer ADDRLSB = $clog2(C_AXI_DATA_WIDTH)-3;

  wire _unused = &{1'b0, s_axi_awprot, s_axi_arprot,
                 s_axi_awaddr[ADDRLSB-1:0], s_axi_araddr[ADDRLSB-1:0]};

  // Register map. Word index = s_axi_a{W,R}ADDR[C_AXI_ADDR_WIDTH-1:ADDRLSB].
  localparam [3:0]
    A_CTRL    = 4'h0,  // RW  bit0 = start (self-clearing)
    A_STATUS  = 4'h1,  // RO  bit0 = busy, bit1 = done
    A_MAXC    = 4'h2,  // RW  bounding box, x=[15:0] y=[31:16]
    A_TRI_V0  = 4'h3,  // RW
    A_TRI_V1  = 4'h4,  // RW
    A_TRI_V2  = 4'h5,  // RW
    A_TRI_COL = 4'h6,  // RW
    A_SSE_LO  = 4'h7,  // RO  delta_sse[31:0]
    A_SSE_HI  = 4'h8;  // RO  delta_sse[63:32]

  wire [C_AXI_ADDR_WIDTH-ADDRLSB-1:0]
    waddr = s_axi_awaddr[C_AXI_ADDR_WIDTH-1:ADDRLSB],
    raddr = s_axi_araddr[C_AXI_ADDR_WIDTH-1:ADDRLSB];

  wire        axil_read_ready;

  reg        axil_bvalid;
  reg  [C_AXI_DATA_WIDTH-1:0]  axil_read_data;
  reg        axil_read_valid;

  reg  [C_AXI_DATA_WIDTH-1:0]
    // RW registers
    ctrl,
    max_coords, // bbox max, vertex_t: x=[31:16] y=[15:0]
    c_tri_v0, // candidate tri vertex 0, vertex_t layout
    c_tri_v1,
    c_tri_v2,
    c_tri_col, // candidate tri color, color_t: r=[31:24] g=[23:16] b=[15:8] a=[7:0]
    // RO registers
    status,
    sse_out_0, // first 32 bits of SSE
    sse_out_1; // last 32 bits of SSE

  // SIMPLE_WRITEs implmentaion
  reg axil_awready;

  initial  axil_awready = 1'b0;
  always_ff @(posedge s_axi_aclk)
  if (!s_axi_aresetn)
  begin
    axil_awready <= 1'b0;
  end // Reset to not ready
  else
  begin
    // Uses must ensure data and adress is valid on the same clock cylce
    // to write.
    axil_awready <= !axil_awready // ready for only one tick
      && (s_axi_awvalid && s_axi_wvalid) // write address and data valid
      && (!s_axi_bvalid || s_axi_bready); // write responce is valid or
                                          // master is ready
  end // Not reset path

  assign  s_axi_awready = axil_awready; // worker ready for control
  assign  s_axi_wready  = axil_awready; // worker ready for data

  initial  axil_bvalid = 0;
  always_ff @(posedge s_axi_aclk)
  if (!s_axi_aresetn)
    axil_bvalid <= 0;
  else if (axil_awready)
    axil_bvalid <= 1;
  else if (s_axi_bready)
    axil_bvalid <= 0;

  assign  s_axi_bvalid = axil_bvalid;
  assign  s_axi_bresp = 2'b00;

  // Simple Reads
  reg  axil_arready;

  always_comb
    axil_arready = !s_axi_rvalid;

  assign  s_axi_arready = axil_arready;
  assign  axil_read_ready = (s_axi_arvalid && s_axi_arready);

  initial  axil_read_valid = 1'b0;
  always_ff @(posedge s_axi_aclk)
  if (!s_axi_aresetn)
    axil_read_valid <= 1'b0;
  else if (axil_read_ready)
    axil_read_valid <= 1'b1;
  else if (s_axi_rready)
    axil_read_valid <= 1'b0;

  assign  s_axi_rvalid = axil_read_valid;
  assign  s_axi_rdata  = axil_read_data;
  assign  s_axi_rresp = 2'b00; // OK status always

  // Register management
  //
  // RW registers (ctrl, max_coords, c_tri_v0..2, c_tri_col) are driven by the
  // AXI write channel below. RO views (status, sse_out_*) are pure functions of
  // the datapath inputs.

  logic done_flag; // done flag that resets on start tick
  always_ff @(posedge s_axi_aclk)
  begin
    if (!s_axi_aresetn) done_flag <= 1'b0;
    else if (start) done_flag <= 1'b0;
    else if (done) done_flag <= 1'b1;
  end

  always_comb
  begin
    status    = {{(C_AXI_DATA_WIDTH-2){1'b0}}, done_flag, busy};
    sse_out_0 = delta_sse[31:0];
    sse_out_1 = delta_sse[63:32];
  end

  initial axil_read_data = 0;

  // This detimines what sections of the new data are to be applied to the
  // registers. This is using the per byte valid status. I may remove this,
  // as it slows down the implementaion, and I am fine to force the driver
  // to allays give valid data. It really depends on how the stream
  // implmentation works.
  //
  // TODO: review above
  function automatic [C_AXI_DATA_WIDTH-1:0]  apply_wstrb;
    input  [C_AXI_DATA_WIDTH-1:0]    prior_data;
    input  [C_AXI_DATA_WIDTH-1:0]    new_data;
    input  [C_AXI_DATA_WIDTH/8-1:0]  wstrb;

    integer  k;
    for(k=0; k<C_AXI_DATA_WIDTH/8; k=k+1)
    begin
      apply_wstrb[k*8 +: 8]
        = wstrb[k] ? new_data[k*8 +: 8] : prior_data[k*8 +: 8];
    end
  endfunction

  // --- Write decode --------------------------------------------------------
  // On the single-cycle axil_awready pulse, route byte-strobed write data to
  // the addressed RW register. CTRL bit0 is masked to 0 in storage so the
  // start bit is self-clearing; the actual kick is the `start` pulse below.
  initial
  begin
    ctrl       = 0;
    max_coords = 0;
    c_tri_v0   = 0;
    c_tri_v1   = 0;
    c_tri_v2   = 0;
    c_tri_col  = 0;
  end
  always_ff @(posedge s_axi_aclk)
  if (!s_axi_aresetn)
  begin
    ctrl       <= 0;
    max_coords <= 0;
    c_tri_v0   <= 0;
    c_tri_v1   <= 0;
    c_tri_v2   <= 0;
    c_tri_col  <= 0;
  end
  else if (axil_awready)
  begin
    case (waddr)
      A_CTRL   : ctrl       <= apply_wstrb(ctrl,       s_axi_wdata, s_axi_wstrb)
                                 & {{(C_AXI_DATA_WIDTH-1){1'b1}}, 1'b0};
      A_MAXC   : max_coords <= apply_wstrb(max_coords, s_axi_wdata, s_axi_wstrb);
      A_TRI_V0 : c_tri_v0   <= apply_wstrb(c_tri_v0,   s_axi_wdata, s_axi_wstrb);
      A_TRI_V1 : c_tri_v1   <= apply_wstrb(c_tri_v1,   s_axi_wdata, s_axi_wstrb);
      A_TRI_V2 : c_tri_v2   <= apply_wstrb(c_tri_v2,   s_axi_wdata, s_axi_wstrb);
      A_TRI_COL: c_tri_col  <= apply_wstrb(c_tri_col,  s_axi_wdata, s_axi_wstrb);
      default  : ; // RO or unmapped: ignore, BRESP stays OKAY
    endcase
  end

  // start: 1-cycle pulse when CTRL bit0 is written with its byte strobe set.
  assign start = axil_awready && (waddr == A_CTRL)
                 && s_axi_wstrb[0] && s_axi_wdata[0];

  // --- Read mux ----------------------------------------------------------
  // Capture only on an accepted read (axil_read_ready) so raddr is valid and
  // we are not latching off an idle bus. Held until the master takes it.
  always_ff @(posedge s_axi_aclk)
  if (!s_axi_aresetn)
    axil_read_data <= 0;
  else if (axil_read_ready)
  begin
    case (raddr)
      A_CTRL   : axil_read_data <= ctrl;
      A_STATUS : axil_read_data <= status;
      A_MAXC   : axil_read_data <= max_coords;
      A_TRI_V0 : axil_read_data <= c_tri_v0;
      A_TRI_V1 : axil_read_data <= c_tri_v1;
      A_TRI_V2 : axil_read_data <= c_tri_v2;
      A_TRI_COL: axil_read_data <= c_tri_col;
      A_SSE_LO : axil_read_data <= sse_out_0;
      A_SSE_HI : axil_read_data <= sse_out_1;
      default  : axil_read_data <= '0;
    endcase
  end

  // Register to output
  assign max_coord = vertex_t'(max_coords);
  assign triangle = triangle_t'({c_tri_col, c_tri_v2, c_tri_v1, c_tri_v0});

  assign s_axi_tready = render_ready;
  always_ff @(posedge s_axi_aclk)
    // reset
  if (!s_axi_aresetn)
  begin
    pixel_valid <= 1'b0;
    pixel_last <= 1'b0;
  end
  else begin
    pixel_valid <= (s_axi_tvalid && s_axi_tready);
    pixel_last <= (s_axi_tvalid && s_axi_tready && s_axi_tlast);

    if(s_axi_tvalid && s_axi_tready)
    begin
      {t_col, b_col} <= s_axi_tdata;
    end
  end

  // TODO: formal tests
endmodule
