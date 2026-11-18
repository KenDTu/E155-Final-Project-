// Author: Ken TU
// Email: ktu@g.hmc.edu
// Date of Creation: 11/16/2026
// Purpose: Deliver control signals to the H-bridge for each of the four motor's direction and velocity using a PWM

`timescale 1ns/1ps

// Instantiate Motor Controller for One Motor
// Outputs the PWM signal to the X_enable, and [clockwise anticlockwise] pair 
// to the [IN1 IN2], or [IN3 IN4] on the H-bridge
module motorcontroller(
  input logic clk,
  input logic enable,
  input logic reset,
  //input logic [1:0] direction,   // which direction is the robot moving?
  output logic alpha_enable,     // which wheel are we enabling?
  output logic clockwise,        // when facing into the motor are we CW or ACW?
  output logic anticlockwise
);

    // =========== HARD CODED VALUES from MCU ===========
    logic [1:0] direction = 2'b00;


    // ============== END OF HARD CODED VALUES =========

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
