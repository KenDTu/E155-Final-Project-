// Author: Ken TU
// Email: ktu@g.hmc.edu
// Date of Creation: 11/16/2026
// Purpose: Deliver control signals to the H-bridge for each of the four motor's direction and velocity using a PWM

`timescale 1ns/1ps

// Instantiate Motor Controller for One Motor
module motorcontroller(
  input  logic clk,
  input  logic reset,
  output logic out
);

    // alpha's enable 
    logic alpha_enable = 1'b0;

    // Value of the PWM to send to H-bridge
    logic vPWM = 1'b0;

    // Peiod of the PWM signal expressed in clock cycles
    // multiply this value by the clock period to get the value in ns
    logic [31:0] periodLength = 500;

    // pulse width of the PWM signal expressed in clock cycles
    // multiply this value by the clock period to get the value in ns
    logic [31:0] pulseLength = 250;

    // Data variables?
    logic [31:0] counter = 0;

    // NB: integer <=> logic signed [31:0]

    // Internal high-speed oscillator
  	HSOSC #(.CLKHF_DIV(2'b01))
		  hf_osc (.CLKHFPU(1'b1), .CLKHFEN(1'b1), .CLKHF(int_osc));

    // Instantiates counter to clock divide the internal clock

    counter counter_alpha(.clk(int_osc), .reset(reset), .enable(enable));

    // Activate the PWM by sending 0 or 1 to vPWM
    always_ff @(posedge clk)
      begin
        if (pulseLength > counter)   vPWM <= 1'b1;
        else                         vPWM <= 1'b0;
      end

    assign alpha_enable = vPWM;
  
endmodule
