// rotation_driver_alpha.sv
// Author: Ken TU
// Email: ktu@g.hmc.edu
// Date of Creation: 11/18/2026
// Purpose: Decides the rotation direction e.g. CW/ACW based on direction input

`timescale 1ns/1ps
module rotation_driver_alpha(
  input logic [1:0] direction,
  output logic [7:6] motor_vector
);

    always_comb
        case(direction) // SET to ALPHA -- motor_vec = CW ACW
            2'b00:  motor_vector = 2'b01; // forward
            2'b01:  motor_vector = 2'b10; // backwards
            2'b10:  motor_vector = 2'b10; // left
            2'b11:  motor_vector = 2'b01; // right
            default: motor_vector = 2'b00;
        endcase
  
endmodule
