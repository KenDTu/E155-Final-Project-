// Author: Ken TU
// Email: ktu@g.hmc.edu
// Date of Creation: 11/18/2026
// Purpose: Decides the rotation direction e.g. CW/ACW based on direction input

module rotation_driver(
  input  logic direction,
  // input  logic wheel idk
  output logic clockwise,
  output logic anticlockwise
);
  // 
  // Requires an if statement to select the wheel because each wheel is different
    always_comb
        case(direction)
            2'b00:  clockwise = 1'b0;
            2'b01:  clockwise = 1'b1;
            2'b10:  clockwise = 1'b1;
            2'b11:  clockwise = 1'b0;
            default: clockwise = 1'b0
        endcase

    assign anticlockwise = ~clockwise;

endmodule
